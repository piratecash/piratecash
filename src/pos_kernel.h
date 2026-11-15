// Distributed under the MIT software license, see the accompanying
// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include <arith_uint256.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>
#include <util/time.h>

#include <optional>
#include <set>
#include <utility>
#include <vector>

extern RecursiveMutex cs_main; // NOLINT(readability-redundant-declaration)

class CBlockHeader;
class CBlockIndex;
class CChain;
class CCoinsViewCache;
class CTxMemPool;
namespace node {
class BlockManager;
} // namespace node
namespace Consensus {
struct Params;
}


static constexpr CAmount MIN_STAKE_AMOUNT = COIN;
/** Confirmations a coinbase output needs before it can stake: CheckProofOfStake wants
 *  more than COINBASE_MATURITY blocks between its block and the block being built on. */
static constexpr int STAKE_COINBASE_MIN_DEPTH{COINBASE_MATURITY + 2};
static constexpr int64_t MAX_POS_BLOCK_AHEAD_TIME = 180;
static constexpr int64_t MAX_POS_BLOCK_AHEAD_SAFETY_MARGIN = 5;

// Compute the hash modifier for proof-of-stake
bool ComputeNextStakeModifierV2(const uint32_t blockTime, const CBlockIndex* pindexPrev, uint32_t& nStakeModifier);

/** How far ahead of the node's own clock a PoS block at `height` may be stamped. */
int64_t MaxPosBlockAheadTime(int height, const Consensus::Params& params);
/** Latest time a PoS v3 block may carry, given the node's own clock (not network-adjusted time). */
int64_t LatestPosV3BlockTime(NodeClock::time_point now, const Consensus::Params& params);
/** Earliest PoS v3 slot at or after `time`. */
int64_t NextPosV3SlotTime(int64_t time, const Consensus::Params& params);
/** Header rules of a PoS block at a PoS v3 height: nNonce is zero, and the time is slot-aligned,
 *  after its parent's and not ahead of `now` + tolerance. */
bool CheckPosV3Header(const CBlockHeader& block, const CBlockIndex& prev, const Consensus::Params& params,
                      NodeClock::time_point now, BlockValidationState& state);

/** The v3 modifier chain starts this many blocks below the last pre-v3 block, so the first v3
 *  kernel depends on the stake inputs of those blocks and not on any block hash. */
static constexpr int POS_V3_MODIFIER_SEED_DEPTH{24};

/**
 * PoS v3 stake modifier M of `block`, the one the kernel of a child of `block` uses.
 * The seed block is the ancestor at height max(0, PosV3Height - 1 - POS_V3_MODIFIER_SEED_DEPTH)
 * and its M is zero; above it (pre-v3 blocks included),
 * M(B) = Hash(stake input hash of B, stake input n of B, M(parent of B)),
 * where the stake input of a block that is not in the PoSv2 format (PoW, PoS v1) is null (zero hash, n = 0).
 * Returns nullopt below the seed height or when the ancestry is incomplete.
 * Results are memoised by block hash; the memo can be dropped at any time. Every 1024th height
 * computed is kept for good, so once a range has been seen no lookup hashes more than 1024 blocks.
 */
std::optional<uint256> GetStakeModifierV3(const CBlockIndex& block, const Consensus::Params& params);
void ClearStakeModifierV3Cache();
/** Size limit of the memo (nullopt restores the default), its current size, the number of
 *  modifiers hashed so far and of eviction passes over the memo, for tests. */
void SetStakeModifierV3CacheLimitForTest(std::optional<size_t> limit);
size_t StakeModifierV3CacheSize();
uint64_t StakeModifierV3HashCountForTest();
uint64_t StakeModifierV3EvictionPassesForTest();

/** Kernel target of a stake: (value / 100) * target(nBits). With `saturate` an overflow gives 2^256-1;
 *  without it the product wraps (pre-v3 consensus, including its partial overflow guard). */
arith_uint256 GetKernelTarget(CAmount value, uint32_t nBits, bool saturate);

/** Wallet policy, not consensus: how many slots past the one that has already started a wallet stamps.
 *  The consensus tolerance (PosV3FutureTolerance) only absorbs clock differences between nodes. */
static constexpr int64_t POSV3_WALLET_SLOT_LOOKAHEAD{0};
/** Wallet policy: at most this many slots are tried in one pass; the rest wait for the next one. */
static constexpr int64_t POSV3_WALLET_MAX_SLOTS_PER_PASS{360};
/** Latest slot the wallet stamps at `now` (node's own clock). */
int64_t WalletLatestPosV3Slot(NodeClock::time_point now, const Consensus::Params& params);
/** Earliest slot a child of `prev` may take: after the parent's time and its median time past. */
int64_t FirstPosV3Slot(const CBlockIndex& prev, const Consensus::Params& params);
/** Last slot one search pass starting at `first` covers: `last`, or less to stay within POSV3_WALLET_MAX_SLOTS_PER_PASS. */
int64_t LastPosV3SlotOfPass(int64_t first, int64_t last, const Consensus::Params& params);

/** A stake input and the slot it stakes in. */
using PosV3Stake = std::pair<COutPoint, int64_t>;

/** A wallet's v3 kernel search over the slots [first, last] on the block `prev`. */
struct PosV3Search {
    uint256 prev;
    int64_t first;
    int64_t last;
    //! Stakes not to use: blocks built from them failed on `prev`.
    std::set<PosV3Stake> excluded;
    //! Set by the search: the stake of the block it built (or tried to), once a kernel is found.
    std::optional<PosV3Stake> used;
    //! Set by the search when it is done with these slots: no coin has a kernel in them that is not
    //! excluded and does not fail before its coinstake is made.
    bool no_kernel{false};
};

/** Which slots a staking wallet tests on the current tip: every slot that has started, each until a
 *  search over it is done, once per tip. The slots do not depend on how often it is called.
 *  Stakes whose blocks failed are left out of later searches on the same tip. */
class PosV3SlotTracker
{
public:
    /** The next slots to search on `tip` at `now`; nullopt when every started slot has been searched. */
    std::optional<PosV3Search> NextRange(const CBlockIndex& tip, NodeClock::time_point now, const Consensus::Params& params);
    /** Report a search on `tip` that built no block: its slots are done if the search says so;
     *  otherwise they are offered again, without the stake it used if it found one. */
    void Searched(const CBlockIndex& tip, const PosV3Search& search, const Consensus::Params& params);
    /** Leave `stake` out of later searches on `tip`: the block built from it failed. */
    void Exclude(const CBlockIndex& tip, const PosV3Stake& stake);

private:
    void SetTip(const CBlockIndex& tip);

    uint256 m_tip;
    int64_t m_next_slot{0};
    std::set<PosV3Stake> m_excluded;
};

struct PosV3Slot {
    uint32_t time;
    //! The nBits validation requires of a child of the parent stamped `time`.
    uint32_t bits;
};
/** The valid slots of a child of `prev` in [first, last], oldest first, at most POSV3_WALLET_MAX_SLOTS_PER_PASS. */
std::vector<PosV3Slot> PosV3Slots(const CBlockIndex& prev, int64_t first, int64_t last, const Consensus::Params& params);
/** Search `slots` for a v3 kernel of one stake, oldest slot first; `modifier` is GetStakeModifierV3() of `prev`.
 *  On success returns the index of the slot, sets nTime and nBits of `current` and zeroes its nNonce;
 *  otherwise returns nullopt and leaves it untouched. */
std::optional<size_t> FindStakeKernelV3(CBlockHeader& current, const CBlockIndex& prev, const CBlockIndex& blockFrom, const CTxOut& stake_out,
                                        const COutPoint& prevout, const std::vector<PosV3Slot>& slots, const uint256& modifier,
                                        int64_t min_stake_age, const Consensus::Params& params, uint256& hashProofOfStake,
                                        bool fPrint = false);

/** A coin that may stake on a block. */
struct PosV3StakeCandidate {
    const CBlockIndex* block_from;
    CTxOut stake_out;
    COutPoint prevout;
};
/** The candidates with a kernel in `slots` as (candidate index, slot index): for each candidate its earliest
 *  slot that is not `excluded`, earliest slot first and, within a slot, in the order given.
 *  `modifier` is GetStakeModifierV3() of `prev`. */
std::vector<std::pair<size_t, size_t>> RankStakeKernelsV3(const std::vector<PosV3StakeCandidate>& candidates, const CBlockIndex& prev,
                                                          const std::vector<PosV3Slot>& slots, const uint256& modifier,
                                                          int64_t min_stake_age, const Consensus::Params& params,
                                                          const std::set<PosV3Stake>& excluded = {});

/** PoS v3 kernel hash of one slot; `modifier` is GetStakeModifierV3() of the parent block. */
uint256 StakeHashV3(const uint256& modifier, uint32_t nTimeBlockFrom, const COutPoint& prevout, uint32_t nTime);

// Check whether stake kernel meets hash target
// Sets hashProofOfStake on success return
// With fCheck=false it searches for a kernel below PoS v3 only: it writes the modifier into nNonce
// before searching and nTime on success. At v3 heights only fCheck=true is valid; searches use FindStakeKernelV3().
uint256 stakeHash(unsigned int nTimeTx, CDataStream ss, unsigned int prevoutIndex, uint256 prevoutHash, unsigned int nTimeBlockFrom);
bool CheckStakeKernelHash(
    CBlockHeader &current,
    const CBlockIndex &blockPrev,
    const CBlockIndex &blockFrom,
    const CTransaction txPrev,
    const COutPoint prevout,
    unsigned int nHashDrift,
    bool fCheck,
    uint256& hashProofOfStake,
    bool fPrintProofOfStake = false);

// Same check for callers that hold only the stake output (e.g. a UTXO entry).
bool CheckStakeKernelHash(
    CBlockHeader &current,
    const CBlockIndex &blockPrev,
    const CBlockIndex &blockFrom,
    const CTxOut& stake_out,
    const COutPoint prevout,
    unsigned int nHashDrift,
    bool fCheck,
    uint256& hashProofOfStake,
    bool fPrintProofOfStake = false);

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(BlockValidationState& state, const CBlockHeader& block, uint256& hashProofOfStake, const Consensus::Params& consensus, const CTxMemPool* mempool = nullptr, const node::BlockManager* blockman = nullptr, const CChain* active_chain = nullptr, CCoinsViewCache* coins_tip = nullptr);

// Header-only stake double-spend guard, extracted from CheckProofOfStake so
// the walk can be unit-tested directly. Walks the still-unvalidated PoS
// header tail from `pindex_prev` toward the fork point `pindex_fork`
// (exclusive) and returns true if `prevout` is reused as a stake input by any
// node along the way. The walk stops at the fork point, at the first
// fully-validated (BLOCK_VALID_SCRIPTS) or non-PoS node, or at genesis
// (pprev == nullptr).
bool HasHeaderOnlyStakeReuse(const CBlockIndex* pindex_prev,
                            const CBlockIndex* pindex_fork,
                            const COutPoint& prevout) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

#endif // BITCOIN_KERNEL_H
