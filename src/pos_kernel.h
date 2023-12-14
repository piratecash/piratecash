// Distributed under the MIT software license, see the accompanying
// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include "streams.h"
#include "validation.h"


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
bool CheckProofOfStake(CValidationState &state, const CBlockHeader &block, uint256& hashProofOfStake, const Consensus::Params& consensus);

#endif // BITCOIN_KERNEL_H
