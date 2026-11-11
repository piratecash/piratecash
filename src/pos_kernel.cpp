// Distributed under the MIT software license, see the accompanying
/* @flow */
// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>

#include <crypto/common.h>
#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/validation.h>
#include <index/txindex.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <node/transaction.h>
#include <policy/policy.h>
#include <pos_header_presync.h>
#include <pos_kernel.h>
#include <primitives/block.h>
#include <script/interpreter.h>
#include <timedata.h>
#include <undo.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>

#include <map>
#include <tuple>
#include <utility>
#include <vector>

using namespace std;

// NOTE: This should be > 10 minutes
// MODIFIER_INTERVAL: time to elapse before new modifier is computed
static constexpr unsigned int MODIFIER_INTERVAL = 60;

// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
static constexpr int MODIFIER_INTERVAL_RATIO = 3;

static constexpr int MODIFIER_INTERVAL_SECTIONS_MAX = 64;

// Get the last stake modifier and its generation time from a given block
static bool GetLastStakeModifier(const CBlockIndex* pindex, uint32_t& nStakeModifier, int64_t& nModifierTime)
{
    if (!pindex) {
        LogError("GetLastStakeModifier: null pindex\n");
        return false;
    }
    while (pindex && pindex->pprev && !pindex->IsGeneratedStakeModifier())
        pindex = pindex->pprev;
    nStakeModifier = pindex->nStakeModifier();
    nModifierTime = pindex->GetBlockTime();
    return true;
}

template <int Section>
struct StakeModifierSelectionIntervalSection {
    static_assert(Section >= 0 && Section < MODIFIER_INTERVAL_SECTIONS_MAX, "Invalid nSection");
    static constexpr auto MI_1 = MODIFIER_INTERVAL_SECTIONS_MAX - 1;
    static constexpr int value = (MODIFIER_INTERVAL * MI_1) / (MI_1 + ((MI_1 - Section) * (MODIFIER_INTERVAL_RATIO - 1)));
};

using IntervalSectionArray = int64_t[64];

template<int Section>
struct StakeModifierSelectionIntervalHelper {
    static constexpr int64_t value = (
        StakeModifierSelectionIntervalHelper<Section-1>::value
        + StakeModifierSelectionIntervalSection<Section>::value
    );
    static void fill(IntervalSectionArray &arr) {
        arr[Section] = StakeModifierSelectionIntervalSection<Section>::value;
        StakeModifierSelectionIntervalHelper<Section-1>::fill(arr);
    }
};
template<>
struct StakeModifierSelectionIntervalHelper<0> {
    static constexpr int64_t value = StakeModifierSelectionIntervalSection<0>::value;
    static void fill(IntervalSectionArray &arr) {
        arr[0] = StakeModifierSelectionIntervalSection<0>::value;
    }
};

class StakeModifierIntervalSelector {
private:
    IntervalSectionArray array;

public:
    StakeModifierIntervalSelector() {
        StakeModifierSelectionIntervalHelper<MODIFIER_INTERVAL_SECTIONS_MAX - 1>::fill(array);
    }

    int64_t Get(int section) {
        assert(section >= 0 && section < 64);
        return array[section];
    }
};
StakeModifierIntervalSelector g_StakeModifierIntervalSelector;

// Get stake modifier selection interval (in seconds)
static constexpr int64_t GetStakeModifierSelectionInterval()
{
    return StakeModifierSelectionIntervalHelper<MODIFIER_INTERVAL_SECTIONS_MAX - 1>::value;
}

using StakeModifierCandidate = tuple<int64_t, uint256, const CBlockIndex*>;

// select a block from the candidate blocks in vSortedByTimestamp, excluding
// already selected blocks in vSelectedBlocks, and with timestamp up to
// nSelectionIntervalStop.
static bool SelectBlockFromCandidates(
    vector<StakeModifierCandidate>& vSortedByTimestamp,
    map<uint256, const CBlockIndex*>& mapSelectedBlocks,
    int64_t nSelectionIntervalStop,
    uint64_t nStakeModifierPrev,
    const CBlockIndex** pindexSelected)
{
    bool fSelected = false;
    arith_uint256 hashBest{0};
    *pindexSelected = nullptr;

    for (auto iter = vSortedByTimestamp.begin(); ; ++iter) {
        if (iter == vSortedByTimestamp.end()) {
            vSortedByTimestamp.clear();
            break;
        }

        const CBlockIndex* pindex = get<2>(*iter);
        if (!pindex) {
            LogError("SelectBlockFromCandidates: missing candidate block index\n");
            return false;
        }
        if (fSelected && pindex->GetBlockTime() > nSelectionIntervalStop) {
            // No point to re-consider the blocks
            vSortedByTimestamp.erase(vSortedByTimestamp.begin(), iter+1);
            break;
        }

        if (mapSelectedBlocks.count(pindex->GetBlockHash()) > 0)
            continue;

        // compute the selection hash by hashing an input that is unique to that block
        uint256 hashProof = pindex->GetBlockHash();

        CDataStream ss(SER_GETHASH, 0);
        ss << hashProof << nStakeModifierPrev;
        arith_uint256 hashSelection = UintToArith256(Hash(ss));

        // the selection hash is divided by 2**32 so that proof-of-stake block
        // is always favored over proof-of-work block. this is to preserve
        // the energy efficiency property
        if (pindex->IsProofOfStake())
            hashSelection >>= 32;

        if (fSelected && hashSelection < hashBest) {
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*)pindex;
        } else if (!fSelected) {
            iter = vSortedByTimestamp.begin();
            fSelected = true;
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*)pindex;
        }
    }

    LogPrint(BCLog::STAKING, "%s: selection hash=%s\n", __func__, hashBest.ToString().c_str());

    return fSelected;
}

// Stake Modifier (hash modifier of proof-of-stake):
// The purpose of stake modifier is to prevent a txout (coin) owner from
// computing future proof-of-stake generated by this txout at the time
// of transaction confirmation. To meet kernel protocol, the txout
// must hash with a future stake modifier to generate the proof.
// Stake modifier consists of bits each of which is contributed from a
// selected block of a given block group in the past.
// The selection of a block is based on a hash of the block's proof-hash and
// the previous stake modifier.
// Stake modifier is recomputed at a fixed time interval instead of every
// block. This is to make it difficult for an attacker to gain control of
// additional bits in the stake modifier, even after generating a chain of
// blocks.
bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint32_t& nStakeModifier)
{
    nStakeModifier = 0;
    if (!pindexPrev) {
        return true; // genesis block's modifier is 0
    }
    if (pindexPrev->nHeight == 0) {
        //Give a stake modifier to the first block
        nStakeModifier = static_cast<uint32_t>(0x1234567887654321ULL);
        return true;
    }

    // First find current stake modifier and its generation block time
    // if it's not old enough, return the same stake modifier
    int64_t nModifierTime = 0;
    if (!GetLastStakeModifier(pindexPrev, nStakeModifier, nModifierTime)) {
        LogError("ComputeNextStakeModifier: unable to get last modifier\n");
        return false;
    }

    LogPrint(BCLog::STAKING, "%s: prev modifier=%08x time=%d\n",
             __func__, nStakeModifier, nModifierTime);

    LogPrint(BCLog::STAKING, "%s: block %d modifier time last vs prev %llx~%llx >= %llx~%llx\n",
             __func__, pindexPrev->nHeight,
             nModifierTime, (nModifierTime / MODIFIER_INTERVAL),
             pindexPrev->GetBlockTime(), (pindexPrev->GetBlockTime() / MODIFIER_INTERVAL));

    if (nModifierTime / MODIFIER_INTERVAL >= pindexPrev->GetBlockTime() / MODIFIER_INTERVAL) {
        return true;
    }

    // Sort candidate blocks by timestamp
    vector<StakeModifierCandidate> vSortedByTimestamp;
    vSortedByTimestamp.reserve(MODIFIER_INTERVAL_SECTIONS_MAX);
    int64_t nSelectionInterval = GetStakeModifierSelectionInterval();
    int64_t nSelectionIntervalStart = (pindexPrev->GetBlockTime() / MODIFIER_INTERVAL) * MODIFIER_INTERVAL - nSelectionInterval;
    const CBlockIndex* pindex = pindexPrev;

    while (pindex && pindex->GetBlockTime() >= nSelectionIntervalStart) {
        vSortedByTimestamp.push_back(make_tuple(pindex->GetBlockTime(), pindex->GetBlockHash(), pindex));
        pindex = pindex->pprev;
    }

    int nHeightFirstCandidate = pindex ? (pindex->nHeight + 1) : 0;
    reverse(vSortedByTimestamp.begin(), vSortedByTimestamp.end()); // it should improve sorting time
    sort(vSortedByTimestamp.begin(), vSortedByTimestamp.end());

    // Select 64 blocks from candidate blocks to generate stake modifier
    uint64_t nStakeModifierNew = 0;
    int64_t nSelectionIntervalStop = nSelectionIntervalStart;
    map<uint256, const CBlockIndex*> mapSelectedBlocks;
    for (int nRound = 0; nRound < MODIFIER_INTERVAL_SECTIONS_MAX; nRound++) {
        // add an interval section to the current selection round
        nSelectionIntervalStop += g_StakeModifierIntervalSelector.Get(nRound);

        // select a block from the candidates of current round
        if (SelectBlockFromCandidates(vSortedByTimestamp, mapSelectedBlocks, nSelectionIntervalStop, nStakeModifier, &pindex)) {
            // write the entropy bit of the selected block
            nStakeModifierNew |= (((uint64_t)pindex->GetStakeEntropyBit()) << nRound);
        } else {
            LogPrint(BCLog::STAKING, "WARN: unable to select candidate block for stake modifier at round %d - left zero\n", nRound);
            break;
        }

        // add the selected block from candidates to selected list
        mapSelectedBlocks.insert(make_pair(pindex->GetBlockHash(), pindex));
        LogPrint(BCLog::STAKING, "%s: selected round %d stop=%s height=%d bit=%d\n",
                 __func__, nRound, nSelectionIntervalStop,
                 pindex->nHeight, pindex->GetStakeEntropyBit());
    }

    // Print selection map for visualization of the selected blocks
    if (LogAcceptDebug(BCLog::STAKING)) {
        string strSelectionMap = "";
        // '-' indicates proof-of-work blocks not selected
        strSelectionMap.insert(0, pindexPrev->nHeight - nHeightFirstCandidate + 1, '-');
        pindex = pindexPrev;
        while (pindex && pindex->nHeight >= nHeightFirstCandidate) {
            // '=' indicates proof-of-stake blocks not selected
            if (pindex->IsProofOfStake())
                strSelectionMap.replace(pindex->nHeight - nHeightFirstCandidate, 1, "=");
            pindex = pindex->pprev;
        }
        for (const auto &item : mapSelectedBlocks) {
            // 'S' indicates selected proof-of-stake blocks
            // 'W' indicates selected proof-of-work blocks
            strSelectionMap.replace(item.second->nHeight - nHeightFirstCandidate, 1, item.second->IsProofOfStake() ? "S" : "W");
        }
        LogPrintf("%s: selection height [%d, %d] map %s\n", __func__, nHeightFirstCandidate, pindexPrev->nHeight, strSelectionMap.c_str());
    }

    LogPrint(BCLog::STAKING, "%s: new modifier=%08x prevblktime=%d\n",
             __func__, nStakeModifierNew, pindexPrev->GetBlockTime());

    nStakeModifier = nStakeModifierNew;
    return true;
}

bool ComputeNextStakeModifierV2(const uint32_t blockTime, const CBlockIndex* pindexPrev, uint32_t& nStakeModifier)
{
    nStakeModifier = 0;

    if (!pindexPrev) {
        return false;
    }

    /**
     * This is actually bound to stake maturity period of 1 hour, but testnet uses only 3 minutes.
     *
     * The previous block stake is used as difficult to mine entropy as it is expected to be outside
     * of the interval of prediction.
     *
     * The general approach is to use the oldest block in such interval as source of random entropy,
     * but block time can be manipulated.
     *
     * Therefore, the middle block time is used as another source of entropy as extra precaution.
     *
     * The problem becomes as follows:
     * - stakes needs to be mined in before maturity period
     * - target stakes depend on block hashes inside the maturity period
     * - there are around 60 blocks = 60 stakes needed
     * - oldest block hash is not predictable in the future
     * - all this problem solving needs to be sorted in quite short period of time as the main chain
     *   progresses rapidly.
     * - current hash power demand is in Thz
     */
    const uint32_t MODIFIER_INTERVAL = 3600;
    const uint32_t timeMid = blockTime - (MODIFIER_INTERVAL / 2);
    const uint32_t timeOld = blockTime - (MODIFIER_INTERVAL - MAX_POS_BLOCK_AHEAD_TIME);

    const CBlockIndex* pmiddle = pindexPrev;
    for (; pmiddle->pprev && (pmiddle->pprev->nTime > timeMid); pmiddle = pmiddle->pprev);

    const CBlockIndex* poldest = pmiddle;
    for (; poldest->pprev && (poldest->pprev->nTime > timeOld); poldest = poldest->pprev);

    auto prevout = pindexPrev->StakeInput();

    // From more recent to more oldest
    CDataStream ss(SER_GETHASH, 0);
    ss << prevout.hash << prevout.n;
    ss << pmiddle->GetBlockHash();
    ss << poldest->GetBlockHash();

    // The first 64 bits
    const uint256 hashStakeModifier = Hash(ss);
    nStakeModifier = ReadLE64(hashStakeModifier.begin());

    LogPrint(BCLog::STAKING, "%s: new modifier=%llx time=%llu, prevblk=%s\n",
             __func__, nStakeModifier,
             blockTime,
             pindexPrev->GetBlockHash().ToString().c_str());

    return true;
}

bool CachedNextStakeModifierV2(const uint32_t blockTime, const CBlockIndex* pindexPrev, uint32_t& nStakeModifier)
{
    // This can be done more "correct" way...
    static uint256 last_parent;
    static std::map<uint32_t, uint64_t> sm_cache;

    auto parent = pindexPrev->GetBlockHash();

    // Reset cache
    if (last_parent != parent) {
        sm_cache.clear();
        last_parent = parent;
    }

    // Fast path
    auto cached = sm_cache.find(blockTime);

    if (cached != sm_cache.end()) {
        nStakeModifier = cached->second;

        LogPrint(BCLog::STAKING, "%s: cached modifier=%llx time=%llu, prevblk=%s\n",
                __func__, nStakeModifier,
                blockTime,
                pindexPrev->GetBlockHash().ToString().c_str());
        return true;
    }

    // Slow path
    if (!ComputeNextStakeModifierV2(blockTime, pindexPrev, nStakeModifier)) {
        return false;
    }

    // Cache
    sm_cache[blockTime] = nStakeModifier;

    return true;
}

uint256 stakeHash(unsigned int nTimeTx, CDataStream ss, unsigned int prevoutIndex, uint256 prevoutHash, unsigned int nTimeBlockFrom)
{
    //Pivx will hash in the transaction hash and the index number in order to make sure each hash is unique
    ss << nTimeBlockFrom << prevoutIndex << prevoutHash << nTimeTx;
    return Hash(ss);
}

//instead of looping outside and reinitializing variables many times, we will give a nTimeTx and also search interval so that we can do all the hashing here
bool CheckStakeKernelHash(
    CBlockHeader &current,
    const CBlockIndex &blockPrev,
    const CBlockIndex &blockFrom,
    const CTransaction txPrev,
    const COutPoint prevout,
    unsigned int nHashDrift,
    bool fCheck,
    uint256& hashProofOfStake,
    bool fPrintProofOfStake
) {
    // Guard against an out-of-range stake output index before dereferencing
    // txPrev.vout[prevout.n] (prevout.n is attacker-controlled via the header).
    if (prevout.n >= txPrev.vout.size()) {
        LogError("CheckStakeKernelHash() : stake prevout index %u out of range (vout size %u)\n", prevout.n, (unsigned int)txPrev.vout.size());
        return false;
    }

    return CheckStakeKernelHash(current, blockPrev, blockFrom, txPrev.vout[prevout.n], prevout,
                                nHashDrift, fCheck, hashProofOfStake, fPrintProofOfStake);
}

bool CheckStakeKernelHash(
    CBlockHeader &current,
    const CBlockIndex &blockPrev,
    const CBlockIndex &blockFrom,
    const CTxOut& stake_out,
    const COutPoint prevout,
    unsigned int nHashDrift,
    bool fCheck,
    uint256& hashProofOfStake,
    bool fPrintProofOfStake
) {
    // Legacy way of parameter passing
    unsigned int nBits = current.nBits;
    unsigned int& nTimeTx = current.nTime;
    uint32_t &nStakeModifier = current.nStakeModifier();
    //

    //assign new variables to make it easier to read
    CAmount nValueIn = stake_out.nValue;
    unsigned int nTimeBlockFrom = blockFrom.GetBlockTime();
    auto min_age = Params().MinStakeAge();

    if (nValueIn < MIN_STAKE_AMOUNT) {
        LogError("CheckStakeKernelHash() : stake value is too small %d < %d\n", nValueIn, MIN_STAKE_AMOUNT);
        return false;
    }

    if (nTimeTx < nTimeBlockFrom) { // Transaction timestamp violation
        LogError("CheckStakeKernelHash() : nTime violation\n");
        return false;
    }

    if (nTimeBlockFrom + min_age > nTimeTx) // Min age requirement
    {
        // During generation, some stakes may be not year ready
        if (fCheck) {
            LogError("%s : min age violation - nTimeBlockFrom=%d nStakeMinAge=%d nTimeTx=%d\n",
                  __func__, nTimeBlockFrom, min_age, nTimeTx);
        }
        return false;
    }

    //grab difficulty
    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);
    arith_uint256 bnTarget = (arith_uint256(nValueIn) / 100) * bnTargetPerCoinDay;

    if (bnTarget < bnTargetPerCoinDay) {
        LogPrint(BCLog::STAKING, "PoS target overflow %s amount %d < common %s, using ~0\n",
                  bnTarget.GetHex().c_str(),
                  nValueIn,
                  bnTargetPerCoinDay.GetHex().c_str());
        bnTarget = ~arith_uint256(0);
    }

    //grab stake modifier
    //-------------------
    uint32_t nRequiredStakeModifier = 0;

    // This is a six month later fix of the problem stated in the note below.
    if (current.IsProofOfStakeV2()) {
        if (fCheck && !ComputeNextStakeModifierV2(nTimeTx, &blockPrev, nRequiredStakeModifier)) {
            LogPrintf("CheckStakeKernelHash(): failed to get kernel stake modifier V2 \n");
            return false;
        }
        // pass
    } else // PoS v1

    // NOTE: this must be calculated based on previous-to-tip, but not previous-to-stake block!
    if (!ComputeNextStakeModifier(&blockFrom, nRequiredStakeModifier)) {
        LogPrintf("CheckStakeKernelHash(): failed to get kernel stake modifier \n");
        return false;
    }

    if (fCheck) {
        if (nStakeModifier != nRequiredStakeModifier) {
            LogError(
                "%s : nStakeModifier mismatch at %d %llx != %llx\n",
                __func__, blockFrom.nHeight,
                nStakeModifier, nRequiredStakeModifier );
            return false;
        }
    } else {
        nStakeModifier = nRequiredStakeModifier;
    }

    //create data stream once instead of repeating it in the loop
    CDataStream ss(SER_GETHASH, 0);
    ss << nStakeModifier;
    hashProofOfStake = stakeHash(nTimeTx, ss, prevout.n, prevout.hash, nTimeBlockFrom);

    // if wallet is simply checking to make sure a hash is valid
    //-------------------
    if (fCheck) {
        return UintToArith256(hashProofOfStake) < bnTarget;
    }

    // search
    //-------------------
    auto min_time = nTimeTx;
    auto max_time = std::min<int64_t>(
                min_time + nHashDrift,
                TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime()) + MAX_POS_BLOCK_AHEAD_TIME - MAX_POS_BLOCK_AHEAD_SAFETY_MARGIN);
    LogPrint(BCLog::STAKING, "%s: looking for solution in range %lld .. %lld (%lld) \n",
             __func__, min_time, max_time, (max_time - min_time));

    for (auto try_time = min_time; try_time < max_time; ++try_time)
    {
        if (current.IsProofOfStakeV2()) {
            if (!CachedNextStakeModifierV2(try_time, &blockPrev, nStakeModifier)) {
                LogPrintf("CheckStakeKernelHash(): failed to get kernel stake modifier V2 \n");
                return false;
            }
        }
        ss = CDataStream(SER_GETHASH, 0);
        ss << nStakeModifier;

        //hash this iteration
        hashProofOfStake = stakeHash(try_time, ss, prevout.n, prevout.hash, nTimeBlockFrom);

        // if stake hash does not meet the target then continue to next iteration
        if (UintToArith256(hashProofOfStake) >= bnTarget) {
            continue;
        }

        nTimeTx = try_time;

        if (fPrintProofOfStake) {
            LogPrintf("CheckStakeKernelHash() : using modifier %s at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
                nStakeModifier,
                blockFrom.nHeight,
                FormatISO8601DateTime(blockFrom.nTime).c_str(),
                blockFrom.nHeight,
                FormatISO8601DateTime(blockFrom.GetBlockTime()).c_str());
            LogPrintf("CheckStakeKernelHash() : pass protocol=%s modifier=%s nTimeBlockFrom=%u prevoutHash=%s nTimeTxPrev=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
                "0.3",
                boost::lexical_cast<std::string>(nStakeModifier).c_str(),
                nTimeBlockFrom, prevout.hash.ToString().c_str(), nTimeBlockFrom, prevout.n, try_time,
                hashProofOfStake.ToString().c_str());
        }
        return true;
    }

    return false;
}

bool HasHeaderOnlyStakeReuse(const CBlockIndex* pindex_prev,
                            const CBlockIndex* pindex_fork,
                            const COutPoint& prevout)
{
    // Walk from pindex_prev down to the fork point, advancing by each node's
    // own parent (pindex_walk->pprev). The leading null check guards the case
    // where the fork point is genesis (pprev == nullptr). Only the
    // still-unvalidated PoS header tail is inspected; the walk terminates as
    // soon as it reaches the fork point, a fully-validated block, or a
    // non-PoS block.
    for (auto pindex_walk = pindex_prev;
         pindex_walk &&
            (pindex_walk != pindex_fork) &&
            pindex_walk->IsProofOfStake() &&
            !pindex_walk->IsValid(BLOCK_VALID_SCRIPTS);
        pindex_walk = pindex_walk->pprev
    ) {
        if (prevout == pindex_walk->StakeInput()) {
            return true;
        }
    }
    return false;
}

// Stake output of a header that extends the active tip, read from the UTXO set
// so that nodes with and without -txindex accept the same headers.
static bool LookupStakeInTipCoins(BlockValidationState& state, const COutPoint& prevout,
                                  const CBlockIndex& header_prev, const CChain& active_chain,
                                  CCoinsViewCache& coins_tip, CTxOut& stake_out,
                                  bool& stake_is_coinbase, const CBlockIndex*& pindex_tx)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    // Do not let headers grow the coins cache.
    const bool was_cached{coins_tip.HaveCoinInCache(prevout)};
    Coin coin;
    const bool found{coins_tip.GetCoin(prevout, coin)};
    if (!was_cached) coins_tip.Uncache(prevout);

    if (found) pindex_tx = active_chain[coin.nHeight];
    if (!found || pindex_tx == nullptr) {
        if (&header_prev == active_chain.Tip()) {
            return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-unkown-stake");
        }
        // The stake may be created by a block between the tip and this header.
        return state.Error("tmp-bad-unkown-stake");
    }

    stake_out = coin.out;
    stake_is_coinbase = coin.IsCoinBase();
    return true;
}

namespace {
enum class StakeLookup {
    FOUND,
    MISSING, //!< the coin did not exist at the fork point
    UNKNOWN, //!< this node cannot tell
};

//! Coins spent by one block of the active chain, taken from its undo data.
struct SpentCoins {
    uint256 block_hash;
    std::vector<std::pair<COutPoint, Coin>> coins;
};
std::map<int, SpentCoins> g_spent_coins GUARDED_BY(::cs_main);
//! Blocks spending more than this are not looked into; their branch is then left unjudged.
constexpr size_t MAX_SPENT_COINS_PER_BLOCK{20000};

const SpentCoins* ReadSpentCoins(const CBlockIndex& block_index, const Consensus::Params& consensus, int keep_from_height)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    g_spent_coins.erase(g_spent_coins.begin(), g_spent_coins.lower_bound(keep_from_height));
    const auto it{g_spent_coins.find(block_index.nHeight)};
    if (it != g_spent_coins.end() && it->second.block_hash == block_index.GetBlockHash()) return &it->second;

    if (!(block_index.nStatus & BLOCK_HAVE_DATA) || !(block_index.nStatus & BLOCK_HAVE_UNDO)) return nullptr;
    CBlock block;
    CBlockUndo undo;
    if (!node::ReadBlockFromDisk(block, &block_index, consensus) || !node::UndoReadFromDisk(undo, &block_index)) return nullptr;
    if (undo.vtxundo.size() + 1 != block.vtx.size()) return nullptr;
    size_t input_count{0};
    for (const CTxUndo& tx_undo : undo.vtxundo) input_count += tx_undo.vprevout.size();
    if (input_count > MAX_SPENT_COINS_PER_BLOCK) return nullptr;

    SpentCoins spent{block_index.GetBlockHash(), {}};
    for (size_t i = 1; i < block.vtx.size(); ++i) {
        const auto& inputs{block.vtx[i]->vin};
        const auto& coins{undo.vtxundo[i - 1].vprevout};
        if (inputs.size() != coins.size()) return nullptr;
        for (size_t j = 0; j < inputs.size(); ++j) {
            spent.coins.emplace_back(inputs[j].prevout, coins[j]);
        }
    }
    return &(g_spent_coins[block_index.nHeight] = std::move(spent));
}

// Stake output of a header on a branch that leaves the active chain below the tip,
// found without a transaction index: either still unspent at the tip, or spent by
// one of our blocks above the fork point (the competing branch may stake it too).
StakeLookup LookupStakeBelowFork(const COutPoint& prevout, const CBlockIndex& fork, const CChain& active_chain,
                                 CCoinsViewCache& coins_tip, const Consensus::Params& consensus, CTxOut& stake_out,
                                 bool& stake_is_coinbase, const CBlockIndex*& pindex_tx)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    // Bounded by what a pruned node keeps and by how far back a stake can matter.
    const int max_depth{std::min<int>(pos_presync::VerifiabilityWindow(Params()), Params().MinBlocksToKeep())};
    const CBlockIndex* tip{active_chain.Tip()};
    if (tip->nHeight - fork.nHeight > max_depth) return StakeLookup::UNKNOWN;

    const auto found{[&](const Coin& coin) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
        pindex_tx = active_chain[coin.nHeight];
        if (pindex_tx == nullptr) return StakeLookup::UNKNOWN;
        stake_out = coin.out;
        stake_is_coinbase = coin.IsCoinBase();
        return StakeLookup::FOUND;
    }};

    const bool was_cached{coins_tip.HaveCoinInCache(prevout)};
    Coin unspent;
    const bool have_unspent{coins_tip.GetCoin(prevout, unspent)};
    if (!was_cached) coins_tip.Uncache(prevout);
    if (have_unspent) return found(unspent);

    for (const CBlockIndex* walk = tip; walk != &fork; walk = walk->pprev) {
        const SpentCoins* spent{ReadSpentCoins(*walk, consensus, tip->nHeight - max_depth)};
        if (spent == nullptr) return StakeLookup::UNKNOWN;
        for (const auto& [outpoint, coin] : spent->coins) {
            if (outpoint == prevout) return found(coin);
        }
    }
    return StakeLookup::MISSING;
}
} // namespace

// Check kernel hash target and coinstake signature
bool CheckProofOfStake(BlockValidationState& state, const CBlockHeader& header, uint256& hashProofOfStake, const Consensus::Params& consensus, const CTxMemPool* mempool, const node::BlockManager* blockman, const CChain* active_chain, CCoinsViewCache* coins_tip)
{
    if (header.posBlockSig.empty()) {
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-pos-sig", "missing PoS signature");
    }

    COutPoint prevout = header.StakeInput();

    if (blockman == nullptr || active_chain == nullptr) {
        return state.Error("tmp-missing-chain-context");
    }

    CTxOut stake_out;
    bool stake_is_coinbase{false};
    const CBlockIndex* pindex_tx = nullptr;
    const CBlockIndex* pindex_prev = nullptr;

    bool stake_from_tip_coins{false};
    bool stake_below_fork_unknown{false};
    if (coins_tip != nullptr) {
        LOCK(::cs_main);
        const CBlockIndex* pindex_header_prev = blockman->LookupBlockIndex(header.hashPrevBlock);
        const CBlockIndex* tip = active_chain->Tip();
        const CBlockIndex* fork = pindex_header_prev != nullptr && tip != nullptr ? active_chain->FindFork(pindex_header_prev) : nullptr;
        if (fork != nullptr && fork == tip) {
            if (!LookupStakeInTipCoins(state, prevout, *pindex_header_prev, *active_chain, *coins_tip,
                                       stake_out, stake_is_coinbase, pindex_tx)) {
                return false;
            }
            stake_from_tip_coins = true;
        } else if (fork != nullptr) {
            switch (LookupStakeBelowFork(prevout, *fork, *active_chain, *coins_tip, consensus,
                                         stake_out, stake_is_coinbase, pindex_tx)) {
            case StakeLookup::FOUND:
                stake_from_tip_coins = true;
                break;
            case StakeLookup::MISSING:
                if (pindex_header_prev == fork) {
                    return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-unkown-stake");
                }
                // The stake may be created by a block of the branch that we do not have.
                return state.Error("tmp-bad-unkown-stake");
            case StakeLookup::UNKNOWN:
                stake_below_fork_unknown = true;
                break;
            }
        }
    }

    // Side branches and callers without a coins view: find the previous transaction in database
    uint256 txinHashBlock;
    CTransactionRef txinPrevRef;

    if (!stake_from_tip_coins) {
        txinPrevRef = node::GetTransaction(/* block_index */ nullptr, mempool, prevout.hash, consensus, txinHashBlock);
    }
    LOCK(::cs_main);
    if (!stake_from_tip_coins && !txinPrevRef) {
        const CBlockIndex* pindex_header_prev = blockman->LookupBlockIndex(header.hashPrevBlock);

        // Without a transaction index a branch too deep to look into cannot be judged.
        const bool cannot_tell{stake_below_fork_unknown && (!g_txindex || !g_txindex->GetSummary().synced)};
        if (!cannot_tell && pindex_header_prev != nullptr && active_chain->Contains(pindex_header_prev)) {
            return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-unkown-stake");
        } else {
            // The previous block is missing, so the stake input may still be valid.
            return state.Error("tmp-bad-unkown-stake");
        }
    }

    // Check tx input block is known
    if (!stake_from_tip_coins) {
        const CBlockIndex* pindex_stake = blockman->LookupBlockIndex(txinHashBlock);

        if (pindex_stake != nullptr && active_chain->Contains(pindex_stake)) {
            pindex_tx = pindex_stake;
        } else {
            const CBlockIndex* pindex_header_prev = blockman->LookupBlockIndex(header.hashPrevBlock);

            if (pindex_header_prev != nullptr && active_chain->Contains(pindex_header_prev)) {
                return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-stake-mempool",
                                     "stake from mempool");
            } else {
                // The stake input's block may still be missing from the active chain.
                return state.Error("tmp-bad-stake-mempool");
            }
        }

        // Guard against an out-of-range stake output index before dereferencing
        // vout[prevout.n]. prevout.n comes from the attacker-controlled header
        // (posStakeN); a crafted header referencing a real transaction hash but
        // a bogus index would otherwise cause an out-of-bounds read / crash.
        if (prevout.n >= txinPrevRef->vout.size()) {
            return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-pos-input",
                                 "stake prevout index out of range");
        }

        stake_out = txinPrevRef->vout[prevout.n];
        stake_is_coinbase = txinPrevRef->IsCoinBase();
    }

    // Header-only chain specific validation
    {
        const CBlockIndex* pindex_header_prev = blockman->LookupBlockIndex(header.hashPrevBlock);

        // It must never happen as it's part of header validation.
        if (pindex_header_prev == nullptr) {
            return state.Invalid(BlockValidationResult::BLOCK_MISSING_PREV, "bad-prev-header",
                                "previous PoS header is not known");
        }

        pindex_prev = pindex_header_prev;
        const auto pindex_fork = active_chain->FindFork(pindex_prev);

        // Just in case, it must never happen.
        if (!pindex_fork) {
            return state.Invalid(BlockValidationResult::BLOCK_INVALID_PREV, "bad-fork-point",
                                "the fork point is not found");
        }

        // Check if UTXO is beyond possible fork point
        if (pindex_fork->nHeight < pindex_tx->nHeight) {
            if (pindex_prev != pindex_fork) {
                // Blocks of the branch that we do not have may confirm the same transaction.
                return state.Error("tmp-bad-stake-after-fork");
            }
            return state.Invalid(BlockValidationResult::BLOCK_INVALID_PREV, "bad-stake-after-fork",
                                "rogue fork tries to use UTXO from the current chain");
        }

        // Reject a rogue fork that reuses the same stake UTXO anywhere in its
        // still-unvalidated header tail (see HasHeaderOnlyStakeReuse).
        if (HasHeaderOnlyStakeReuse(pindex_prev, pindex_fork, prevout)) {
            return state.Invalid(BlockValidationResult::BLOCK_INVALID_PREV, "bad-header-double-spent",
                             "rogue fork tries use the same UTXO twice");
        }
    }

    // NOTE: stake age check is part of CheckStakeKernelHash()

    // Check stake maturity (double checking with other functionality for DoS mitigation).
    //
    // PirateCash: measure maturity against the height of the new block being
    // validated (pindex_prev->nHeight + 1), not the height of our own local
    // tip. During headers-first IBD the peer can deliver a header for a block
    // far ahead of our chain (e.g. pindex_prev = 1_076_000 while our Tip is
    // only at 526_274). Using ChainActive().Tip() here mistakenly rejects
    // such legitimate headers whenever the stake-utxo's containing block
    // happens to land within COINBASE_MATURITY blocks of our local tip even
    // though it is already mature at the actual new-block height.
    if (stake_is_coinbase &&
        ((pindex_prev->nHeight - pindex_tx->nHeight) <= COINBASE_MATURITY)
    ) {
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-stake-coinbase-maturity",
                            "coinbase maturity mismatch for stake");
    }

    // Extract stake public key ID and verify block signature
    {
        TxoutType whichType;
        std::vector<std::vector<unsigned char>> vSolutions;
        CKeyID key_id;
        const auto &spk = stake_out.scriptPubKey;

        whichType = Solver(spk, vSolutions);

        if (whichType == TxoutType::NONSTANDARD) {
            return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-pos-input",
                             "invalid Stake Input script");
        }

        if (whichType == TxoutType::PUBKEYHASH) // pay to address type
        {
            key_id = CKeyID(uint160(vSolutions[0]));
        }
        else if (whichType == TxoutType::PUBKEY) // pay to public key
        {
            key_id = CPubKey(vSolutions[0]).GetID();
        }
        else
        {
            return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-pos-input",
                             "unsupported Stake Input script");
        }

        if (!header.CheckBlockSignature(key_id)) {
            return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-blk-sig",
                             "invalid block signature");
        }
    }

    unsigned int nInterval = 0;
    CBlockHeader rwheader = header; // const_cast could be used, but just safety

    bool is_valid = CheckStakeKernelHash(
            rwheader,
            *pindex_prev,
            *pindex_tx,
            stake_out,
            prevout,
            nInterval,
            true,
            hashProofOfStake,
            false);

    if (!is_valid) {
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-pos-proof");
    }

    return true;
}
