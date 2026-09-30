// Distributed under the MIT software license, see the accompanying
// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include <consensus/amount.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>

extern RecursiveMutex cs_main; // NOLINT(readability-redundant-declaration)

class CBlockHeader;
class CBlockIndex;
class CChain;
class CTxMemPool;
namespace node {
class BlockManager;
} // namespace node
namespace Consensus {
struct Params;
}


static constexpr CAmount MIN_STAKE_AMOUNT = COIN;
static constexpr int64_t MAX_POS_BLOCK_AHEAD_TIME = 180;
static constexpr int64_t MAX_POS_BLOCK_AHEAD_SAFETY_MARGIN = 5;

// Compute the hash modifier for proof-of-stake
bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint32_t& nStakeModifier);
bool ComputeNextStakeModifierV2(const uint32_t blockTime, const CBlockIndex* pindexPrev, uint32_t& nStakeModifier);

// Check whether stake kernel meets hash target
// Sets hashProofOfStake on success return
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

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(BlockValidationState& state, const CBlockHeader& block, uint256& hashProofOfStake, const Consensus::Params& consensus, const CTxMemPool* mempool = nullptr, const node::BlockManager* blockman = nullptr, const CChain* active_chain = nullptr);

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
