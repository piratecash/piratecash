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
#include <pow.h>
#include <pos_header_presync.h>
#include <pos_kernel.h>
#include <primitives/block.h>
#include <script/interpreter.h>
#include <timedata.h>
#include <undo.h>
#include <util/hasher.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>

#include <algorithm>
#include <map>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

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

int64_t MaxPosBlockAheadTime(int height, const Consensus::Params& params)
{
    return params.IsPosV3Height(height) ? params.PosV3FutureTolerance() : MAX_POS_BLOCK_AHEAD_TIME;
}

int64_t LatestPosV3BlockTime(NodeClock::time_point now, const Consensus::Params& params)
{
    return TicksSinceEpoch<std::chrono::seconds>(now) + params.PosV3FutureTolerance();
}

int64_t NextPosV3SlotTime(int64_t time, const Consensus::Params& params)
{
    const int64_t slot{params.nPosV3SlotSeconds};
    return ((time + slot - 1) / slot) * slot;
}

bool CheckPosV3Header(const CBlockHeader& block, const CBlockIndex& prev, const Consensus::Params& params,
                      NodeClock::time_point now, BlockValidationState& state)
{
    if (block.nNonce != 0) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "bad-nonce-v3", "nNonce of a PoS v3 block must be zero");
    }
    if (block.GetBlockTime() % params.nPosV3SlotSeconds != 0) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "time-not-slot",
                             strprintf("block timestamp %d is not a multiple of %d", block.GetBlockTime(), params.nPosV3SlotSeconds));
    }
    if (block.GetBlockTime() <= prev.GetBlockTime()) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "time-not-after-parent",
                             strprintf("block timestamp %d is not after its parent's %d", block.GetBlockTime(), prev.GetBlockTime()));
    }
    const int64_t latest{LatestPosV3BlockTime(now, params)};
    if (block.GetBlockTime() > latest) {
        return state.Invalid(BlockValidationResult::BLOCK_TIME_FUTURE, "time-too-new",
                             strprintf("block timestamp too far in the future %d %d", block.GetBlockTime(), latest));
    }
    return true;
}

namespace {
//! Default size of the v3 modifier memo (about 100 bytes an entry).
constexpr size_t DEFAULT_STAKE_MODIFIER_V3_CACHE{1 << 16};
//! Modifiers at heights that are multiples of this are kept for good, which bounds every walk.
constexpr int STAKE_MODIFIER_V3_ANCHOR_INTERVAL{1024};
struct StakeModifierV3Entry {
    uint256 modifier;
    int height;
    //! The GetStakeModifierV3() call that added the entry.
    uint64_t call;
};
GlobalMutex g_modifier_v3_mutex;
//! Keyed by block hash: the hash fixes the parent and the stake input, so the modifier as well.
std::unordered_map<uint256, StakeModifierV3Entry, BlockHasher> g_modifier_v3 GUARDED_BY(g_modifier_v3_mutex);
//! Never evicted, not counted against the limit: one entry per STAKE_MODIFIER_V3_ANCHOR_INTERVAL blocks of each branch seen.
//! So it is bounded by the size of the block index / STAKE_MODIFIER_V3_ANCHOR_INTERVAL.
std::unordered_map<uint256, uint256, BlockHasher> g_modifier_v3_anchors GUARDED_BY(g_modifier_v3_mutex);
size_t g_modifier_v3_limit GUARDED_BY(g_modifier_v3_mutex){DEFAULT_STAKE_MODIFIER_V3_CACHE};
//! Highest height in the memo.
int g_modifier_v3_top GUARDED_BY(g_modifier_v3_mutex){-1};
//! The activation height the memo was filled for (unit tests run several chain configurations).
int g_modifier_v3_height GUARDED_BY(g_modifier_v3_mutex){-1};
uint64_t g_modifier_v3_calls GUARDED_BY(g_modifier_v3_mutex){0};
uint64_t g_modifier_v3_hashes GUARDED_BY(g_modifier_v3_mutex){0};
uint64_t g_modifier_v3_eviction_passes GUARDED_BY(g_modifier_v3_mutex){0};

uint256 NextStakeModifierV3(const CBlockIndex& block, const uint256& parent_modifier)
{
    // A PoS v1 block's stake input is neither serialized nor hashed, so peers may not know it.
    const COutPoint stake_input{block.IsProofOfStakeV2() ? COutPoint{block.posStakeHash, block.posStakeN} : COutPoint{uint256{}, 0}};
    CDataStream ss(SER_GETHASH, 0);
    ss << stake_input.hash << stake_input.n << parent_modifier;
    return Hash(ss);
}

void ClearStakeModifiersV3() EXCLUSIVE_LOCKS_REQUIRED(g_modifier_v3_mutex)
{
    g_modifier_v3.clear();
    g_modifier_v3_anchors.clear();
    g_modifier_v3_top = -1;
}

//! Over the limit, drop the entries deepest below the highest one, except those the current call
//! added. Work at the top of the chain only needs the parent's entry, so it never walks further
//! than one block; each eviction frees about half of the memo, which keeps its cost constant per entry added.
void EvictStakeModifiersV3() EXCLUSIVE_LOCKS_REQUIRED(g_modifier_v3_mutex)
{
    const int top{g_modifier_v3_top};
    for (int window = static_cast<int>(g_modifier_v3_limit / 2); g_modifier_v3.size() > g_modifier_v3_limit;) {
        const int floor{top - window};
        // The lowest entry this pass keeps that a smaller window could drop.
        int lowest{top};
        std::erase_if(g_modifier_v3, [floor, &lowest](const auto& entry) EXCLUSIVE_LOCKS_REQUIRED(g_modifier_v3_mutex) {
            if (entry.second.call == g_modifier_v3_calls) return false;
            if (entry.second.height < floor) return true;
            lowest = std::min(lowest, entry.second.height);
            return false;
        });
        ++g_modifier_v3_eviction_passes;
        if (lowest >= top) break;
        // Skip the windows that would drop nothing; window 0 drops everything below the top.
        do {
            window /= 2;
        } while (window > 0 && top - window <= lowest);
    }
}
} // namespace

std::optional<uint256> GetStakeModifierV3(const CBlockIndex& block, const Consensus::Params& params)
{
    const int seed_height{std::max(0, params.PosV3Height - 1 - POS_V3_MODIFIER_SEED_DEPTH)};
    if (block.nHeight < seed_height) return std::nullopt;

    LOCK(g_modifier_v3_mutex);
    if (g_modifier_v3_height != params.PosV3Height) {
        ClearStakeModifiersV3();
        g_modifier_v3_height = params.PosV3Height;
    }
    ++g_modifier_v3_calls;

    // Walk down to the seed or to a known modifier, then hash back up.
    std::vector<const CBlockIndex*> pending;
    uint256 modifier;
    for (const CBlockIndex* walk = &block;; walk = walk->pprev) {
        if (walk == nullptr || walk->phashBlock == nullptr) return std::nullopt;
        if (walk->nHeight == seed_height) {
            modifier.SetNull();
            break;
        }
        if (const auto it{g_modifier_v3.find(walk->GetBlockHash())}; it != g_modifier_v3.end()) {
            modifier = it->second.modifier;
            break;
        }
        if (const auto it{g_modifier_v3_anchors.find(walk->GetBlockHash())}; it != g_modifier_v3_anchors.end()) {
            modifier = it->second;
            break;
        }
        pending.push_back(walk);
    }

    // pending[0] is `block`. The current call's entries are not evicted, so keep only the top `limit`
    // of a long walk (after a restart, the first one goes down to the seed).
    for (size_t i = pending.size(); i-- > 0;) {
        const CBlockIndex& walk{*pending[i]};
        modifier = NextStakeModifierV3(walk, modifier);
        ++g_modifier_v3_hashes;
        const int height{walk.nHeight};
        if (height % STAKE_MODIFIER_V3_ANCHOR_INTERVAL == 0) {
            g_modifier_v3_anchors.emplace(walk.GetBlockHash(), modifier);
        } else if (i < g_modifier_v3_limit) {
            g_modifier_v3.emplace(walk.GetBlockHash(), StakeModifierV3Entry{modifier, height, g_modifier_v3_calls});
        }
        g_modifier_v3_top = std::max(g_modifier_v3_top, height);
    }
    EvictStakeModifiersV3();
    return modifier;
}

void ClearStakeModifierV3Cache()
{
    LOCK(g_modifier_v3_mutex);
    ClearStakeModifiersV3();
}

void SetStakeModifierV3CacheLimitForTest(std::optional<size_t> limit)
{
    LOCK(g_modifier_v3_mutex);
    g_modifier_v3_limit = limit.value_or(DEFAULT_STAKE_MODIFIER_V3_CACHE);
    EvictStakeModifiersV3();
}

size_t StakeModifierV3CacheSize()
{
    LOCK(g_modifier_v3_mutex);
    return g_modifier_v3.size();
}

uint64_t StakeModifierV3HashCountForTest()
{
    LOCK(g_modifier_v3_mutex);
    return g_modifier_v3_hashes;
}

uint64_t StakeModifierV3EvictionPassesForTest()
{
    LOCK(g_modifier_v3_mutex);
    return g_modifier_v3_eviction_passes;
}

arith_uint256 GetKernelTarget(CAmount value, uint32_t nBits, bool saturate)
{
    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);
    const arith_uint256 coins{arith_uint256(value) / 100};

    if (saturate) {
        if (bnTargetPerCoinDay != 0 && coins > ~arith_uint256(0) / bnTargetPerCoinDay) return ~arith_uint256(0);
        return coins * bnTargetPerCoinDay;
    }

    arith_uint256 bnTarget = coins * bnTargetPerCoinDay;
    if (bnTarget < bnTargetPerCoinDay) {
        LogPrint(BCLog::STAKING, "PoS target overflow %s amount %d < common %s, using ~0\n",
                  bnTarget.GetHex().c_str(),
                  value,
                  bnTargetPerCoinDay.GetHex().c_str());
        bnTarget = ~arith_uint256(0);
    }
    return bnTarget;
}

uint256 StakeHashV3(const uint256& modifier, uint32_t nTimeBlockFrom, const COutPoint& prevout, uint32_t nTime)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << modifier << nTimeBlockFrom << prevout.n << prevout.hash << nTime;
    return Hash(ss);
}

uint256 stakeHash(unsigned int nTimeTx, CDataStream ss, unsigned int prevoutIndex, uint256 prevoutHash, unsigned int nTimeBlockFrom)
{
    //Pivx will hash in the transaction hash and the index number in order to make sure each hash is unique
    ss << nTimeBlockFrom << prevoutIndex << prevoutHash << nTimeTx;
    return Hash(ss);
}

int64_t WalletLatestPosV3Slot(NodeClock::time_point now, const Consensus::Params& params)
{
    const int64_t slot{params.nPosV3SlotSeconds};
    const int64_t current_slot{TicksSinceEpoch<std::chrono::seconds>(now) / slot * slot};
    return std::min(current_slot + POSV3_WALLET_SLOT_LOOKAHEAD * slot, LatestPosV3BlockTime(now, params));
}

int64_t FirstPosV3Slot(const CBlockIndex& prev, const Consensus::Params& params)
{
    return NextPosV3SlotTime(std::max(prev.GetBlockTime(), prev.GetMedianTimePast()) + 1, params);
}

int64_t LastPosV3SlotOfPass(int64_t first, int64_t last, const Consensus::Params& params)
{
    return std::min(last, first + (POSV3_WALLET_MAX_SLOTS_PER_PASS - 1) * params.nPosV3SlotSeconds);
}

void PosV3SlotTracker::SetTip(const CBlockIndex& tip)
{
    if (tip.GetBlockHash() == m_tip) return;
    m_tip = tip.GetBlockHash();
    m_next_slot = 0;
    m_excluded.clear();
}

std::optional<PosV3Search> PosV3SlotTracker::NextRange(const CBlockIndex& tip, NodeClock::time_point now, const Consensus::Params& params)
{
    SetTip(tip);
    const int64_t first{std::max(m_next_slot, FirstPosV3Slot(tip, params))};
    const int64_t last{LastPosV3SlotOfPass(first, WalletLatestPosV3Slot(now, params), params)};
    if (first > last) return std::nullopt;
    return PosV3Search{.prev = m_tip, .first = first, .last = last, .excluded = m_excluded};
}

void PosV3SlotTracker::Searched(const CBlockIndex& tip, const PosV3Search& search, const Consensus::Params& params)
{
    if (tip.GetBlockHash() != m_tip || search.prev != m_tip) return;
    if (search.no_kernel) {
        m_next_slot = std::max(m_next_slot, search.last + params.nPosV3SlotSeconds);
    } else if (search.used) {
        m_excluded.insert(*search.used);
    }
}

void PosV3SlotTracker::Exclude(const CBlockIndex& tip, const PosV3Stake& stake)
{
    if (tip.GetBlockHash() == m_tip) m_excluded.insert(stake);
}

std::vector<PosV3Slot> PosV3Slots(const CBlockIndex& prev, int64_t first, int64_t last, const Consensus::Params& params)
{
    std::vector<PosV3Slot> slots;
    const int64_t slot{params.nPosV3SlotSeconds};
    first = std::max(NextPosV3SlotTime(first, params), FirstPosV3Slot(prev, params));
    last = LastPosV3SlotOfPass(first, last, params);
    CBlockHeader candidate;
    for (int64_t time = first; time <= last; time += slot) {
        candidate.nTime = static_cast<uint32_t>(time);
        slots.push_back({candidate.nTime, GetNextWorkRequired(&prev, &candidate, params)});
    }
    return slots;
}

std::optional<size_t> FindStakeKernelV3(CBlockHeader& current, const CBlockIndex& prev, const CBlockIndex& blockFrom, const CTxOut& stake_out,
                                        const COutPoint& prevout, const std::vector<PosV3Slot>& slots, const uint256& modifier,
                                        int64_t min_stake_age, const Consensus::Params& params, uint256& hashProofOfStake, bool fPrint)
{
    if (!params.IsPosV3Height(prev.nHeight + 1) || stake_out.nValue < MIN_STAKE_AMOUNT) return std::nullopt;

    const uint32_t time_from{static_cast<uint32_t>(blockFrom.GetBlockTime())};
    const int64_t mature_at{blockFrom.GetBlockTime() + min_stake_age};
    std::optional<uint32_t> target_bits;
    arith_uint256 target;
    for (size_t i = 0; i < slots.size(); ++i) {
        const PosV3Slot& slot{slots[i]};
        if (slot.time < mature_at) continue;
        if (target_bits != slot.bits) {
            target_bits = slot.bits;
            target = GetKernelTarget(stake_out.nValue, slot.bits, /*saturate=*/true);
        }
        const uint256 proof{StakeHashV3(modifier, time_from, prevout, slot.time)};
        if (UintToArith256(proof) >= target) continue;

        hashProofOfStake = proof;
        current.nTime = slot.time;
        current.nBits = slot.bits;
        current.nNonce = 0;
        if (fPrint) {
            LogPrint(BCLog::STAKING, "%s: pass protocol=v3 modifier=%s nTimeBlockFrom=%u prevoutHash=%s nPrevout=%u nTime=%u nBits=%08x hashProof=%s\n",
                      __func__, modifier.ToString(), time_from, prevout.hash.ToString(), prevout.n, slot.time, slot.bits,
                      proof.ToString());
        }
        return i;
    }
    return std::nullopt;
}

std::vector<std::pair<size_t, size_t>> RankStakeKernelsV3(const std::vector<PosV3StakeCandidate>& candidates, const CBlockIndex& prev,
                                                          const std::vector<PosV3Slot>& slots, const uint256& modifier,
                                                          int64_t min_stake_age, const Consensus::Params& params,
                                                          const std::set<PosV3Stake>& excluded)
{
    std::vector<std::pair<size_t, size_t>> ranked;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const PosV3StakeCandidate& candidate{candidates[i]};
        // Search on past an excluded slot.
        for (size_t start = 0; start < slots.size();) {
            const std::vector<PosV3Slot> rest(slots.begin() + start, slots.end());
            CBlockHeader scratch;
            uint256 proof;
            const auto slot{FindStakeKernelV3(scratch, prev, *candidate.block_from, candidate.stake_out, candidate.prevout,
                                              rest, modifier, min_stake_age, params, proof)};
            if (!slot) break;
            if (!excluded.count({candidate.prevout, rest[*slot].time})) {
                ranked.emplace_back(i, start + *slot);
                break;
            }
            start += *slot + 1;
        }
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });
    return ranked;
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

    const Consensus::Params& consensus = Params().GetConsensus();
    const bool pos_v3 = current.IsProofOfStakeV2() && consensus.IsPosV3Height(blockPrev.nHeight + 1);

    //grab difficulty
    const arith_uint256 bnTarget = GetKernelTarget(nValueIn, nBits, /*saturate=*/pos_v3);

    if (pos_v3) {
        // Searches at v3 heights go through FindStakeKernelV3().
        if (!Assume(fCheck)) return false;
        const auto modifier{GetStakeModifierV3(blockPrev, consensus)};
        if (!modifier) {
            LogPrintf("CheckStakeKernelHash(): failed to get kernel stake modifier V3\n");
            return false;
        }
        hashProofOfStake = StakeHashV3(*modifier, nTimeBlockFrom, prevout, nTimeTx);
        return UintToArith256(hashProofOfStake) < bnTarget;
    }

    // A kernel needs the stake input in the header: only the PoSv2 format has one.
    if (!current.IsProofOfStakeV2()) {
        LogError("CheckStakeKernelHash() : not a PoSv2 header\n");
        return false;
    }

    //grab stake modifier
    //-------------------
    uint32_t nRequiredStakeModifier = 0;
    if (fCheck && !ComputeNextStakeModifierV2(nTimeTx, &blockPrev, nRequiredStakeModifier)) {
        LogPrintf("CheckStakeKernelHash(): failed to get kernel stake modifier V2 \n");
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
        if (!CachedNextStakeModifierV2(try_time, &blockPrev, nStakeModifier)) {
            LogPrintf("CheckStakeKernelHash(): failed to get kernel stake modifier V2 \n");
            return false;
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
    //! False when the block could not be looked into; kept so that it is not read from disk again.
    bool usable{false};
    std::unordered_map<COutPoint, Coin, SaltedOutpointHasher> coins;
};
std::map<int, SpentCoins> g_spent_coins GUARDED_BY(::cs_main);
size_t g_spent_coins_total GUARDED_BY(::cs_main){0};
//! Blocks spending more than this are not looked into; their branch is then left unjudged.
constexpr size_t MAX_SPENT_COINS_PER_BLOCK{20000};
//! Same, for all remembered blocks together.
constexpr size_t MAX_SPENT_COINS_TOTAL{200000};
//! While the tip is this block, blocks at or below the height do not fit: they are not read again.
struct SpentCoinsFull {
    uint256 tip_hash;
    int height{-1};
};
SpentCoinsFull g_spent_coins_full GUARDED_BY(::cs_main);

bool LoadSpentCoins(const CBlockIndex& block_index, const Consensus::Params& consensus, SpentCoins& spent)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    if (!(block_index.nStatus & BLOCK_HAVE_DATA) || !(block_index.nStatus & BLOCK_HAVE_UNDO)) return false;
    CBlock block;
    CBlockUndo undo;
    if (!node::ReadBlockFromDisk(block, &block_index, consensus) || !node::UndoReadFromDisk(undo, &block_index)) return false;
    if (undo.vtxundo.size() + 1 != block.vtx.size()) return false;
    size_t input_count{0};
    for (const CTxUndo& tx_undo : undo.vtxundo) input_count += tx_undo.vprevout.size();
    if (input_count > MAX_SPENT_COINS_PER_BLOCK) return false;

    for (size_t i = 1; i < block.vtx.size(); ++i) {
        const auto& inputs{block.vtx[i]->vin};
        const auto& coins{undo.vtxundo[i - 1].vprevout};
        if (inputs.size() != coins.size()) return false;
        for (size_t j = 0; j < inputs.size(); ++j) {
            spent.coins.emplace(inputs[j].prevout, coins[j]);
        }
    }
    return true;
}

const SpentCoins* ReadSpentCoins(const CBlockIndex& block_index, const Consensus::Params& consensus, int keep_from_height,
                                 const CBlockIndex& tip)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    const uint256 tip_hash{tip.GetBlockHash()};
    if (g_spent_coins_full.tip_hash == tip_hash && block_index.nHeight <= g_spent_coins_full.height) return nullptr;

    const auto forget{[](auto it) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
        g_spent_coins_total -= it->second.coins.size();
        return g_spent_coins.erase(it);
    }};
    for (auto it = g_spent_coins.begin(); it != g_spent_coins.end() && it->first < keep_from_height;) it = forget(it);
    // Blocks above the tip were left behind by a reorganization and would only take up room.
    for (auto it = g_spent_coins.upper_bound(tip.nHeight); it != g_spent_coins.end();) it = forget(it);

    auto it{g_spent_coins.find(block_index.nHeight)};
    if (it != g_spent_coins.end() && it->second.block_hash != block_index.GetBlockHash()) {
        forget(it);
        it = g_spent_coins.end();
    }
    if (it == g_spent_coins.end()) {
        SpentCoins spent{block_index.GetBlockHash(), false, {}};
        spent.usable = LoadSpentCoins(block_index, consensus, spent);
        if (!spent.usable) spent.coins.clear();
        // Blocks near the tip are needed by every lookup, so room is made at the deep end.
        // Running out of room is remembered only until the tip moves on.
        for (auto deep = g_spent_coins.begin(); deep != g_spent_coins.end() && deep->first < block_index.nHeight &&
                                                g_spent_coins_total + spent.coins.size() > MAX_SPENT_COINS_TOTAL;) {
            // An unusable block holds no coins: dropping it frees nothing and costs a reread.
            deep = deep->second.usable ? forget(deep) : std::next(deep);
        }
        if (g_spent_coins_total + spent.coins.size() > MAX_SPENT_COINS_TOTAL) {
            g_spent_coins_full = {tip_hash, block_index.nHeight};
            return nullptr;
        }
        g_spent_coins_total += spent.coins.size();
        it = g_spent_coins.emplace(block_index.nHeight, std::move(spent)).first;
    }
    return it->second.usable ? &it->second : nullptr;
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
        const SpentCoins* spent{ReadSpentCoins(*walk, consensus, tip->nHeight - max_depth, *tip)};
        if (spent == nullptr) return StakeLookup::UNKNOWN;
        if (const auto it{spent->coins.find(prevout)}; it != spent->coins.end()) return found(it->second);
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
