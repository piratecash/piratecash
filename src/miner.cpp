// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2022 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos_kernel.h>
#include <wallet/wallet.h>

#ifdef __linux__
#include <sys/resource.h>
#endif // __linux__
#include <miner.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <script/signingprovider.h>
#include <timedata.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <util/validation.h>

#include <evo/specialtx.h>
#include <evo/cbtx.h>
#include <evo/simplifiedmns.h>
#include <governance/governance.h>
#include <llmq/blockprocessor.h>
#include <llmq/chainlocks.h>
#include <llmq/instantsend.h>
#include <llmq/utils.h>
#include <masternode/payments.h>
#include <masternode/sync.h>
#include <spork.h>

#include <algorithm>
#include <utility>
#include <boost/thread.hpp>

int64_t nLastCoinStakeSearchTime = 0;
namespace {
RecursiveMutex g_mining_status_mutex;
std::string miningStatus GUARDED_BY(g_mining_status_mutex);

void SetMiningStatus(std::string status)
{
    LOCK(g_mining_status_mutex);
    miningStatus = std::move(status);
}

} // namespace

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}

BlockAssembler::Options::Options() {
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
}

BlockAssembler::BlockAssembler(const CSporkManager& sporkManager, CGovernanceManager& governanceManager,
                               const llmq::CQuorumBlockProcessor& quorumBlockProcessor, llmq::CChainLocksHandler& clhandler,
                               llmq::CInstantSendManager& isman, CEvoDB& evoDb, const CTxMemPool& mempool, const CChainParams& params, const Options& options) :
      chainparams(params),
      m_mempool(mempool),
      spork_manager(sporkManager),
      governance_manager(governanceManager),
      quorum_block_processor(quorumBlockProcessor),
      m_clhandler(clhandler),
      m_isman(isman),
      m_evoDb(evoDb)
{
    blockMinFeeRate = options.blockMinFeeRate;
    // Limit size to between 1K and MaxBlockSize()-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MaxBlockSize(fDIP0001ActiveAtTip) - 1000), (unsigned int)options.nBlockMaxSize));
}

static BlockAssembler::Options DefaultOptions()
{
    // Block resource limits
    BlockAssembler::Options options;
    options.nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
    if (gArgs.IsArgSet("-blockmaxsize")) {
        options.nBlockMaxSize = gArgs.GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
    }
    if (gArgs.IsArgSet("-blockmintxfee")) {
        std::optional<CAmount> parsed = ParseMoney(gArgs.GetArg("-blockmintxfee", ""));
        options.blockMinFeeRate = CFeeRate{parsed.value_or(DEFAULT_BLOCK_MIN_TX_FEE)};
    } else {
        options.blockMinFeeRate = CFeeRate{DEFAULT_BLOCK_MIN_TX_FEE};
    }
    return options;
}

BlockAssembler::BlockAssembler(const CSporkManager& sporkManager, CGovernanceManager& governanceManager,
                               const llmq::CQuorumBlockProcessor& quorumBlockProcessor, llmq::CChainLocksHandler& clhandler,
                               llmq::CInstantSendManager& isman, CEvoDB& evoDb, const CTxMemPool& mempool, const CChainParams& params)
    : BlockAssembler(sporkManager, governanceManager, quorumBlockProcessor, clhandler, isman, evoDb, mempool, params, DefaultOptions()) {}

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

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, std::shared_ptr<CWallet> pwallet, int64_t block_time, bool isPos)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    CBlock* const pblock = pblocktemplate->block.get(); // pointer for convenience
    bool sign_block = false;

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    if (isPos) {
        // Keep the stake tx pinned at vtx[1] while the v19 block template
        // machinery appends commitments and mempool transactions after it.
        pblock->vtx.emplace_back();
        pblocktemplate->vTxFees.push_back(-1); // updated if stake is found
        pblocktemplate->vTxSigOps.push_back(-1); // updated if stake is found
    }

    LOCK2(cs_main, m_mempool.cs);

    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    bool fDIP0003Active_context = nHeight >= chainparams.GetConsensus().DIP0003Height;
    bool fDIP0008Active_context = nHeight >= chainparams.GetConsensus().DIP0008Height;

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus(), chainparams.BIP9CheckMasternodesUpgraded(), isPos);
    // Non-mainnet only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (Params().NetworkIDString() != CBaseChainParams::MAIN)
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = isPos ? block_time : GetAdjustedTime();
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

    if (fDIP0003Active_context) {
        for (const Consensus::LLMQParams& params : llmq::utils::GetEnabledQuorumParams(pindexPrev)) {
            std::vector<CTransactionRef> vqcTx;
            if (quorum_block_processor.GetMineableCommitmentsTx(params,
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
    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    int64_t nTime1 = GetTimeMicros();

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
    CAmount blockReward = nFees + GetBlockSubsidy(pindexPrev->nBits, pindexPrev->nHeight, Params().GetConsensus());

    // Compute regular coinbase transaction.
    coinbaseTx.vout[0].nValue = blockReward;

    if (!fDIP0003Active_context) {
        coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    } else {
        coinbaseTx.vin[0].scriptSig = CScript() << OP_RETURN;

        coinbaseTx.nVersion = 3;
        coinbaseTx.nType = TRANSACTION_COINBASE;

        CCbTx cbTx;

        if (fDIP0008Active_context) {
            cbTx.nVersion = 2;
        } else {
            cbTx.nVersion = 1;
        }

        cbTx.nHeight = nHeight;

        CValidationState state;
        if (!CalcCbTxMerkleRootMNList(*pblock, pindexPrev, cbTx.merkleRootMNList, state, ::ChainstateActive().CoinsTip())) {
            throw std::runtime_error(strprintf("%s: CalcCbTxMerkleRootMNList failed: %s", __func__, FormatStateMessage(state)));
        }
        if (fDIP0008Active_context) {
            if (!CalcCbTxMerkleRootQuorums(*pblock, pindexPrev, quorum_block_processor, cbTx.merkleRootQuorums, state)) {
                throw std::runtime_error(strprintf("%s: CalcCbTxMerkleRootQuorums failed: %s", __func__, FormatStateMessage(state)));
            }
        }

        SetTxPayload(coinbaseTx, cbTx);
    }

    // Update coinbase transaction with additional info about masternode and governance payments,
    // get some info back to pass to getblocktemplate
    FillBlockPayments(spork_manager, governance_manager, coinbaseTx, nHeight, blockReward, pblocktemplate->voutMasternodePayments, pblocktemplate->voutSuperblockPayments);

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

    if (isPos) {
        // The restored v19.3.0 LOCK2(cs_main, m_mempool.cs) must stay intact
        // for the dash template path, but PoS staking needs wallet access and
        // its own cs_main sections inside CreateCoinStake().
        REVERSE_LOCK(criticalblock2);
        REVERSE_LOCK(criticalblock1);

        assert(pwallet != nullptr);

        if (pwallet->IsLocked(true)) {
            error("%s: wallet is locked!", __func__);
            return std::move(pblocktemplate);
        }

        boost::this_thread::interruption_point();

        CMutableTransaction coinbaseTx(*(pblock->CoinBase()));
        bool fStakeFound = pwallet->CreateCoinStake(pindexPrev, *pblock, coinbaseTx);

        if (fStakeFound) {
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
            const CScript& pos_script = pblock->CoinBase()->vout[0].scriptPubKey;
            const SigningProvider* provider = pwallet->GetSigningProvider(pos_script);
            CKey key;
            if (!provider || !provider->GetKey(pblock->posPubKey.GetID(), key) ||
                !key.SignCompact(pblock->GetHash(), pblock->vchBlockSig)) {
                error("%s: failed to sign block", __func__);
                return nullptr;
            }
        }
    }

    if (isPos && pindexPrev != ::ChainActive().Tip()) {
        LogPrint(BCLog::STAKING, "%s: the network has already found another block", __func__);
        return nullptr;
    }

    CValidationState state;
    if (!TestBlockValidity(state, m_clhandler, m_evoDb, chainparams, *pblock, pindexPrev, false, false)) {
        if (isPos) {
            error("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state));
            return nullptr;
        }
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }
    int64_t nTime2 = GetTimeMicros();

    LogPrint(BCLog::BENCHMARK, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, unsigned int packageSigOps) const
{
    if (nBlockSize + packageSize >= nBlockMaxSize)
        return false;
    if (nBlockSigOps + packageSigOps >= MaxBlockSigOps(fDIP0001ActiveAtTip))
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - safe TXs in regard to ChainLocks
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package)
{
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
        if (!m_clhandler.IsTxSafeForMining(m_isman, it->GetTx().GetHash())) {
            return false;
        }
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblocktemplate->block->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOps.push_back(iter->GetSigOpCount());
    nBlockSize += iter->GetTxSize();
    ++nBlockTx;
    nBlockSigOps += iter->GetSigOpCount();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set &mapModifiedTx)
{
    AssertLockHeld(m_mempool.cs);

    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        m_mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCountWithAncestors -= it->GetSigOpCount();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    AssertLockHeld(m_mempool.cs);

    assert(it != m_mempool.mapTx.end());
    return mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it);
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
void BlockAssembler::addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated)
{
    AssertLockHeld(m_mempool.cs);

    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = m_mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != m_mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != m_mempool.mapTx.get<ancestor_score>().end() &&
            SkipMapTxEntry(m_mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == m_mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = m_mempool.mapTx.project<0>(mi);
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

        if (packageFees < blockMinFeeRate.GetFee(packageSize)) {
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

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockSize > nBlockMaxSize - 1000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        m_mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

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

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, sortedEntries);

        for (size_t i=0; i<sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
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


void PoSMiner(std::shared_ptr<CWallet> pwallet, CConnman& connman, CEvoDB& evo_db, CTxMemPool& mempool, CThreadInterrupt &interrupt)
{
    LogPrintf("PoSMiner started\n");
    util::ThreadRename("piratecash-miner");
    SetThreadPriority(0);

    assert(sporkManager);
    assert(governance);
    assert(llmq::quorumBlockProcessor);
    assert(llmq::chainLocksHandler);
    assert(llmq::quorumInstantSendManager);
    BlockAssembler ba{*sporkManager, *governance, *llmq::quorumBlockProcessor, *llmq::chainLocksHandler, *llmq::quorumInstantSendManager, evo_db, mempool, Params()};
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
            CBlockIndex* pindexPrev = ::ChainActive().Tip();

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
        if (!::masternodeSync->IsSynced() ||
            (connman.GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)) {
            std::string status = ":";
            if (!::masternodeSync->IsSynced()) {
                status += "<br>- masternode list isn't synced";
            }
            if ((connman.GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)) {
                status += "<br>- no connections with network";
            }
            SetMiningStatus(std::move(status));
            nLastCoinStakeSearchTime = 0;
            interrupt.sleep_for(std::chrono::seconds(hash_interval));
            LogPrint(BCLog::STAKING, "%s : node not ready mnsync=%d peers=%d\n",
                                  __func__,
                                  int(!::masternodeSync->IsSynced()),
                                  int(connman.GetNodeCount(CConnman::CONNECTIONS_ALL) == 0));
            continue;
        }

        if ((GetTime() - nMintableLastCheck > 60))
        {
            nMintableLastCheck = GetTime();
            fMintableCoins = pwallet->MintableCoins();
        }

        {
            CBlockIndex* pindexPrev = ::ChainActive().Tip();
            
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

        if (pwallet->IsLocked(true) ||
            !fMintableCoins ||
            (pwallet->nReserveBalance >= pwallet->GetBalance().m_mine_trusted)) {
            std::string status = ":";
            if (pwallet->IsLocked(true)){
                status += "<br>- wallet is currently <b>locked</b>";
            }
            if (pwallet->nReserveBalance >= pwallet->GetBalance().m_mine_trusted){
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
                                  int(pwallet->IsLocked(true)),
                                  int(!fMintableCoins),
                                  int(pwallet->nReserveBalance >= pwallet->GetBalance().m_mine_trusted));
            continue;
        }

        if (last_height == ::ChainActive().Height())
        {
            if ((GetTime() - hash_interval) < nLastCoinStakeSearchTime)
            {
                continue;
            }
        } else {
            last_height = ::ChainActive().Height();
            start_block_time = 0;
        }

        CBlockIndex* pindexPrev = ::ChainActive().Tip();
        if (!pindexPrev) {
            interrupt.sleep_for(std::chrono::seconds(1));
            continue;
        }

        // For now keep the staking path simple: build the PoS candidate
        // directly and let CreateCoinStake()/CheckProof decide whether this
        // pass actually found a valid kernel.
        start_block_time = std::max<int64_t>(
            start_block_time,
            std::max<int64_t>(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime())
        );

        //
        // Create new block
        //
        auto pblocktemplate = ba.CreateNewBlock(coinbaseScript, pwallet, start_block_time, true);
        nLastCoinStakeSearchTime = GetAdjustedTime();

        if (!pblocktemplate.get())
            continue;

        auto pblock = pblocktemplate->block;

        CValidationState state;

        if (!CheckProof(state, *pblock, Params().GetConsensus())) {
            // Mimics limit in pos_kernel.cpp
            start_block_time = std::min<int64_t>(
                pblock->nTime + pwallet->nHashDrift,
                nLastCoinStakeSearchTime + MAX_POS_BLOCK_AHEAD_TIME - MAX_POS_BLOCK_AHEAD_SAFETY_MARGIN
            );

            continue;
        }

        //Stake miner main
        LogPrintf("PoSMiner : proof-of-stake block found %s \n", pblock->GetHash().ToString().c_str());

        bool fNewBlock = false;
        bool fAccepted = g_chainman.ProcessNewBlock(Params(), pblock, true, &fNewBlock);
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

bool IsStakingActive() {
    return (GetAdjustedTime() - nLastCoinStakeSearchTime) < 60;
}

std::string getMiningStatus() {
    LOCK(g_mining_status_mutex);
    return miningStatus;
}

void SetThreadPriority(int nPriority)
{
#ifdef WIN32
    SetThreadPriority(GetCurrentThread(), nPriority);
#else // WIN32
#ifdef PRIO_THREAD
    setpriority(PRIO_THREAD, 0, nPriority);
#else  // PRIO_THREAD
    setpriority(PRIO_PROCESS, 0, nPriority);
#endif // PRIO_THREAD
#endif // WIN32
}
