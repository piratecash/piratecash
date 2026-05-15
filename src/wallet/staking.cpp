// Copyright (c) 2026 The PirateCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>

#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <evo/dmn_types.h>
#include <logging.h>
#include <pos_kernel.h>
#include <primitives/block.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <shutdown.h>
#include <sync.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <util/time.h>
#include <wallet/coincontrol.h>
#include <wallet/receive.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/spend.h>

#include <algorithm>
#include <map>

namespace wallet {

bool CWallet::SelectStakeCoins(StakeCandidates& setCoins, CAmount nTargetAmount) const
{
    CAmount nAmountSelected = 0;
    LOCK(cs_wallet);

    CCoinControl coin_control;
    const std::vector<COutput> available_coins{AvailableCoins(*this, &coin_control, CFeeRate(0), MIN_STAKE_AMOUNT).all()};

    for (const COutput& output : available_coins) {
        if (ShutdownRequested()) break;
        if (!output.spendable || !output.safe) continue;
        if (output.txout.nValue < MIN_STAKE_AMOUNT) continue;
        if (inputStakeProtect && dmn_types::IsCollateralAmount(output.txout.nValue)) continue;

        const CWalletTx* wtx = GetWalletTx(output.outpoint.hash);
        if (!wtx) continue;
        if (output.outpoint.n >= wtx->tx->vout.size()) continue;

        const int min_depth = wtx->IsCoinBase() ? COINBASE_MATURITY : 10;
        if (output.depth < min_depth) continue;

        setCoins.emplace_back(output.txout.nValue, wtx, output.outpoint.n);
        nAmountSelected += output.txout.nValue;
        if (nAmountSelected >= nTargetAmount) break;
    }

    return nAmountSelected > 0;
}

bool CWallet::CreateCoinStake(const CBlockIndex* pindex_prev, CBlock& curr_block, CMutableTransaction& coinbaseTx)
{
    const CAmount nBalance = GetBalance(*this).m_mine_trusted;

    if (m_args.IsArgSet("-reservebalance")) {
        const auto parsed = ParseMoney(m_args.GetArg("-reservebalance", ""));
        if (!parsed) return error("%s: invalid reserve balance amount", __func__);
        nReserveBalance = *parsed;
    }

    if (nBalance <= nReserveBalance) {
        return error("%s: balance is less than required to reserve", __func__);
    }

    const CAmount nTargetAmount = nBalance - nReserveBalance;
    if (setStakeCoins.empty() || GetTime() - nLastStakeSetUpdate > nStakeSetUpdateTime) {
        setStakeCoins.clear();
        if (!SelectStakeCoins(setStakeCoins, nTargetAmount)) {
            LogPrint(BCLog::STAKING, "%s: no inputs eligible for staking\n", __func__);
            return false;
        }
        std::sort(setStakeCoins.begin(), setStakeCoins.end(), [](const auto& lhs, const auto& rhs) {
            return std::get<0>(lhs) > std::get<0>(rhs);
        });
        nLastStakeSetUpdate = GetTime();
    }

    LogPrint(BCLog::STAKING, "%s: found %u possible stake inputs\n", __func__, setStakeCoins.size());

    for (auto iter = setStakeCoins.begin(); iter != setStakeCoins.end(); ++iter) {
        if (ShutdownRequested()) break;

        const CWalletTx* pWalletTxIn = std::get<1>(*iter);
        const COutPoint prevoutStake{pWalletTxIn->GetHash(), std::get<2>(*iter)};
        LogPrint(BCLog::STAKING, "%s: trying tx=%s n=%u\n", __func__, prevoutStake.hash.ToString(), prevoutStake.n);

        std::map<COutPoint, Coin> coins{{prevoutStake, Coin{}}};
        chain().findCoins(coins);
        if (coins[prevoutStake].IsSpent()) {
            LogPrint(BCLog::STAKING, "%s: skipping stale stake input tx=%s n=%u (already spent)\n",
                     __func__, prevoutStake.hash.ToString(), prevoutStake.n);
            continue;
        }

        const auto* conf = pWalletTxIn->state<TxStateConfirmed>();
        const CBlockIndex* pcoin_index = nullptr;
        if (conf && conf->confirmed_block_height <= pindex_prev->nHeight) {
            pcoin_index = pindex_prev->GetAncestor(conf->confirmed_block_height);
        }
        if (!conf || !pcoin_index || pcoin_index->GetBlockHash() != conf->confirmed_block_hash) {
            LogPrintf("%s: failed to find block index for %s\n", __func__, conf ? conf->confirmed_block_hash.ToString() : pWalletTxIn->GetHash().ToString());
            continue;
        }

        uint256 hashProofOfStake;
        const bool fKernelFound = CheckStakeKernelHash(
            curr_block,
            *pindex_prev,
            *pcoin_index,
            *pWalletTxIn->tx,
            prevoutStake,
            nHashDrift,
            false,
            hashProofOfStake,
            true);

        if (!fKernelFound) continue;

        if (curr_block.nTime <= pindex_prev->GetMedianTimePast()) {
            LogPrint(BCLog::STAKING, "%s: kernel found, but it is too far in the past\n", __func__);
            continue;
        }

        LogPrint(BCLog::STAKING, "%s: kernel found\n", __func__);

        std::vector<std::vector<unsigned char>> vSolutions;
        const CTxOut& tx_in = pWalletTxIn->tx->vout[prevoutStake.n];
        const CScript& scriptPubKeyKernel = tx_in.scriptPubKey;
        const TxoutType whichType = Solver(scriptPubKeyKernel, vSolutions);

        if (whichType == TxoutType::NONSTANDARD) {
            LogPrint(BCLog::STAKING, "%s: failed to parse kernel\n", __func__);
            continue;
        }

        if (whichType == TxoutType::PUBKEYHASH) {
            std::unique_ptr<SigningProvider> provider = GetSolvingProvider(scriptPubKeyKernel);
            if (!provider || !provider->GetPubKey(CKeyID(uint160(vSolutions[0])), curr_block.posPubKey)) {
                LogPrint(BCLog::STAKING, "%s: failed to get key for kernel type=%d\n", __func__, static_cast<int>(whichType));
                continue;
            }
        } else if (whichType == TxoutType::PUBKEY) {
            curr_block.posPubKey = CPubKey(vSolutions[0]);
        } else {
            LogPrint(BCLog::STAKING, "%s: no support for kernel type=%d\n", __func__, static_cast<int>(whichType));
            continue;
        }

        assert(curr_block.posPubKey.IsValid());
        const CScript scriptPubKeyOut = GetScriptForDestination(PKHash(curr_block.posPubKey));
        coinbaseTx.vout[0].scriptPubKey = scriptPubKeyOut;

        CMutableTransaction stakeTx;
        stakeTx.vin.emplace_back(prevoutStake);
        stakeTx.vout.emplace_back(0, CScript());
        stakeTx.vout.emplace_back(tx_in.nValue, scriptPubKeyOut);

        CAmount reward = stakeTx.vout[1].nValue;
        const CAmount split_threshold = nStakeSplitThreshold * COIN;
        const CAmount autocombine_target = split_threshold * 2 - 1;

        std::vector<CScript> vin_scripts{scriptPubKeyKernel};
        std::vector<CAmount> vin_values{tx_in.nValue};

        if (fAutocombine != AUTOCOMBINE_DISABLE) {
            std::vector<COutPoint> ac_candidates;
            {
                std::vector<CompactTallyItem> vecTallyRet = SelectCoinsGroupedByAddresses();
                if (fAutocombine == AUTOCOMBINE_SAME) {
                    const CTxDestination reqdest{PKHash(curr_block.posPubKey)};
                    for (auto titer = vecTallyRet.begin(); titer != vecTallyRet.end(); ++titer) {
                        if (titer->txdest == reqdest) {
                            ac_candidates.swap(titer->outpoints);
                            break;
                        }
                    }
                } else if (fAutocombine == AUTOCOMBINE_ANY) {
                    for (const auto& tally : vecTallyRet) {
                        ac_candidates.insert(ac_candidates.end(), tally.outpoints.begin(), tally.outpoints.end());
                    }
                }
            }

            auto ac_iter = ac_candidates.begin();
            const auto min_age = Params().MinStakeAge();
            const auto ac_len = ac_candidates.size();
            int inputs_included = 0;
            for (size_t i = 0; i < ac_len && inputs_included < nStakeMaxSplit; ++i) {
                CTxOut ac_txout;
                for (; ac_iter != ac_candidates.end(); ++ac_iter) {
                    if (*ac_iter == prevoutStake) continue;
                    LOCK(cs_wallet);
                    const CWalletTx* wtx = GetWalletTx(ac_iter->hash);
                    if (!wtx || ac_iter->n >= wtx->tx->vout.size()) continue;

                    ac_txout = wtx->tx->vout[ac_iter->n];
                    const int64_t nTimeInput = static_cast<int64_t>(wtx->GetTxTime()) + static_cast<int64_t>(min_age);
                    if (nTimeInput > static_cast<int64_t>(GetTime())) continue;
                    if (ac_txout.nValue < MIN_STAKE_AMOUNT) break;
                    if ((reward + ac_txout.nValue) < autocombine_target) break;
                }

                if (ac_iter == ac_candidates.end()) break;

                ++inputs_included;
                stakeTx.vin.emplace_back(*ac_iter);
                vin_scripts.emplace_back(ac_txout.scriptPubKey);
                vin_values.emplace_back(ac_txout.nValue);
                reward += ac_txout.nValue;
                LogPrint(BCLog::STAKING, "%s: auto-combining tx=%s n=%u amount=%lld total=%lld\n", __func__,
                         ac_iter->hash.ToString(), ac_iter->n, ac_txout.nValue, reward);
                ++ac_iter;
            }
        }

        for (int i = 0; i < nStakeMaxSplit && reward > split_threshold * 2; ++i) {
            stakeTx.vout.emplace_back(split_threshold, scriptPubKeyOut);
            reward -= split_threshold;
        }

        stakeTx.vout[1].nValue = reward;

        LogPrint(BCLog::STAKING, "%s: split stake vout into %u pieces\n", __func__, stakeTx.vout.size());

        LegacyScriptPubKeyMan* spk_man = GetLegacyScriptPubKeyMan();
        if (!spk_man) return error("%s: legacy script pub key manager missing", __func__);

        for (size_t i = 0; i < vin_scripts.size(); ++i) {
            if (!SignSignature(*spk_man, vin_scripts[i], stakeTx, i, vin_values[i], SIGHASH_ALL)) {
                return error("%s: failed to sign coinstake", __func__);
            }
        }

        curr_block.posStakeHash = prevoutStake.hash;
        curr_block.posStakeN = prevoutStake.n;
        curr_block.Stake() = MakeTransactionRef(std::move(stakeTx));

        LogPrint(BCLog::STAKING, "%s: added kernel type=%d\n", __func__, static_cast<int>(whichType));

        nLastStakeSetUpdate = 0;
        return true;
    }

    LogPrint(BCLog::STAKING, "%s: no stakes found\n", __func__);
    return false;
}

} // namespace wallet
