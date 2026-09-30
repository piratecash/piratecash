// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2014-2025 The Dash Core developers
// Copyright (c) 2018-2026 The PirateCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <node/miner.h>

#include <net.h>
#include <pos_kernel.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#include <wallet/receive.h>
#include <wallet/scriptpubkeyman.h>
#endif

#ifndef WIN32
#include <sys/resource.h>
#endif

#include <chain.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <deploymentstatus.h>
#include <node/context.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <script/signingprovider.h>
#include <sync.h>
#include <timedata.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <util/threadnames.h>
#include <validation.h>

#include <chainlock/chainlock.h>
#include <chainlock/handler.h>
#include <evo/specialtx.h>
#include <evo/cbtx.h>
#include <evo/chainhelper.h>
#include <evo/creditpool.h>
#include <evo/mnhftx.h>
#include <evo/deterministicmns.h>
#include <evo/simplifiedmns.h>
#include <evo/specialtxman.h>
#include <governance/governance.h>
#include <llmq/blockprocessor.h>
#include <llmq/context.h>
#include <llmq/options.h>
#include <llmq/snapshot.h>
#include <masternode/payments.h>
#include <masternode/sync.h>

#include <algorithm>
#include <string>
#include <utility>

namespace node {
std::atomic<int64_t> nLastCoinStakeSearchTime{0};

namespace {
RecursiveMutex g_mining_status_mutex;
std::string miningStatus GUARDED_BY(g_mining_status_mutex);

#ifdef ENABLE_WALLET
void SetMiningStatus(std::string status)
{
    LOCK(g_mining_status_mutex);
    miningStatus = std::move(status);
}
#endif

} // namespace

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime{std::max<int64_t>(pindexPrev->GetMedianTimePast() + 1, TicksSinceEpoch<std::chrono::seconds>(NodeClock::now()))};

    if (nOldTime < nNewTime) {
        pblock->nTime = nNewTime;
    }

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks) {
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);
    }

    return nNewTime - nOldTime;
}

static BlockAssembler::Options ClampOptions(BlockAssembler::Options options)
{
    options.nBlockMaxSize = std::clamp<size_t>(options.nBlockMaxSize, 1000, DEFAULT_BLOCK_MAX_SIZE);
    return options;
}

BlockAssembler::BlockAssembler(Chainstate& chainstate, const NodeContext& node, const CTxMemPool* mempool, const Options& options) :
      m_chain_helper(chainstate.ChainHelper()),
      m_chainstate{chainstate},
      m_evoDb(*Assert(node.evodb)),
      m_chainlocks(*Assert(node.chainlocks)),
      m_clhandler(*Assert(node.clhandler)),
      chainparams(chainstate.m_chainman.GetParams()),
      m_mempool{mempool},
      m_quorum_block_processor(*Assert(Assert(node.llmq_ctx)->quorum_block_processor)),
      m_options{ClampOptions(options)},
      m_block_max_size_configured{m_options.nBlockMaxSize}
{
}

void ApplyArgsManOptions(const ArgsManager& args, BlockAssembler::Options& options)
{
    // Block resource limits
    options.nBlockMaxSize = std::max<int64_t>(1000, args.GetIntArg("-blockmaxsize", options.nBlockMaxSize));
    if (const auto blockmintxfee{args.GetArg("-blockmintxfee")}) {
        if (const auto parsed{ParseMoney(*blockmintxfee)}) options.blockMinFeeRate = CFeeRate{*parsed};
    }
}
static BlockAssembler::Options ConfiguredOptions()
{
    BlockAssembler::Options options;
    ApplyArgsManOptions(gArgs, options);
    return options;
}

BlockAssembler::BlockAssembler(Chainstate& chainstate, const NodeContext& node, const CTxMemPool* mempool)
    : BlockAssembler(chainstate, node, mempool, ConfiguredOptions()) {}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockSize = 1000;
    nBlockSigOps = 100;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

// Helper to calculate best chainlock
static bool CalcCbTxBestChainlock(const chainlock::Chainlocks& chainlocks, const CBlockIndex* pindexPrev,
                                  uint32_t& bestCLHeightDiff, CBLSSignature& bestCLSignature)
{
    auto best_clsig = chainlocks.GetBestChainLock();
    if (best_clsig.getHeight() < Params().GetConsensus().DeploymentHeight(Consensus::DEPLOYMENT_V19)) {
        // We don't want legacy BLS ChainLocks in CbTx (can happen on regtest/devenets)
        best_clsig = chainlock::ChainLockSig{};
    }
    if (best_clsig.getHeight() == pindexPrev->nHeight) {
        // Our best CL is the newest one possible
        bestCLHeightDiff = 0;
        bestCLSignature = best_clsig.getSig();
        return true;
    }

    auto prevBlockCoinbaseChainlock = GetNonNullCoinbaseChainlock(pindexPrev);
    if (prevBlockCoinbaseChainlock.has_value()) {
        // Previous block Coinbase contains a non-null CL: We must insert the same sig or a better (newest) one
        if (best_clsig.IsNull()) {
            // We don't know any CL, therefore inserting the CL of the previous block
            bestCLHeightDiff = prevBlockCoinbaseChainlock->second + 1;
            bestCLSignature = prevBlockCoinbaseChainlock->first;
            return true;
        }

        // We check if our best CL is newer than the one from previous block Coinbase
        int curCLHeight = best_clsig.getHeight();
        int prevCLHeight = pindexPrev->nHeight - static_cast<int>(prevBlockCoinbaseChainlock->second) - 1;
        if (curCLHeight < prevCLHeight) {
            // Our best CL isn't newer: inserting CL from previous block
            bestCLHeightDiff = prevBlockCoinbaseChainlock->second + 1;
            bestCLSignature = prevBlockCoinbaseChainlock->first;
        }
        else {
            // Our best CL is newer
            bestCLHeightDiff = pindexPrev->nHeight - best_clsig.getHeight();
            bestCLSignature = best_clsig.getSig();
        }

        return true;
    }
    else {
        // Previous block Coinbase has no CL. We can either insert null or any valid CL
        if (best_clsig.IsNull()) {
            // We don't know any CL, therefore inserting a null CL
            bestCLHeightDiff = 0;
            bestCLSignature.Reset();
            return false;
        }

        // Inserting our best CL
        bestCLHeightDiff = pindexPrev->nHeight - best_clsig.getHeight();
        bestCLSignature = best_clsig.getSig();

        return true;
    }
}


std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, std::shared_ptr<wallet::CWallet> pwallet, int64_t block_time, bool isPos)
{
#ifndef ENABLE_WALLET
    if (isPos) {
        LogError("%s: staking requires wallet support", __func__);
        return nullptr;
    }
#endif
    const auto time_start{SteadyClock::now()};

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if (!pblocktemplate.get()) {
        return nullptr;
    }
    CBlock* const pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    if (isPos) {
        // Keep the stake tx pinned at vtx[1] while the v19 block template
        // machinery appends commitments and mempool transactions after it.
        // The v20 credit pool balance is filled after CreateCoinStake() writes
        // the real coinstake transaction into this slot.
        pblock->vtx.emplace_back();
        pblocktemplate->vTxFees.push_back(-1); // updated if stake is found
        pblocktemplate->vTxSigOps.push_back(-1); // updated if stake is found
    }

    WAIT_LOCK(::cs_main, lock_main);
    CBlockIndex* pindexPrev = m_chainstate.m_chain.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    const bool fDIP0001Active_context{DeploymentActiveAfter(pindexPrev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_DIP0001)};
    const bool fDIP0003Active_context{DeploymentActiveAfter(pindexPrev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_DIP0003)};
    const bool fDIP0008Active_context{DeploymentActiveAfter(pindexPrev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_DIP0008)};
    const bool fV20Active_context{DeploymentActiveAfter(pindexPrev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_V20)};
    const bool fV24Active_context{DeploymentActiveAfter(pindexPrev, m_chainstate.m_chainman, Consensus::DEPLOYMENT_V24)};

    // Recompute from the configured limit so repeated PoS templates do not shrink it.
    m_options.nBlockMaxSize = std::max<size_t>(1000, std::min<size_t>(MaxBlockSize(fDIP0001Active_context) - 1000, m_block_max_size_configured));
    m_options.nBlockMaxSigOps = MaxBlockSigOps(fDIP0001Active_context);

#ifdef ENABLE_WALLET
    // PoS: reserve room for the minimal coinstake within the size limit;
    // a PoS block needs at least the coinbase reservation + the coinstake
    // (a warning about too small -blockmaxsize is issued at startup)
    constexpr size_t MIN_STAKE_SIZE_BUDGET = 1000;
    const size_t block_size_limit = isPos ? std::max(m_options.nBlockMaxSize, 1000 + MIN_STAKE_SIZE_BUDGET) : m_options.nBlockMaxSize;
    if (isPos) {
        m_options.nBlockMaxSize = block_size_limit - MIN_STAKE_SIZE_BUDGET;
        // Reserve sigops for the coinstake outputs (one per P2PKH output,
        // bounded by nStakeMaxSplit and by the coinstake size budget)
        const size_t stake_sigops_reserve = std::min<size_t>(
            static_cast<size_t>(Assert(pwallet)->nStakeMaxSplit) + 2,
            MAX_STANDARD_TX_SIZE / 36 + 2);
        m_options.nBlockMaxSigOps -= std::min(m_options.nBlockMaxSigOps, stake_sigops_reserve);
    }
#endif

    pblock->nVersion = isPos
                          ? ComputeBlockVersion(pindexPrev, chainparams.GetConsensus(), chainparams.BIP9CheckMasternodesUpgraded(), true)
                          : m_chainstate.m_chainman.m_versionbitscache.ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    // Non-mainnet only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.NetworkIDString() != CBaseChainParams::MAIN) {
        pblock->nVersion = gArgs.GetIntArg("-blockversion", pblock->nVersion);
    }

    pblock->nTime = isPos ? block_time : TicksSinceEpoch<std::chrono::seconds>(NodeClock::now());
    m_lock_time_cutoff = pindexPrev->GetMedianTimePast();

    if (fDIP0003Active_context) {
        for (const Consensus::LLMQParams& params : llmq::GetEnabledQuorumParams(m_chainstate.m_chainman, pindexPrev)) {
            std::vector<CTransactionRef> vqcTx;
            if (m_quorum_block_processor.GetMineableCommitmentsTx(params,
                                                                  nHeight,
                                                                  vqcTx)) {
                for (const auto& qcTx : vqcTx) {
                    pblock->vtx.emplace_back(qcTx);
                    pblocktemplate->vTxFees.emplace_back(0);
                    pblocktemplate->vTxSigOps.emplace_back(0);
                    nBlockSize += qcTx->GetTotalSize();
                    ++nBlockTx;
                }
            }
        }
    }

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    if (m_mempool) {
        LOCK(m_mempool->cs);
        addPackageTxs(*m_mempool, nPackagesSelected, nDescendantsUpdated, pindexPrev);
    }

    const auto time_1{SteadyClock::now()};

    m_last_block_num_txs = nBlockTx;
    m_last_block_size = nBlockSize;
    LogPrintf("CreateNewBlock(): total size %u txs: %u fees: %ld sigops %d\n", nBlockSize, nBlockTx, nFees, nBlockSigOps);

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;

    // NOTE: unlike in bitcoin, we need to pass PREVIOUS block height here
    CAmount blockSubsidy = GetBlockSubsidyInner(pindexPrev->nBits, pindexPrev->nHeight, chainparams.GetConsensus(), fV20Active_context);
    CAmount blockReward = blockSubsidy + nFees;

    // Compute regular coinbase transaction.
    coinbaseTx.vout[0].nValue = blockReward;

    const auto set_credit_pool_balance = [&](CCbTx& cbTx) {
        BlockValidationState state;
        const auto creditPoolDiff = GetCreditPoolDiffForBlock(*m_chain_helper.credit_pool_manager, *pblock, pindexPrev, chainparams.GetConsensus(), blockSubsidy, state);
        if (creditPoolDiff == std::nullopt) {
            throw std::runtime_error(strprintf("%s: GetCreditPoolDiffForBlock failed: %s", __func__, state.ToString()));
        }

        cbTx.creditPoolBalance = creditPoolDiff->GetTotalLocked();
        if (cbTx.nVersion >= CCbTx::Version::MERKLE_ROOT_ASSETUNLOCKS) {
            cbTx.merkleRootAssetUnlocks = CalcCbTxMerkleRootAssetUnlocks(*pblock);
        }
    };

    if (!fDIP0003Active_context) {
        coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    } else {
        coinbaseTx.vin[0].scriptSig = CScript() << OP_RETURN;

        coinbaseTx.nVersion = 3;
        coinbaseTx.nType = TRANSACTION_COINBASE;

        CCbTx cbTx;

        if (fV24Active_context && fV20Active_context) {
            cbTx.nVersion = CCbTx::Version::MERKLE_ROOT_ASSETUNLOCKS;
        } else if (fV20Active_context) {
            cbTx.nVersion = CCbTx::Version::CLSIG_AND_BALANCE;
        } else if (fDIP0008Active_context) {
            cbTx.nVersion = CCbTx::Version::MERKLE_ROOT_QUORUMS;
        } else {
            cbTx.nVersion = CCbTx::Version::MERKLE_ROOT_MNLIST;
        }

        cbTx.nHeight = nHeight;

        BlockValidationState state;
        CDeterministicMNList mn_list;
        const bool is_v24_active{DeploymentActiveAfter(pindexPrev, m_chainstate.m_chainman, Consensus::DEPLOYMENT_V24)};
        if (!m_chain_helper.special_tx->BuildNewListFromBlock(*pblock, pindexPrev, is_v24_active,
                                                              m_chainstate.CoinsTip(), true, state, mn_list)) {
            throw std::runtime_error(strprintf("%s: BuildNewListFromBlock failed: %s", __func__, state.ToString()));
        }
        if (!CalcCbTxMerkleRootMNList(cbTx.merkleRootMNList, mn_list.to_sml(), state)) {
            throw std::runtime_error(strprintf("%s: CalcCbTxMerkleRootMNList failed: %s", __func__, state.ToString()));
        }
        if (fDIP0008Active_context) {
            if (!CalcCbTxMerkleRootQuorums(*pblock, pindexPrev, m_quorum_block_processor, cbTx.merkleRootQuorums, state)) {
                throw std::runtime_error(strprintf("%s: CalcCbTxMerkleRootQuorums failed: %s", __func__, state.ToString()));
            }
            if (fV20Active_context) {
                if (CalcCbTxBestChainlock(m_chainlocks, pindexPrev, cbTx.bestCLHeightDiff, cbTx.bestCLSignature)) {
                    LogPrintf("CreateNewBlock() h[%d] CbTx bestCLHeightDiff[%d] CLSig[%s]\n", nHeight, cbTx.bestCLHeightDiff, cbTx.bestCLSignature.ToString());
                } else {
                    // not an error
                    LogPrintf("CreateNewBlock() h[%d] CbTx failed to find best CL. Inserting null CL\n", nHeight);
                }
                if (!isPos) {
                    set_credit_pool_balance(cbTx);
                }
            }
        }

        SetTxPayload(coinbaseTx, cbTx);
    }

    // Update coinbase transaction with additional info about masternode and governance payments,
    // get some info back to pass to getblocktemplate
    const MnRewardEra mn_reward_era{GetMnRewardEraAfter(pindexPrev, m_chainstate.m_chainman)};
    m_chain_helper.mn_payments->FillBlockPayments(coinbaseTx, pindexPrev, blockSubsidy, nFees, mn_reward_era, pblocktemplate->voutMasternodePayments, pblocktemplate->voutSuperblockPayments);

    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    pblocktemplate->vTxFees[0] = -nFees;

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    if (!isPos) {
        UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    }
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0;
    pblocktemplate->nPrevBits = pindexPrev->nBits;
    pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(*pblock->vtx[0]);

#ifdef ENABLE_WALLET
    if (isPos) {
        REVERSE_LOCK(lock_main);
        bool sign_block{false};

        assert(pwallet != nullptr);

        if (pwallet->IsLocked(true)) {
            LogError("%s: wallet is locked!", __func__);
            return nullptr;
        }

        CMutableTransaction coinbaseTx(*(pblock->CoinBase()));
        // Coinstake is capped by what is left within the configured block limits
        const size_t stake_size_budget = block_size_limit > nBlockSize ? block_size_limit - nBlockSize : 0;
        const unsigned int max_block_sigops = MaxBlockSigOps(fDIP0001Active_context);
        const size_t stake_sigops_budget = max_block_sigops > nBlockSigOps ? max_block_sigops - nBlockSigOps : 0;
        bool fStakeFound = pwallet->CreateCoinStake(pindexPrev, *pblock, coinbaseTx, stake_size_budget, stake_sigops_budget);

        if (fStakeFound) {
            if (fV20Active_context) {
                auto opt_cbTx = GetTxPayload<CCbTx>(coinbaseTx.vExtraPayload);
                if (!opt_cbTx) {
                    throw std::runtime_error(strprintf("%s: failed to get CbTx payload", __func__));
                }

                CCbTx cbTx = *opt_cbTx;
                {
                    // Credit-pool snapshots share EvoDB state with block connection.
                    LOCK(cs_main);
                    if (pindexPrev != m_chainstate.m_chain.Tip()) return nullptr;
                    set_credit_pool_balance(cbTx);
                }
                SetTxPayload(coinbaseTx, cbTx);
            }

            sign_block = true;
            pblock->CoinBase() = MakeTransactionRef(std::move(coinbaseTx));
            pblocktemplate->vTxFees[1] = 0;
            pblocktemplate->vTxSigOps[1] = GetLegacySigOpCount(*pblock->Stake());
        } else {
            pblock->vtx.erase(pblock->vtx.begin() + 1);
            pblocktemplate->vTxFees.erase(pblocktemplate->vTxFees.begin() + 1);
            pblocktemplate->vTxSigOps.erase(pblocktemplate->vTxSigOps.begin() + 1);
        }

        pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
        pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(*pblock->CoinBase());

        if (sign_block) {
            // GetKey() decrypts through cs_wallet; keep cs_wallet before cs_KeyStore.
            LOCK(pwallet->cs_wallet);
            const SigningProvider* provider = pwallet->GetLegacyScriptPubKeyMan();
            CKey key;
            if (!provider || !provider->GetKey(pblock->posPubKey.GetID(), key) ||
                !key.SignCompact(pblock->GetHash(), pblock->posBlockSig)) {
                LogError("%s: failed to sign block", __func__);
                return nullptr;
            }
        }
    }

#endif

    if (isPos && pindexPrev != m_chainstate.m_chain.Tip()) {
        LogPrint(BCLog::STAKING, "%s: the network has already found another block", __func__);
        return nullptr;
    }

    BlockValidationState state;
    if (m_options.test_block_validity && !TestBlockValidity(state, m_chainlocks, m_evoDb, chainparams, m_chainstate, *pblock, pindexPrev, /*fCheckPOW=*/false, /*fCheckMerkleRoot=*/false)) {
        if (isPos) {
            LogError("%s: TestBlockValidity failed: %s", __func__, state.ToString());
            return nullptr;
        }
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, state.ToString()));
    }
    const auto time_2{SteadyClock::now()};

    LogPrint(BCLog::BENCHMARK, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n",
             Ticks<MillisecondsDouble>(time_1 - time_start), nPackagesSelected, nDescendantsUpdated,
             Ticks<MillisecondsDouble>(time_2 - time_1),
             Ticks<MillisecondsDouble>(time_2 - time_start));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        } else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, unsigned int packageSigOps) const
{
    if (nBlockSize + packageSize >= m_options.nBlockMaxSize) {
        return false;
    }

    if (nBlockSigOps + packageSigOps >= m_options.nBlockMaxSigOps) {
        return false;
    }
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - safe TXs in regard to ChainLocks
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package) const
{
    const bool is_v24_active{
        DeploymentActiveAfter(m_chainstate.m_chain.Tip(), m_chainstate.m_chainman, Consensus::DEPLOYMENT_V24)};
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, m_lock_time_cutoff)) {
            return false;
        }

        // A special transaction that was valid when it entered the mempool can be invalidated by
        // intervening state, most sharply by a fork activating a rule it violates, and nothing
        // evicts it. Selection otherwise trusts mempool validity, so the entry would be picked into
        // every template and TestBlockValidity would then reject the whole block, leaving an honest
        // miner unable to produce one at all. Recheck here, at package granularity: the caller adds
        // every member of a package that passes, so dropping one member while keeping its
        // descendants would itself yield an invalid template. check_sigs is off because signatures
        // were verified on entry and cannot have changed; note CheckMNHFTx() verifies its quorum
        // signature regardless, which is cheap enough here given how rare MNHF signals are.
        if (it->GetTx().IsSpecialTxVersion()) {
            TxValidationState tx_state;
            if (!m_chain_helper.special_tx->CheckSpecialTx(it->GetTx(), m_chainstate.m_chain.Tip(), is_v24_active,
                                                           m_chainstate.CoinsTip(), /*check_sigs=*/false, tx_state)) {
                return false;
            }
        }

        // The shared-collateral covenant applies to every transaction, not only special ones: a
        // normal transaction creating or spending a template output can sit in the mempool across
        // v24 activation on a node accepting nonstandard transactions, and would poison every
        // template through the same TestBlockValidity path described above.
        if (DeploymentActiveAfter(m_chainstate.m_chain.Tip(), m_chainstate.m_chainman,
                                  Consensus::DEPLOYMENT_V24)) {
            TxValidationState tx_state;
            if (!CheckSharedCollateralSpends(it->GetTx(), m_chainstate.CoinsTip(), tx_state) ||
                !CheckSharedCollateralTemplateOutputs(it->GetTx(), tx_state)) {
                return false;
            }
        }

        const auto& txid = it->GetTx().GetHash();
        if (!m_chain_helper.IsInstantSendEnabled() || m_chain_helper.IsInstantSendLocked(txid)) {
            continue;
        }

        if (!it->GetTx().vin.empty() && !m_clhandler.IsTxSafeForMining(txid)) {
            return false;
        }
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblocktemplate->block.vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOps.push_back(iter->GetSigOpCount());
    nBlockSize += iter->GetTxSize();
    ++nBlockTx;
    nBlockSigOps += iter->GetSigOpCount();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee rate %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

/** Add descendants of given transactions to mapModifiedTx with ancestor
 * state updated assuming given transactions are inBlock. Returns number
 * of updated descendants. */
static int UpdatePackagesForAdded(const CTxMemPool& mempool,
                                  const CTxMemPool::setEntries& alreadyAdded,
                                  indexed_modified_transaction_set& mapModifiedTx) EXCLUSIVE_LOCKS_REQUIRED(mempool.cs)
{
    AssertLockHeld(mempool.cs);

    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc)) {
                continue;
            }
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                mit = mapModifiedTx.insert(modEntry).first;
            }
            mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
        }
    }
    return nDescendantsUpdated;
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(const CTxMemPool& mempool, int& nPackagesSelected, int& nDescendantsUpdated, const CBlockIndex* const pindexPrev)
{
    AssertLockHeld(mempool.cs);

    // This credit pool is used only to check withdrawal limits and to find
    // duplicates of indexes. There's used `BlockSubsidy` equaled to 0
    std::optional<CCreditPoolDiff> creditPoolDiff;
    if (DeploymentActiveAfter(pindexPrev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_V20)) {
        CCreditPool creditPool = m_chain_helper.GetCreditPool(pindexPrev);
        creditPoolDiff.emplace(std::move(creditPool), pindexPrev, chainparams.GetConsensus(), 0);
    }

    // This map with signals is used only to find duplicates
    auto signals = m_chain_helper.ehf_manager->GetSignalsStage(pindexPrev);
    const bool is_v24_active{DeploymentActiveAfter(pindexPrev, m_chainstate.m_chainman, Consensus::DEPLOYMENT_V24)};

    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        //
        // Skip entries in mapTx that are already in a block or are present
        // in mapModifiedTx (which implies that the mapTx ancestor state is
        // stale due to ancestor inclusion in the block)
        // Also skip transactions that we've already failed to add. This can happen if
        // we consider a transaction in mapModifiedTx and it fails: we can then
        // potentially consider it again while walking mapTx.  It's currently
        // guaranteed to fail again, but as a belt-and-suspenders check we put it in
        // failedTx and avoid re-evaluation, since the re-evaluation would be using
        // cached size/sigops/fee values that are not actually correct.
        /** Return true if given transaction from mapTx has already been evaluated,
         * or if the transaction's cached data in mapTx is incorrect. */
        if (mi != mempool.mapTx.get<ancestor_score>().end()) {
            auto it = mempool.mapTx.project<0>(mi);
            assert(it != mempool.mapTx.end());
            if (mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it)) {
                ++mi;
                continue;
            }
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        unsigned int packageSigOps = iter->GetSigOpCountWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOps = modit->nSigOpCountWithAncestors;
        }

        if (packageFees < m_options.blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOps)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockSize > m_options.nBlockMaxSize - 1000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        auto ancestors{mempool.AssumeCalculateMemPoolAncestors(__func__, *iter, CTxMemPool::Limits::NoLimits(), /*fSearchForParents=*/false)};

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final and safe
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, sortedEntries);

        auto packageSignals = signals;
        std::vector<CTransactionRef> creditPoolTransactions;
        bool validPackage{true};
        for (const auto& entry : sortedEntries) {
            const auto& tx = entry->GetTx();
            if (std::optional<uint8_t> signal = extractEHFSignal(tx); signal != std::nullopt) {
                if (!packageSignals.emplace(*signal, 0).second) {
                    LogPrintf("%s: package tx %s skipped due to duplicate EHF signal %d\n", __func__,
                              tx.GetHash().ToString(), *signal);
                    validPackage = false;
                    break;
                }
            }
            if (tx.IsSpecialTxVersion() && (tx.nType == TRANSACTION_ASSET_LOCK || tx.nType == TRANSACTION_ASSET_UNLOCK)) {
                // Version 2 asset unlocks are not expiry-evicted: an expired instance stays in
                // the mempool awaiting a re-signed replacement. Skip instances that are not
                // currently minable (expired height window or stale quorum) instead of
                // producing an invalid template.
                if (IsAssetUnlockWithStableTxid(tx)) {
                    TxValidationState state;
                    if (!m_chain_helper.special_tx->CheckSpecialTx(tx, m_chainstate.m_chain.Tip(), is_v24_active,
                                                                   m_chainstate.CoinsTip(), /*check_sigs=*/true, state)) {
                        LogPrintf("%s: package tx %s skipped, asset unlock instance not currently minable: %s\n", __func__,
                                  tx.GetHash().ToString(), state.ToString());
                        validPackage = false;
                        break;
                    }
                }
                creditPoolTransactions.emplace_back(entry->GetSharedTx());
            }
        }

        if (validPackage && creditPoolDiff != std::nullopt && !creditPoolTransactions.empty()) {
            TxValidationState state;
            if (!creditPoolDiff->ProcessLockUnlockTransactions(creditPoolTransactions, state)) {
                LogPrintf("%s: package tx %s skipped due to credit pool state: %s\n", __func__,
                          iter->GetTx().GetHash().ToString(), state.ToString());
                validPackage = false;
            }
        }
        if (!validPackage) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;
        signals = std::move(packageSignals);

        for (size_t i = 0; i < sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(mempool, ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*(pblock->CoinBase()));
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce));
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->CoinBase() = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

#ifdef ENABLE_WALLET
void PoSMiner(std::shared_ptr<wallet::CWallet> pwallet, NodeContext& node, CThreadInterrupt& interrupt)
{
    LogPrintf("PoSMiner started\n");
    util::ThreadRename("piratecash-miner");
    SetThreadPriority(0);

    BlockAssembler ba{Assert(node.chainman)->ActiveChainstate(), node, Assert(node.mempool.get())};
    CScript coinbaseScript; // unused for PoS

    //control the amount of times the client will check for mintable coins
    bool fMintableCoins = false;
    int nMintableLastCheck = 0;
    int last_height = -1;
    int64_t start_block_time = 0;
    const CChainParams& chainparams = Params();

    while (!interrupt) {
        auto hash_interval = std::max(pwallet->nHashInterval, (unsigned int)1);
        interrupt.sleep_for(std::chrono::seconds(hash_interval));

        {
            CBlockIndex* pindexPrev = WITH_LOCK(cs_main, return Assert(node.chainman)->ActiveChain().Tip());

            if (!pindexPrev) {
                interrupt.sleep_for(std::chrono::seconds(1));
                SetMiningStatus(":<br>- no active blocks");
                LogPrint(BCLog::STAKING, "%s : %s \n", __func__, getMiningStatus());
                continue;
            }

            if (!IsPoSEnforcedHeight(pindexPrev->nHeight + 1) && !IsPoSV2EnforcedHeight(pindexPrev->nHeight + 1) && !pindexPrev->IsProofOfStake()) {
                interrupt.sleep_for(std::chrono::seconds(hash_interval));
                SetMiningStatus(":<br>- PoS is not enabled at height " + std::to_string(pindexPrev->nHeight + 1));
                LogPrint(BCLog::STAKING, "%s : %s \n", __func__, getMiningStatus());
                continue;
            }
        }

        // Don't enter wallet staking checks until the node is operational
        // enough for PoS. This avoids touching the wallet's cs_main/cs_wallet
        // path while startup is still finishing.
        const bool mn_sync_done = node.mn_sync != nullptr && node.mn_sync->IsSynced();
        const size_t peer_count = node.connman ? node.connman->GetNodeCount(ConnectionDirection::Both) : 0;
        if (!mn_sync_done || peer_count == 0) {
            std::string status = ":";
            if (!mn_sync_done) {
                status += "<br>- masternode list isn't synced";
            }
            if (peer_count == 0) {
                status += "<br>- no connections with network";
            }
            SetMiningStatus(std::move(status));
            nLastCoinStakeSearchTime = 0;
            interrupt.sleep_for(std::chrono::seconds(hash_interval));
            LogPrint(BCLog::STAKING, "%s : node not ready mnsync=%d peers=%d\n",
                                  __func__,
                                  int(!mn_sync_done),
                                  int(peer_count == 0));
            continue;
        }

        if ((GetTime() - nMintableLastCheck > 60))
        {
            nMintableLastCheck = GetTime();
            fMintableCoins = pwallet->MintableCoins();
        }

        {
            CBlockIndex* pindexPrev = WITH_LOCK(cs_main, return Assert(node.chainman)->ActiveChain().Tip());

            if (!pindexPrev) {
                interrupt.sleep_for(std::chrono::seconds(1));
                SetMiningStatus(":<br>- no active blocks");
                LogPrint(BCLog::STAKING, "%s : %s \n", __func__, getMiningStatus());
                continue;
            }

            if (!IsPoSEnforcedHeight(pindexPrev->nHeight + 1) && !IsPoSV2EnforcedHeight(pindexPrev->nHeight + 1) && !pindexPrev->IsProofOfStake()) {
                interrupt.sleep_for(std::chrono::seconds(hash_interval));
                SetMiningStatus(":<br>- PoS is not enabled at height " + std::to_string(pindexPrev->nHeight + 1));
                LogPrint(BCLog::STAKING, "%s : %s \n", __func__, getMiningStatus());
                continue;
            }

            if (pindexPrev->nHeight + 1  < chainparams.GetConsensus().nForkHeight) {
                interrupt.sleep_for(std::chrono::seconds(hash_interval));
                SetMiningStatus(":<br>- PoSv2 is not enabled at height <b>" + std::to_string(pindexPrev->nHeight + 1) + "</b>");
                LogPrint(BCLog::STAKING, "%s : %s \n", __func__, getMiningStatus());
                continue;
            }
        }

        SetMiningStatus("");

        bool wallet_locked;
        bool below_reserve;
        {
            LOCK(pwallet->cs_wallet);
            wallet_locked = pwallet->IsLocked(true);
            below_reserve = pwallet->nReserveBalance >= wallet::GetBalance(*pwallet).m_mine_trusted;
        }
        if (wallet_locked || !fMintableCoins || below_reserve) {
            std::string status = ":";
            if (wallet_locked){
                status += "<br>- wallet is currently <b>locked</b>";
            }
            if (below_reserve){
                status += "<br>- your balance is less than the reserved amount";
            }
            if (!fMintableCoins){
                status += "<br>- no mature or available coins for staking";
            }
            SetMiningStatus(std::move(status));
            nLastCoinStakeSearchTime = 0;
            interrupt.sleep_for(std::chrono::seconds(hash_interval));
            LogPrint(BCLog::STAKING, "%s : wallet not ready locked=%d coins=%d reserve=%d\n",
                                  __func__,
                                  int(wallet_locked),
                                  int(!fMintableCoins),
                                  int(below_reserve));
            continue;
        }

        const int current_height = WITH_LOCK(cs_main, return Assert(node.chainman)->ActiveChain().Height());
        if (last_height == current_height)
        {
            if ((GetTime() - hash_interval) < nLastCoinStakeSearchTime.load())
            {
                continue;
            }
        } else {
            last_height = current_height;
            start_block_time = 0;
        }

        CBlockIndex* pindexPrev = WITH_LOCK(cs_main, return Assert(node.chainman)->ActiveChain().Tip());
        if (!pindexPrev) {
            interrupt.sleep_for(std::chrono::seconds(1));
            continue;
        }

        // For now keep the staking path simple: build the PoS candidate
        // directly and let CreateCoinStake()/CheckProof decide whether this
        // pass actually found a valid kernel.
        start_block_time = std::max<int64_t>(
            start_block_time,
            std::max<int64_t>(pindexPrev->GetMedianTimePast() + 1, TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime()))
        );

        //
        // Create new block
        //
        auto pblocktemplate = ba.CreateNewBlock(coinbaseScript, pwallet, start_block_time, true);
        nLastCoinStakeSearchTime = TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime());

        if (!pblocktemplate.get())
            continue;

        auto pblock = std::make_shared<CBlock>(std::move(pblocktemplate->block));

        BlockValidationState state;

        const bool proof_ok = WITH_LOCK(cs_main, return CheckProof(state, *pblock, Params().GetConsensus(), &Assert(node.chainman)->m_blockman, &Assert(node.chainman)->ActiveChain()));
        if (!proof_ok) {
            // Mimics limit in pos_kernel.cpp
            start_block_time = std::min<int64_t>(
                pblock->nTime + pwallet->nHashDrift,
                nLastCoinStakeSearchTime.load() + MAX_POS_BLOCK_AHEAD_TIME - MAX_POS_BLOCK_AHEAD_SAFETY_MARGIN
            );

            LogPrint(BCLog::STAKING, "%s : proof check failed: %s\n", __func__, state.ToString());
            continue;
        }

        //Stake miner main
        LogPrintf("PoSMiner : proof-of-stake block found %s \n", pblock->GetHash().ToString().c_str());

        bool fNewBlock = false;
        bool fAccepted = Assert(node.chainman)->ProcessNewBlock(pblock, true, &fNewBlock);
        auto hash = pblock->GetHash();

        if (fAccepted) {
            if (fNewBlock) {
                LogPrintf("PoSMiner : block is submitted %s\n", hash.ToString().c_str());
            } else {
                LogPrintf("PoSMiner : block duplicate %s\n", hash.ToString().c_str());
            }
        } else {
            LogPrintf("PoSMiner : block is rejected %s\n", hash.ToString().c_str());
        }
    }
}

#endif // ENABLE_WALLET

bool IsStakingActive() {
    return (TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime()) - nLastCoinStakeSearchTime.load()) < 60;
}

std::string getMiningStatus() {
    LOCK(g_mining_status_mutex);
    return miningStatus;
}

void SetThreadPriority(int nPriority)
{
#ifdef WIN32
    ::SetThreadPriority(::GetCurrentThread(), nPriority);
#else // WIN32
#ifdef PRIO_THREAD
    setpriority(PRIO_THREAD, 0, nPriority);
#else  // PRIO_THREAD
    setpriority(PRIO_PROCESS, 0, nPriority);
#endif // PRIO_THREAD
#endif // WIN32
}
} // namespace node
