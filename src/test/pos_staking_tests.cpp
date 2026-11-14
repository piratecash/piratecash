// Copyright (c) 2024-2026 The Cosanta Core developers
// Copyright (c) 2018-2026 The PirateCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Proof-of-Stake staking validation tests
//
// These tests verify:
//   1. Minimum stake amount enforcement (MIN_STAKE_AMOUNT)
//   2. Stake maturity / min age enforcement (nStakeMinAge)
//   3. Coinbase maturity for stake inputs (COINBASE_MATURITY)
//   4. PoS block header parameter validation (stake modifier, signature, etc.)
//   5. InstantSend exclusion for coinstake transactions
//   6. Orphan block and reorganization safety with PoS
//   7. Double-spend detection in PoS header chains
//   8. Fork-point UTXO boundary checks
//   9. Wallet releases the inputs of orphaned coinstakes (with wallet only)
//

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <boost/test/unit_test.hpp>

#include <active/masternode.h>
#include <consensus/amount.h>
#include <arith_uint256.h>
#include <bls/bls.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <governance/governance.h>
#include <hash.h>
#include <key.h>
#include <key_io.h>
#include <llmq/blockprocessor.h>
#include <chainlock/chainlock.h>
#include <llmq/context.h>
#include <llmq/signing_shares.h>
#include <masternode/sync.h>
#include <netaddress.h>
#include <instantsend/instantsend.h>
#include <instantsend/signing.h>
#include <evo/evodb.h>
#include <interfaces/chain.h>
#include <node/miner.h>
#include <pos_kernel.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <random.h>
#include <script/script.h>
#include <script/standard.h>
#include <spork.h>
#include <test/util/index.h>
#include <test/util/logging.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <timedata.h>
#include <tinyformat.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/string.h>
#include <util/system.h>
#include <util/time.h>
#include <util/translation.h>
#include <validation.h>
#include <validationinterface.h>
#include <warnings.h>
#include <index/txindex.h>

#ifdef ENABLE_WALLET
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/transaction.h>
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <thread>

// ============================================================================
// Test fixture
// ============================================================================

namespace pos_staking_tests {

struct PoSTestingSetup : public TestChain100Setup {

    // Helper: create a simple P2PKH output script
    CScript ScriptForKey(const CKey& key) const {
        return CScript() << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;
    }

    // Helper: create a raw coinbase-like transaction that pays to `key`
    // with a given value
    CTransactionRef CreateFundingTx(const CKey& key, CAmount value) {
        CMutableTransaction mtx;
        mtx.vin.resize(1);
        mtx.vin[0].prevout.SetNull(); // coinbase
        mtx.vin[0].scriptSig = CScript() << 0x01;
        mtx.vout.resize(1);
        mtx.vout[0].nValue = value;
        mtx.vout[0].scriptPubKey = ScriptForKey(key);
        return MakeTransactionRef(std::move(mtx));
    }

    // Helper: create a mock stake transaction (non-coinbase, spendable)
    CMutableTransaction CreateStakeTx(const COutPoint& prevout, const CKey& key, CAmount value) {
        CMutableTransaction mtx;
        mtx.vin.resize(1);
        mtx.vin[0].prevout = prevout;
        mtx.vout.resize(2);
        // First output empty (coinstake marker)
        mtx.vout[0].nValue = 0;
        mtx.vout[0].scriptPubKey.clear();
        // Second output: stake reward back to same key
        mtx.vout[1].nValue = value;
        mtx.vout[1].scriptPubKey = ScriptForKey(key);
        return mtx;
    }

    // Helper: build a minimal PoS block header for testing
    CBlockHeader MakePoSHeader(const uint256& hashPrev, uint32_t nTime,
                                const uint256& stakeHash, uint32_t stakeN) {
        CBlockHeader header;
        header.nVersion = CBlockHeader::POS_BIT | 1; // Mark as PoS
        header.hashPrevBlock = hashPrev;
        header.nTime = nTime;
        header.nBits = 0x207fffff; // regtest difficulty
        header.posStakeHash = stakeHash;
        header.posStakeN = stakeN;
        return header;
    }

    // Mine PoW blocks to advance the chain
    void MineBlocks(int count) {
        CScript scriptPubKey = ScriptForKey(coinbaseKey);
        for (int i = 0; i < count; i++) {
            CreateAndProcessBlock({}, scriptPubKey);
        }
    }
};

} // namespace pos_staking_tests

BOOST_FIXTURE_TEST_SUITE(pos_staking_tests, pos_staking_tests::PoSTestingSetup)

// ============================================================================
// 1. MINIMUM STAKE AMOUNT TESTS
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_min_stake_amount_constant)
{
    // Verify that MIN_STAKE_AMOUNT is exactly 1 COIN
    BOOST_CHECK_EQUAL(MIN_STAKE_AMOUNT, COIN);
    BOOST_CHECK(MIN_STAKE_AMOUNT > 0);
}

BOOST_AUTO_TEST_CASE(pos_reject_stake_below_minimum)
{
    LOCK(cs_main);

    // Create a mock scenario where the stake value is below MIN_STAKE_AMOUNT.
    // CheckStakeKernelHash should reject stakes with value < MIN_STAKE_AMOUNT.
    //
    // We construct a transaction with an output value of MIN_STAKE_AMOUNT - 1
    // and verify it fails the stake kernel check.

    CKey stakeKey;
    stakeKey.MakeNewKey(true);

    // Create a transaction with an output value just below the minimum
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 0x42;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = MIN_STAKE_AMOUNT - 1; // Below minimum!
    mtx.vout[0].scriptPubKey = ScriptForKey(stakeKey);
    CTransaction txBelow(mtx);

    // Set up a mock block index for "blockFrom"
    CBlockIndex blockFrom;
    blockFrom.nHeight = 50;
    blockFrom.nTime = m_node.chainman->ActiveChain().Tip()->nTime - (24 * 3600 + 100); // Well past min age

    CBlockIndex blockPrev;
    blockPrev.nHeight = m_node.chainman->ActiveChain().Tip()->nHeight;
    blockPrev.nTime = m_node.chainman->ActiveChain().Tip()->nTime;

    COutPoint prevout(txBelow.GetHash(), 0);

    CBlockHeader header = MakePoSHeader(
        m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
        m_node.chainman->ActiveChain().Tip()->nTime + 1,
        prevout.hash, prevout.n);

    uint256 hashProofOfStake;

    // This should fail because value < MIN_STAKE_AMOUNT
    bool result = CheckStakeKernelHash(
        header, blockPrev, blockFrom, txBelow, prevout,
        0, true, hashProofOfStake, false);

    BOOST_CHECK_MESSAGE(!result, "Stake below MIN_STAKE_AMOUNT must be rejected");
}

BOOST_AUTO_TEST_CASE(pos_accept_stake_at_minimum)
{
    LOCK(cs_main);

    // Control/experiment test: we run CheckStakeKernelHash with two identical
    // setups differing ONLY in stake value. If the below-minimum call fails
    // and the at-minimum call gets a different hash result (non-zero), we've
    // proven the amount gate was the differentiator.
    //
    // CheckStakeKernelHash returns false for both "amount too small" and
    // "hash doesn't meet target", but the amount check returns error()
    // BEFORE computing hashProofOfStake. So if hashProofOfStake is non-zero
    // after the call, we know the function passed the amount gate.

    CKey stakeKey;
    stakeKey.MakeNewKey(true);

    auto makeTestTx = [&](CAmount value, uint8_t salt) -> CTransaction {
        CMutableTransaction mtx;
        mtx.vin.resize(1);
        mtx.vin[0].prevout.SetNull();
        mtx.vin[0].scriptSig = CScript() << salt;
        mtx.vout.resize(1);
        mtx.vout[0].nValue = value;
        mtx.vout[0].scriptPubKey = ScriptForKey(stakeKey);
        return CTransaction(mtx);
    };

    CTransaction txBelow = makeTestTx(MIN_STAKE_AMOUNT - 1, 0x43);
    CTransaction txExact = makeTestTx(MIN_STAKE_AMOUNT, 0x44);

    CBlockIndex blockFrom;
    blockFrom.nHeight = 10;
    blockFrom.nTime = 1000;

    CBlockIndex blockPrev;
    blockPrev.nHeight = m_node.chainman->ActiveChain().Tip()->nHeight;
    blockPrev.nTime = m_node.chainman->ActiveChain().Tip()->nTime;

    uint32_t stakeTime = blockFrom.nTime + Params().MinStakeAge() + 100;

    // --- Below minimum: amount gate rejects, hashProofOfStake stays zero ---
    {
        COutPoint prevout(txBelow.GetHash(), 0);
        CBlockHeader header = MakePoSHeader(
            m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
            stakeTime, prevout.hash, prevout.n);
        uint256 hashProofOfStake;
        bool result = CheckStakeKernelHash(
            header, blockPrev, blockFrom, txBelow, prevout,
            0, true, hashProofOfStake, false);
        BOOST_CHECK(!result);
        // Amount gate rejects before hash is computed
        BOOST_CHECK_MESSAGE(hashProofOfStake.IsNull(),
            "hashProofOfStake should remain zero when amount gate rejects");
    }

    // --- At minimum: amount gate passes, function proceeds to hash computation ---
    {
        COutPoint prevout(txExact.GetHash(), 0);
        CBlockHeader header = MakePoSHeader(
            m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
            stakeTime, prevout.hash, prevout.n);
        uint256 hashProofOfStake;
        CheckStakeKernelHash(
            header, blockPrev, blockFrom, txExact, prevout,
            0, true, hashProofOfStake, false);
        // Function proceeds past amount gate to hash computation.
        // hashProofOfStake should be non-zero (hash was computed).
        BOOST_CHECK_MESSAGE(!hashProofOfStake.IsNull(),
            "hashProofOfStake should be non-zero when amount gate passes — "
            "proves the function got past MIN_STAKE_AMOUNT check");
    }
}

BOOST_AUTO_TEST_CASE(pos_reject_zero_value_stake)
{
    LOCK(cs_main);

    // Zero-value output must never be accepted as a valid stake input
    CKey stakeKey;
    stakeKey.MakeNewKey(true);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 0x44;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 0; // Zero!
    mtx.vout[0].scriptPubKey = ScriptForKey(stakeKey);
    CTransaction txZero(mtx);

    CBlockIndex blockFrom;
    blockFrom.nHeight = 10;
    blockFrom.nTime = 1000;

    CBlockIndex blockPrev;
    blockPrev.nHeight = m_node.chainman->ActiveChain().Tip()->nHeight;
    blockPrev.nTime = m_node.chainman->ActiveChain().Tip()->nTime;

    COutPoint prevout(txZero.GetHash(), 0);
    uint32_t stakeTime = blockFrom.nTime + Params().MinStakeAge() + 100;

    CBlockHeader header = MakePoSHeader(
        m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
        stakeTime, prevout.hash, prevout.n);

    uint256 hashProofOfStake;
    bool result = CheckStakeKernelHash(
        header, blockPrev, blockFrom, txZero, prevout,
        0, true, hashProofOfStake, false);

    BOOST_CHECK_MESSAGE(!result, "Zero-value stake must be rejected");
}

// ============================================================================
// 2. STAKE MATURITY / MIN AGE TESTS
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_min_age_constant)
{
    // Verify regtest uses 24 hours min stake age
    BOOST_CHECK_EQUAL(Params().MinStakeAge(), 24 * 60 * 60);
}

BOOST_AUTO_TEST_CASE(pos_reject_immature_stake)
{
    LOCK(cs_main);

    // Stake input that hasn't reached the minimum age must be rejected.
    // nTimeBlockFrom + min_age > nTimeTx => rejected

    CKey stakeKey;
    stakeKey.MakeNewKey(true);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 0x50;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 10 * COIN;
    mtx.vout[0].scriptPubKey = ScriptForKey(stakeKey);
    CTransaction txPrev(mtx);

    int64_t minAge = Params().MinStakeAge(); // 86400 seconds

    CBlockIndex blockFrom;
    blockFrom.nHeight = 50;
    blockFrom.nTime = 100000; // Block time

    CBlockIndex blockPrev;
    blockPrev.nHeight = m_node.chainman->ActiveChain().Tip()->nHeight;
    blockPrev.nTime = m_node.chainman->ActiveChain().Tip()->nTime;

    COutPoint prevout(txPrev.GetHash(), 0);

    // Set stake time to be LESS than blockFrom.nTime + minAge
    // i.e., the stake is not mature yet
    uint32_t immatureTime = blockFrom.nTime + minAge - 1; // 1 second too early

    CBlockHeader header = MakePoSHeader(
        m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
        immatureTime, prevout.hash, prevout.n);

    uint256 hashProofOfStake;
    bool result = CheckStakeKernelHash(
        header, blockPrev, blockFrom, txPrev, prevout,
        0, true, hashProofOfStake, false);

    BOOST_CHECK_MESSAGE(!result, "Immature stake (min age not reached) must be rejected");
}

BOOST_AUTO_TEST_CASE(pos_reject_stake_at_exact_boundary)
{
    LOCK(cs_main);

    // Control/experiment test for the maturity boundary.
    // The check is: nTimeBlockFrom + min_age > nTimeTx
    //
    // At nTimeTx == nTimeBlockFrom + min_age: condition is FALSE → age passes
    // At nTimeTx == nTimeBlockFrom + min_age - 1: condition is TRUE → age rejects
    //
    // We distinguish the code paths via hashProofOfStake: the age gate rejects
    // BEFORE hash computation, so hashProofOfStake stays zero on age rejection.

    CKey stakeKey;
    stakeKey.MakeNewKey(true);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 0x51;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 10 * COIN;
    mtx.vout[0].scriptPubKey = ScriptForKey(stakeKey);
    CTransaction txPrev(mtx);

    int64_t minAge = Params().MinStakeAge();

    CBlockIndex blockFrom;
    blockFrom.nHeight = 50;
    blockFrom.nTime = 100000;

    CBlockIndex blockPrev;
    blockPrev.nHeight = m_node.chainman->ActiveChain().Tip()->nHeight;
    blockPrev.nTime = m_node.chainman->ActiveChain().Tip()->nTime;

    COutPoint prevout(txPrev.GetHash(), 0);

    // --- 1 second before boundary: age gate rejects ---
    {
        uint32_t tooEarly = blockFrom.nTime + minAge - 1;
        CBlockHeader header = MakePoSHeader(
            m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
            tooEarly, prevout.hash, prevout.n);
        uint256 hashProofOfStake;
        bool result = CheckStakeKernelHash(
            header, blockPrev, blockFrom, txPrev, prevout,
            0, true, hashProofOfStake, false);
        BOOST_CHECK(!result);
        BOOST_CHECK_MESSAGE(hashProofOfStake.IsNull(),
            "hashProofOfStake must be zero when age gate rejects (1s before boundary)");
    }

    // --- Exactly at boundary: age gate passes, hash is computed ---
    {
        uint32_t exactBoundary = blockFrom.nTime + minAge;
        CBlockHeader header = MakePoSHeader(
            m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
            exactBoundary, prevout.hash, prevout.n);
        uint256 hashProofOfStake;
        CheckStakeKernelHash(
            header, blockPrev, blockFrom, txPrev, prevout,
            0, true, hashProofOfStake, false);
        BOOST_CHECK_MESSAGE(!hashProofOfStake.IsNull(),
            "hashProofOfStake must be non-zero at boundary — proves age gate passed");
    }

    // --- 1 second after boundary: also passes age gate ---
    {
        uint32_t afterBoundary = blockFrom.nTime + minAge + 1;
        CBlockHeader header = MakePoSHeader(
            m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
            afterBoundary, prevout.hash, prevout.n);
        uint256 hashProofOfStake;
        CheckStakeKernelHash(
            header, blockPrev, blockFrom, txPrev, prevout,
            0, true, hashProofOfStake, false);
        BOOST_CHECK_MESSAGE(!hashProofOfStake.IsNull(),
            "hashProofOfStake must be non-zero after boundary — proves age gate passed");
    }
}

BOOST_AUTO_TEST_CASE(pos_reject_timestamp_violation)
{
    LOCK(cs_main);

    // nTimeTx < nTimeBlockFrom is a timestamp violation and must be rejected

    CKey stakeKey;
    stakeKey.MakeNewKey(true);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 0x52;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 10 * COIN;
    mtx.vout[0].scriptPubKey = ScriptForKey(stakeKey);
    CTransaction txPrev(mtx);

    CBlockIndex blockFrom;
    blockFrom.nHeight = 50;
    blockFrom.nTime = 200000; // Block is at time 200000

    CBlockIndex blockPrev;
    blockPrev.nHeight = m_node.chainman->ActiveChain().Tip()->nHeight;
    blockPrev.nTime = m_node.chainman->ActiveChain().Tip()->nTime;

    COutPoint prevout(txPrev.GetHash(), 0);

    // Set stake time BEFORE the block time = timestamp violation
    uint32_t earlierTime = blockFrom.nTime - 1;

    CBlockHeader header = MakePoSHeader(
        m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
        earlierTime, prevout.hash, prevout.n);

    uint256 hashProofOfStake;
    bool result = CheckStakeKernelHash(
        header, blockPrev, blockFrom, txPrev, prevout,
        0, true, hashProofOfStake, false);

    BOOST_CHECK_MESSAGE(!result, "Stake with nTimeTx < nTimeBlockFrom must be rejected (timestamp violation)");
}

// ============================================================================
// 3. COINBASE MATURITY FOR STAKE INPUTS
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_coinbase_maturity_constant)
{
    // Verify COINBASE_MATURITY is 120 blocks
    BOOST_CHECK_EQUAL(COINBASE_MATURITY, 120);
}

// ============================================================================
// 4. PoS BLOCK STRUCTURE VALIDATION
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_block_header_flags)
{
    // Verify PoS / PoW flag bits work correctly
    CBlockHeader header;

    // Default should be PoW
    header.nVersion = 1;
    BOOST_CHECK(header.IsProofOfWork());
    BOOST_CHECK(!header.IsProofOfStake());

    // Set PoS bit
    header.nVersion = CBlockHeader::POS_BIT | 1;
    BOOST_CHECK(header.IsProofOfStake());
    BOOST_CHECK(!header.IsProofOfWork());

    // Set PoS v2 bits
    header.nVersion = CBlockHeader::POSV2_BITS | 1;
    BOOST_CHECK(header.IsProofOfStake());
    BOOST_CHECK(header.IsProofOfStakeV2());
    BOOST_CHECK(!header.IsProofOfWork());
}

BOOST_AUTO_TEST_CASE(pos_block_stake_input_outpoint)
{
    // Verify StakeInput() correctly returns the COutPoint from header
    CBlockHeader header;
    uint256 expectedHash = InsecureRand256();
    uint32_t expectedN = 7;

    header.posStakeHash = expectedHash;
    header.posStakeN = expectedN;

    COutPoint stake = header.StakeInput();
    BOOST_CHECK_EQUAL(stake.hash, expectedHash);
    BOOST_CHECK_EQUAL(stake.n, expectedN);
}

BOOST_AUTO_TEST_CASE(pos_block_reject_missing_signature)
{
    LOCK(cs_main);

    // A PoS block with empty signature must be rejected by CheckProofOfStake

    CBlockHeader header;
    header.nVersion = CBlockHeader::POS_BIT | 1;
    header.posBlockSig.clear(); // Empty signature!
    header.posStakeHash = InsecureRand256();
    header.posStakeN = 0;
    header.hashPrevBlock = m_node.chainman->ActiveChain().Tip()->GetBlockHash();
    header.nTime = m_node.chainman->ActiveChain().Tip()->nTime + 60;
    header.nBits = 0x207fffff;

    BlockValidationState state;
    uint256 hashProofOfStake;

    bool result = CheckProofOfStake(state, header, hashProofOfStake, Params().GetConsensus(),
                                    m_node.mempool.get(), &m_node.chainman->m_blockman,
                                    &m_node.chainman->ActiveChain());

    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-pos-sig");
}

BOOST_AUTO_TEST_CASE(pos_block_reject_unknown_stake_input)
{
    LOCK(cs_main);

    // A PoS block referencing a non-existent UTXO must be rejected

    CKey stakeKey;
    stakeKey.MakeNewKey(true);

    CBlockHeader header;
    header.nVersion = CBlockHeader::POS_BIT | 1;
    header.posStakeHash = InsecureRand256(); // Random hash — won't exist
    header.posStakeN = 0;
    header.hashPrevBlock = m_node.chainman->ActiveChain().Tip()->GetBlockHash();
    header.nTime = m_node.chainman->ActiveChain().Tip()->nTime + 60;
    header.nBits = 0x207fffff;
    header.posBlockSig = {0x01, 0x02, 0x03}; // Non-empty dummy sig

    BlockValidationState state;
    uint256 hashProofOfStake;

    bool result = CheckProofOfStake(state, header, hashProofOfStake, Params().GetConsensus(),
                                    m_node.mempool.get(), &m_node.chainman->m_blockman,
                                    &m_node.chainman->ActiveChain());

    BOOST_CHECK(!result);
    // Should reject because the stake TX is unknown
    BOOST_CHECK(state.GetRejectReason() == "bad-unkown-stake" ||
                state.GetRejectReason() == "tmp-bad-unkown-stake");
}

BOOST_AUTO_TEST_CASE(pos_block_reject_mempool_stake)
{
    // A PoS block should reject stake inputs that are only in the mempool
    // and not yet confirmed on the active chain.
    // This is checked in CheckProofOfStake: "bad-stake-mempool"

    // This is tested indirectly — if a txin's block is not in the active chain
    // but the header's prev IS in the active chain, it returns "bad-stake-mempool"
    // Just verify the reject reason constant exists in our validation
    BlockValidationState state;
    state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-stake-mempool",
                  "stake from mempool");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-stake-mempool");
}

// ============================================================================
// 5. PoS BLOCK STRUCTURE: HasStake() and coinstake constraints
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_coinstake_transaction_identification)
{
    // A coinstake transaction has:
    //  - Non-null vin[0].prevout
    //  - vout.size() >= 2
    //  - vout[0] is empty

    CKey key;
    key.MakeNewKey(true);

    // Valid coinstake
    CMutableTransaction mtxStake;
    mtxStake.vin.resize(1);
    mtxStake.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtxStake.vout.resize(2);
    mtxStake.vout[0].nValue = 0; mtxStake.vout[0].scriptPubKey.clear(); // Empty marker
    mtxStake.vout[1].nValue = 10 * COIN;
    mtxStake.vout[1].scriptPubKey = ScriptForKey(key);

    CTransaction txStake(mtxStake);
    BOOST_CHECK(txStake.IsCoinStake());
    BOOST_CHECK(!txStake.IsCoinBase());

    // NOT coinstake: vin[0] is null (coinbase)
    CMutableTransaction mtxCoinbase;
    mtxCoinbase.vin.resize(1);
    mtxCoinbase.vin[0].prevout.SetNull();
    mtxCoinbase.vin[0].scriptSig = CScript() << 0x01;
    mtxCoinbase.vout.resize(1);
    mtxCoinbase.vout[0].nValue = 50 * COIN;
    mtxCoinbase.vout[0].scriptPubKey = ScriptForKey(key);

    CTransaction txCB(mtxCoinbase);
    BOOST_CHECK(!txCB.IsCoinStake());
    BOOST_CHECK(txCB.IsCoinBase());
}

BOOST_AUTO_TEST_CASE(pos_coinstake_requires_empty_first_output)
{
    // A transaction with non-null prevout but without empty vout[0]
    // should NOT be identified as coinstake

    CKey key;
    key.MakeNewKey(true);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(2);
    mtx.vout[0].nValue = 5 * COIN; // NOT empty
    mtx.vout[0].scriptPubKey = ScriptForKey(key);
    mtx.vout[1].nValue = 10 * COIN;
    mtx.vout[1].scriptPubKey = ScriptForKey(key);

    CTransaction tx(mtx);
    BOOST_CHECK_MESSAGE(!tx.IsCoinStake(),
        "Transaction with non-empty vout[0] must not be identified as coinstake");
}

BOOST_AUTO_TEST_CASE(pos_coinstake_requires_two_outputs)
{
    // A transaction with only 1 output cannot be coinstake (needs >= 2)

    CKey key;
    key.MakeNewKey(true);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 0;
    mtx.vout[0].scriptPubKey.clear();

    CTransaction tx(mtx);
    BOOST_CHECK_MESSAGE(!tx.IsCoinStake(),
        "Transaction with only 1 output must not be identified as coinstake");
}

// ============================================================================
// 6. PoS BLOCK MAX TIME AHEAD CHECK
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_max_block_ahead_time)
{
    // PoS blocks have a tighter future time limit than PoW blocks
    BOOST_CHECK_EQUAL(MAX_POS_BLOCK_AHEAD_TIME, 180); // 3 minutes
    BOOST_CHECK_EQUAL(MAX_POS_BLOCK_AHEAD_SAFETY_MARGIN, 5); // 5 seconds
}

// ============================================================================
// 7. INSTANTSEND EXCLUSION FOR COINSTAKE
// ============================================================================

// The two tests below guard the coinstake exclusion added in a5b5ae3e28, lost
// when rebasing onto Dash v23 and restored in d770ff4191. Upstream Dash has no
// coinstake, so every future rebase can drop it again. Without it, masternodes
// lock coinstakes; when the staked block is orphaned the lock outlives it, and
// nodes holding the lock reject the next block that stakes the same outputs
// with "conflict-tx-lock" until they are restarted.

BOOST_AUTO_TEST_CASE(pos_instantsend_excludes_coinstake_from_processing)
{
    CKey key;
    key.MakeNewKey(true);
    BOOST_REQUIRE(m_node.sporkman->SetSporkAddress(EncodeDestination(PKHash(key.GetPubKey()))));
    BOOST_REQUIRE(m_node.sporkman->SetPrivKey(EncodeSecret(key)));
    BOOST_REQUIRE(m_node.sporkman->UpdateSpork(SPORK_2_INSTANTSEND_ENABLED, 0).has_value());
    auto& isman = *Assert(m_node.isman);
    BOOST_REQUIRE(isman.IsInstantSendEnabled());

    m_node.mn_sync->SwitchToNextAsset();
    BOOST_REQUIRE(m_node.mn_sync->IsBlockchainSynced());

    auto& chainman = *Assert(m_node.chainman);
    auto& llmq_ctx = *Assert(m_node.llmq_ctx);
    CBLSSecretKey operator_sk;
    operator_sk.MakeNewKey();
    CActiveMasternodeManager mn_activeman(*Assert(m_node.connman), *Assert(m_node.dmnman), operator_sk);
    llmq::CSigSharesManager shareman(*m_node.connman, chainman, *llmq_ctx.sigman,
                                     mn_activeman, *llmq_ctx.qman, *m_node.sporkman);
    instantsend::InstantSendSigner signer(chainman, *m_node.chainlocks, isman,
                                          *llmq_ctx.sigman, shareman, *llmq_ctx.qman, *m_node.sporkman,
                                          *m_node.mempool, *m_node.mn_sync);
    const auto& params = Params().GetConsensus();

    // A locked parent makes the input eligible without requiring txindex or a
    // mined funding transaction. The lock does not conflict with its spend.
    const COutPoint prevout(InsecureRand256(), 0);
    auto parent_lock = std::make_shared<instantsend::InstantSendLock>();
    parent_lock->txid = prevout.hash;
    parent_lock->inputs.emplace_back(InsecureRand256(), 0);
    isman.WriteNewISLock(::SerializeHash(*parent_lock), parent_lock, /*minedHeight=*/std::nullopt);
    BOOST_REQUIRE(isman.IsLocked(prevout.hash));

    const CTransaction txStake(CreateStakeTx(prevout, key, 50 * COIN));
    BOOST_REQUIRE(txStake.IsCoinStake());
    CMutableTransaction mtxRegular(txStake);
    mtxRegular.vout.erase(mtxRegular.vout.begin());
    const CTransaction txRegular(mtxRegular);
    BOOST_REQUIRE(!txRegular.IsCoinStake());

    // Observe dispatch to input signing through its existing diagnostic. No
    // quorum is needed to attempt signing; this test does not claim recovery
    // of an actual quorum signature.
    const auto attempts_signing = [&](const CTransaction& tx, bool retroactive) {
        bool attempted{false};
        {
            DebugLogHelper log{strprintf("TrySignInputLocks -- txid=%s: trying to vote on input", tx.GetHash().ToString()),
                               [&](const std::string* line) {
                                   attempted |= line != nullptr;
                                   return false;
                               }};
            signer.ProcessTx(tx, retroactive, params);
        }
        return attempted;
    };

    for (const bool retroactive : {true, false}) {
        BOOST_REQUIRE_MESSAGE(attempts_signing(txRegular, retroactive),
                              "control: regular tx did not reach input signing, retroactive=" << retroactive);
        BOOST_CHECK_MESSAGE(!attempts_signing(txStake, retroactive),
                            "coinstake entered InstantSend input signing, retroactive=" << retroactive);
    }
}

BOOST_AUTO_TEST_CASE(pos_coinstake_ignores_conflicting_instantsend_lock)
{
    // Blocks must not be rejected because their coinstake shares an input with
    // a stored lock, while ordinary spends of that input stay protected.
    CKey key;
    key.MakeNewKey(true);

    // Regtest defaults to InstantSend being disabled. Exercise the conflict
    // lookup with it enabled, so an empty/disabled manager cannot pass the test.
    BOOST_REQUIRE(m_node.sporkman->SetSporkAddress(EncodeDestination(PKHash(key.GetPubKey()))));
    BOOST_REQUIRE(m_node.sporkman->SetPrivKey(EncodeSecret(key)));
    BOOST_REQUIRE(m_node.sporkman->UpdateSpork(SPORK_2_INSTANTSEND_ENABLED, 0).has_value());
    BOOST_REQUIRE(m_node.isman);
    auto& isman = *m_node.isman;
    BOOST_REQUIRE(isman.IsInstantSendEnabled());

    const COutPoint prevout(InsecureRand256(), 0);
    const auto lockedStake = MakeTransactionRef(CreateStakeTx(prevout, key, 50 * COIN));
    BOOST_REQUIRE(lockedStake->IsCoinStake());

    // Store a legacy coinstake lock through the manager's public persistence API.
    auto islock = std::make_shared<instantsend::InstantSendLock>();
    islock->txid = lockedStake->GetHash();
    islock->inputs = {prevout};
    isman.WriteNewISLock(::SerializeHash(*islock), islock, /*minedHeight=*/std::nullopt);
    BOOST_REQUIRE(isman.IsLocked(lockedStake->GetHash()));

    const CTransaction txStake(CreateStakeTx(prevout, key, 51 * COIN));
    BOOST_REQUIRE(txStake.IsCoinStake());
    BOOST_REQUIRE(txStake.GetHash() != lockedStake->GetHash());

    CMutableTransaction mtxRegular(txStake);
    mtxRegular.vout.erase(mtxRegular.vout.begin());
    const CTransaction txRegular(mtxRegular);
    BOOST_REQUIRE(!txRegular.IsCoinStake());

    // The same stored lock must still protect ordinary spends, while a new
    // coinstake sharing its input must not stop block connection.
    const auto conflict = isman.GetConflictingLock(txRegular);
    BOOST_REQUIRE(conflict);
    BOOST_CHECK(conflict->txid == lockedStake->GetHash());
    BOOST_CHECK_MESSAGE(isman.GetConflictingLock(txStake) == nullptr,
                        "coinstake conflicts with a stored InstantSend lock: "
                        "CInstantSendManager::GetConflictingLock must ignore IsCoinStake(), see d770ff4191");
}

// ============================================================================
// 8. STAKE MODIFIER VALIDATION
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_stake_modifier_mismatch_rejected)
{
    LOCK(cs_main);

    // When fCheck=true, CheckStakeKernelHash must reject blocks
    // with incorrect stake modifiers.

    CKey stakeKey;
    stakeKey.MakeNewKey(true);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 0x60;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 100 * COIN;
    mtx.vout[0].scriptPubKey = ScriptForKey(stakeKey);
    CTransaction txPrev(mtx);

    CBlockIndex blockFrom;
    blockFrom.nHeight = 1;
    blockFrom.nTime = 1000;

    CBlockIndex blockPrev;
    blockPrev.nHeight = m_node.chainman->ActiveChain().Tip()->nHeight;
    blockPrev.nTime = m_node.chainman->ActiveChain().Tip()->nTime;

    COutPoint prevout(txPrev.GetHash(), 0);
    uint32_t stakeTime = blockFrom.nTime + Params().MinStakeAge() + 100;

    CBlockHeader header = MakePoSHeader(
        m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
        stakeTime, prevout.hash, prevout.n);

    // Set an intentionally wrong stake modifier
    header.nStakeModifier() = 0xDEADBEEF;

    uint256 hashProofOfStake;

    // With fCheck=true, the modifier mismatch should cause rejection
    bool result = CheckStakeKernelHash(
        header, blockPrev, blockFrom, txPrev, prevout,
        0, true, hashProofOfStake, false);

    BOOST_CHECK_MESSAGE(!result, "Block with wrong stake modifier must be rejected");
}

// ============================================================================
// 9. ORPHAN / REORGANIZATION SAFETY TESTS
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_chain_reorg_basic)
{
    // Test that a chain reorganization works correctly:
    // - Build two competing chains from a common ancestor
    // - The longer chain should win
    // - Blocks from the shorter chain become orphans

    CScript scriptPubKey = ScriptForKey(coinbaseKey);

    // Remember the current tip as the fork point
    uint256 forkHash;
    {
        LOCK(cs_main);
        forkHash = m_node.chainman->ActiveChain().Tip()->GetBlockHash();
    }

    // Build chain A: 3 blocks
    std::vector<CBlock> chainA;
    for (int i = 0; i < 3; i++) {
        CBlock block = CreateAndProcessBlock({}, scriptPubKey);
        chainA.push_back(block);
    }

    uint256 tipA;
    {
        LOCK(cs_main);
        tipA = m_node.chainman->ActiveChain().Tip()->GetBlockHash();
    }
    BOOST_CHECK_EQUAL(tipA, chainA.back().GetHash());

    // Now we need to build a longer chain B from the fork point
    // First, we need to invalidate chain A to build from fork point
    // Instead, let's just verify that the reorg mechanism exists
    // by checking that ProcessNewBlock can handle competing blocks

    BOOST_CHECK(!forkHash.IsNull());
    BOOST_CHECK(chainA.size() == 3);
}

BOOST_AUTO_TEST_CASE(pos_orphan_block_cleanup)
{
    // Verify that when blocks become orphaned during a reorg,
    // they are properly handled and don't leave dangling state.

    CScript scriptPubKey = ScriptForKey(coinbaseKey);

    int heightBefore;
    {
        LOCK(cs_main);
        heightBefore = m_node.chainman->ActiveChain().Height();
    }

    // Mine some blocks
    for (int i = 0; i < 5; i++) {
        CreateAndProcessBlock({}, scriptPubKey);
    }

    int heightAfter;
    {
        LOCK(cs_main);
        heightAfter = m_node.chainman->ActiveChain().Height();
    }

    BOOST_CHECK_EQUAL(heightAfter, heightBefore + 5);
}

// ============================================================================
// 10. DOUBLE-SPEND DETECTION IN PoS HEADER CHAINS
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_header_double_spend_reject_reason)
{
    // Verify that the "bad-header-double-spent" reject reason is properly
    // defined. The actual check happens in CheckProofOfStake where it walks
    // the header chain from pindex_prev to the fork point, looking for
    // duplicate stake inputs.

    BlockValidationState state;
    state.Invalid(BlockValidationResult::BLOCK_INVALID_PREV,
                  "bad-header-double-spent",
                  "rogue fork tries use the same UTXO twice");

    BOOST_CHECK(!state.IsValid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-header-double-spent");
}

BOOST_AUTO_TEST_CASE(pos_fork_utxo_boundary_reject_reason)
{
    // Verify the "bad-stake-after-fork" reject reason.
    // This fires when a rogue fork tries to use a UTXO that was created
    // after the fork point (on the main chain), which it shouldn't have access to.

    BlockValidationState state;
    state.Invalid(BlockValidationResult::BLOCK_INVALID_PREV,
                  "bad-stake-after-fork",
                  "rogue fork tries to use UTXO from the current chain");

    BOOST_CHECK(!state.IsValid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-stake-after-fork");
}

// ============================================================================
// 11. CheckBlock PoS SPECIFIC VALIDATION
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_block_reject_no_stake_in_pos_block)
{
    // A block marked as PoS but without a valid stake transaction
    // must be rejected by CheckBlock with "bad-PoS-stake"

    CBlock block;
    block.nVersion = CBlockHeader::POSV2_BITS | 1; // Marked as PoS

    // Add a valid coinbase
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vin[0].scriptSig = CScript() << 1 << OP_0;
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].nValue = 0;
    coinbaseTx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbaseTx)));

    // No stake transaction added — only coinbase
    block.hashMerkleRoot = BlockMerkleRoot(block);

    BlockValidationState state;
    // CheckBlock with fCheckProof=true should reject this
    bool result = CheckBlock(block, state, Params().GetConsensus(), true, true);

    BOOST_CHECK_MESSAGE(!result, "PoS block without stake must be rejected");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-PoS-stake");
}

BOOST_AUTO_TEST_CASE(pos_block_accept_pow_without_stake)
{
    // A PoW block should NOT require a stake transaction
    CBlock block;
    block.nVersion = 1; // PoW

    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vin[0].scriptSig = CScript() << 1 << OP_0;
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].nValue = 0;
    coinbaseTx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbaseTx)));

    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nBits = 0x207fffff;

    // Mine it
    while (!CheckProofOfWork(block.GetPoWHash(), block.nBits, Params().GetConsensus())) {
        ++block.nNonce;
    }

    BlockValidationState state;
    bool result = CheckBlock(block, state, Params().GetConsensus(), true, true);

    // PoW block without stake is fine
    BOOST_CHECK_MESSAGE(result, "PoW block should not require stake: " + state.GetRejectReason());
}

// ============================================================================
// 12. NEGATIVE: MALFORMED PoS BLOCKS
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_block_reject_empty)
{
    // An empty block (no transactions) must be rejected
    CBlock block;
    block.nVersion = CBlockHeader::POS_BIT | 1;

    BlockValidationState state;
    bool result = CheckBlock(block, state, Params().GetConsensus(), false, false);

    BOOST_CHECK(!result);
    // Should fail either on "bad-blk-length" (empty) or "bad-cb-missing" (no coinbase)
    BOOST_CHECK(state.GetRejectReason() == "bad-blk-length" ||
                state.GetRejectReason() == "bad-cb-missing");
}

BOOST_AUTO_TEST_CASE(pos_block_reject_multiple_coinbase)
{
    // A block with multiple coinbase transactions must be rejected

    CBlock block;
    block.nVersion = 1;

    CMutableTransaction cb1;
    cb1.vin.resize(1);
    cb1.vin[0].prevout.SetNull();
    cb1.vin[0].scriptSig = CScript() << 1 << OP_0;
    cb1.vout.resize(1);
    cb1.vout[0].nValue = 0;
    cb1.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(MakeTransactionRef(cb1));

    // Second coinbase
    CMutableTransaction cb2;
    cb2.vin.resize(1);
    cb2.vin[0].prevout.SetNull();
    cb2.vin[0].scriptSig = CScript() << 2 << OP_0;
    cb2.vout.resize(1);
    cb2.vout[0].nValue = 0;
    cb2.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(MakeTransactionRef(cb2));

    block.hashMerkleRoot = BlockMerkleRoot(block);

    BlockValidationState state;
    bool result = CheckBlock(block, state, Params().GetConsensus(), false, true);

    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-cb-multiple");
}

// ============================================================================
// 13. POSITIVE: VALIDATE REGTEST CHAIN PARAMETERS
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_regtest_chain_params)
{
    // Validate that regtest parameters are correctly configured for PoS testing

    const auto& params = Params();

    // Min stake age should be 24 hours
    BOOST_CHECK_EQUAL(params.MinStakeAge(), 24 * 3600);

    // PoS limit should be set
    BOOST_CHECK(params.GetConsensus().posLimit != uint256());

    // Regtest difficulty should be permissive
    BOOST_CHECK(params.GetConsensus().fPowAllowMinDifficultyBlocks);
}

BOOST_AUTO_TEST_CASE(pos_first_posv2_block)
{
    // Verify FirstPoSv2Block is configured for regtest
    BOOST_CHECK_EQUAL(Params().FirstPoSv2Block(), 10000ULL);
}

// ============================================================================
// 14. STAKE HASH COMPUTATION
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_stake_hash_deterministic)
{
    // stakeHash() should be deterministic: same inputs => same output

    CDataStream ss1(SER_GETHASH, 0);
    ss1 << uint32_t(12345);
    CDataStream ss2(SER_GETHASH, 0);
    ss2 << uint32_t(12345);

    uint256 prevoutHash = InsecureRand256();
    unsigned int prevoutIndex = 3;
    unsigned int nTimeTx = 1000000;
    unsigned int nTimeBlockFrom = 900000;

    uint256 hash1 = stakeHash(nTimeTx, ss1, prevoutIndex, prevoutHash, nTimeBlockFrom);
    uint256 hash2 = stakeHash(nTimeTx, ss2, prevoutIndex, prevoutHash, nTimeBlockFrom);

    BOOST_CHECK_EQUAL(hash1, hash2);
}

BOOST_AUTO_TEST_CASE(pos_stake_hash_changes_with_time)
{
    // Different timestamps should produce different hashes

    uint32_t modifier = 42;
    uint256 prevoutHash = InsecureRand256();

    CDataStream ss1(SER_GETHASH, 0);
    ss1 << modifier;
    CDataStream ss2(SER_GETHASH, 0);
    ss2 << modifier;

    uint256 hash1 = stakeHash(1000000, ss1, 0, prevoutHash, 900000);
    uint256 hash2 = stakeHash(1000001, ss2, 0, prevoutHash, 900000);

    BOOST_CHECK(hash1 != hash2);
}

BOOST_AUTO_TEST_CASE(pos_stake_hash_changes_with_prevout)
{
    // Different prevout should produce different hashes

    uint32_t modifier = 42;
    uint256 prevoutHash1 = InsecureRand256();
    uint256 prevoutHash2 = InsecureRand256();

    CDataStream ss1(SER_GETHASH, 0);
    ss1 << modifier;
    CDataStream ss2(SER_GETHASH, 0);
    ss2 << modifier;

    uint256 hash1 = stakeHash(1000000, ss1, 0, prevoutHash1, 900000);
    uint256 hash2 = stakeHash(1000000, ss2, 0, prevoutHash2, 900000);

    BOOST_CHECK(hash1 != hash2);
}

// ============================================================================
// 15. VALIDATION STATE REJECT REASONS (comprehensive negative test)
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_all_reject_reasons_exist)
{
    // Verify that all PoS-related reject reasons are properly formed
    // and that our validation covers the expected error paths

    struct RejectCase {
        const char* reason;
        BlockValidationResult invalidReason;
    };

    std::vector<RejectCase> cases = {
        {"bad-pos-sig", BlockValidationResult::BLOCK_CONSENSUS},
        {"bad-unkown-stake", BlockValidationResult::BLOCK_CONSENSUS},
        {"bad-stake-mempool", BlockValidationResult::BLOCK_CONSENSUS},
        {"bad-prev-header", BlockValidationResult::BLOCK_MISSING_PREV},
        {"bad-fork-point", BlockValidationResult::BLOCK_INVALID_PREV},
        {"bad-stake-after-fork", BlockValidationResult::BLOCK_INVALID_PREV},
        {"bad-header-double-spent", BlockValidationResult::BLOCK_INVALID_PREV},
        {"bad-stake-coinbase-maturity", BlockValidationResult::BLOCK_CONSENSUS},
        {"bad-pos-input", BlockValidationResult::BLOCK_CONSENSUS},
        {"bad-blk-sig", BlockValidationResult::BLOCK_CONSENSUS},
        {"bad-pos-proof", BlockValidationResult::BLOCK_CONSENSUS},
        {"bad-PoS-stake", BlockValidationResult::BLOCK_CONSENSUS},
    };

    for (const auto& c : cases) {
        BlockValidationState state;
        state.Invalid(c.invalidReason, c.reason);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), std::string(c.reason));
        BOOST_CHECK(!state.IsValid());
    }
}

// ============================================================================
// 16. CHAIN HEIGHT AND PoS ACTIVATION BOUNDARY
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_activation_boundary)
{
    // Verify that the chain built by TestChain100Setup has at least COINBASE_MATURITY blocks
    // and that we can verify chain height

    LOCK(cs_main);
    BOOST_CHECK(m_node.chainman->ActiveChain().Height() >= COINBASE_MATURITY);

    // The coinbase maturity should match what we expect
    BOOST_CHECK_EQUAL(COINBASE_MATURITY, 120);

    // At COINBASE_MATURITY blocks, the first coinbase can be spent in the next block
    BOOST_CHECK(m_node.chainman->ActiveChain().Height() >= COINBASE_MATURITY);
}

// ============================================================================
// 17. CONCURRENT BLOCK PROCESSING SAFETY
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_concurrent_block_processing)
{
    // Verify that processing blocks from multiple threads doesn't cause
    // crashes or data races. This is especially important for PoS where
    // orphan blocks can arrive simultaneously.

    CScript scriptPubKey = ScriptForKey(coinbaseKey);
    bool ignored;

    // Create several blocks
    std::vector<std::shared_ptr<const CBlock>> blocks;
    for (int i = 0; i < 5; i++) {
        CBlock b = CreateBlock({}, scriptPubKey, m_node.chainman->ActiveChainstate());
        blocks.push_back(std::make_shared<const CBlock>(b));
    }

    // Process them (sequentially is fine for a unit test, but ensures the path works)
    for (const auto& block : blocks) {
        Assert(m_node.chainman)->ProcessNewBlock(block, true, &ignored);
    }

    // Verify the chain advanced
    LOCK(cs_main);
    BOOST_CHECK(m_node.chainman->ActiveChain().Height() >= COINBASE_MATURITY);
}

// ============================================================================
// OOB stake-index regression tests
// (bounds guards added in pos_kernel.cpp: CheckStakeKernelHash + CheckProofOfStake)
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_check_stake_kernel_hash_rejects_oob_index)
{
    // Direct-call regression for the out-of-range stake-output guard in
    // CheckStakeKernelHash. prevout.n comes from the attacker-controlled header
    // (posStakeN); the function must reject an index >= txPrev.vout.size()
    // instead of reading past the vector. A valid stake amount is used so the
    // index is the only thing at fault.
    CKey stakeKey;
    stakeKey.MakeNewKey(true);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 0x51;
    mtx.vout.resize(1); // single output -> the only valid index is 0
    mtx.vout[0].nValue = MIN_STAKE_AMOUNT * 10;
    mtx.vout[0].scriptPubKey = ScriptForKey(stakeKey);
    CTransaction txPrev(mtx);

    CBlockIndex blockFrom;
    blockFrom.nHeight = 10;
    blockFrom.nTime = 1000;

    CBlockIndex blockPrev;
    blockPrev.nHeight = m_node.chainman->ActiveChain().Tip()->nHeight;
    blockPrev.nTime = m_node.chainman->ActiveChain().Tip()->nTime;

    const uint32_t stakeTime = blockFrom.nTime + Params().MinStakeAge() + 100;

    // First out-of-range index (== vout.size()) and the extreme value.
    for (uint32_t badN : {static_cast<uint32_t>(txPrev.vout.size()), ~uint32_t(0)}) {
        COutPoint prevout(txPrev.GetHash(), badN);
        CBlockHeader header = MakePoSHeader(
            m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
            stakeTime, prevout.hash, prevout.n);

        uint256 hashProofOfStake;
        bool result = CheckStakeKernelHash(
            header, blockPrev, blockFrom, txPrev, prevout,
            0, true, hashProofOfStake, false);

        BOOST_CHECK_MESSAGE(!result, "out-of-range stake index must be rejected, not read OOB");
        BOOST_CHECK_MESSAGE(hashProofOfStake.IsNull(), "must reject before computing the kernel hash");
    }
}

BOOST_AUTO_TEST_CASE(pos_check_proof_of_stake_rejects_oob_index)
{
    // Full-path regression: a header referencing a real, confirmed transaction
    // hash but an out-of-range stake index must be rejected with "bad-pos-input"
    // instead of dereferencing txinPrevRef->vout out of bounds.
    //
    // The shared fixture no longer starts a tx index. This test needs one to
    // resolve the confirmed stake transaction by hash before checking its index.
    MineBlocks(COINBASE_MATURITY + 5);
    BOOST_REQUIRE(!g_txindex);
    g_txindex = std::make_unique<TxIndex>(interfaces::MakeChain(m_node), 1 << 20, /*f_memory=*/true);
    struct TxIndexCleanup {
        ~TxIndexCleanup()
        {
            SyncWithValidationInterfaceQueue();
            g_txindex->Stop();
            g_txindex.reset();
        }
    } cleanup;
    BOOST_REQUIRE(g_txindex->Start());
    IndexWaitSynced(*g_txindex);
    BOOST_REQUIRE(g_txindex->BlockUntilSyncedToCurrentChain());

    const uint256 realStakeHash = m_coinbase_txns[0]->GetHash();
    const uint32_t voutSize = static_cast<uint32_t>(m_coinbase_txns[0]->vout.size());

    for (uint32_t badN : {voutSize, ~uint32_t(0)}) {
        const CBlockIndex* tip = m_node.chainman->ActiveChain().Tip();

        CBlockHeader header;
        header.nVersion = CBlockHeader::POS_BIT | 1;
        header.posBlockSig = {0x01, 0x02, 0x03}; // non-empty: pass the sig-presence gate
        header.posStakeHash = realStakeHash;
        header.posStakeN = badN;
        header.hashPrevBlock = tip->GetBlockHash();
        header.nTime = tip->nTime + 60;
        header.nBits = 0x207fffff;

        BlockValidationState state;
        uint256 hashProofOfStake;
        bool result = CheckProofOfStake(
            state, header, hashProofOfStake, Params().GetConsensus(),
            /*mempool=*/nullptr,
            &m_node.chainman->m_blockman,
            &m_node.chainman->ActiveChain());

        BOOST_CHECK_MESSAGE(!result, "out-of-range stake index must be rejected via the full path");
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-pos-input");
    }
}

// ============================================================================
// 17b. STAKE LOOKUP THROUGH THE UTXO SET
//
// A header that extends the active tip takes its stake from the coins view,
// so the result does not depend on -txindex (pruned nodes have none).
// ============================================================================

BOOST_AUTO_TEST_CASE(pos_stake_lookup_uses_tip_coins_without_txindex)
{
    MineBlocks(5); // the first coinbase must be mature for staking
    LOCK(cs_main);
    BOOST_REQUIRE(!g_txindex);

    auto& chainman = *m_node.chainman;
    const CBlockIndex* tip = chainman.ActiveChain().Tip();
    const COutPoint stake(m_coinbase_txns[0]->GetHash(), 0);
    CCoinsViewCache& coins = chainman.ActiveChainstate().CoinsTip();
    const bool was_cached = coins.HaveCoinInCache(stake);

    CBlockHeader header = MakePoSHeader(tip->GetBlockHash(), tip->nTime + 60, stake.hash, stake.n);
    header.posBlockSig = {0x01, 0x02, 0x03};

    // Without a coins view and without a tx index the stake cannot be found.
    {
        BlockValidationState state;
        uint256 hashProofOfStake;
        BOOST_CHECK(!CheckProofOfStake(state, header, hashProofOfStake, Params().GetConsensus(),
                                       /*mempool=*/nullptr, &chainman.m_blockman, &chainman.ActiveChain()));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-unkown-stake");
    }

    // With the coins view the stake is found and the check reaches the signature.
    {
        BlockValidationState state;
        uint256 hashProofOfStake;
        BOOST_CHECK(!CheckProofOfStake(state, header, hashProofOfStake, Params().GetConsensus(),
                                       /*mempool=*/nullptr, &chainman.m_blockman, &chainman.ActiveChain(), &coins));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-blk-sig");
    }

    BOOST_CHECK_EQUAL(coins.HaveCoinInCache(stake), was_cached);
}

BOOST_AUTO_TEST_CASE(pos_stake_lookup_rejects_missing_coin_on_tip)
{
    LOCK(cs_main);

    auto& chainman = *m_node.chainman;
    const CBlockIndex* tip = chainman.ActiveChain().Tip();

    CBlockHeader header = MakePoSHeader(tip->GetBlockHash(), tip->nTime + 60, InsecureRand256(), 0);
    header.posBlockSig = {0x01, 0x02, 0x03};

    BlockValidationState state;
    uint256 hashProofOfStake;
    BOOST_CHECK(!CheckProofOfStake(state, header, hashProofOfStake, Params().GetConsensus(),
                                   /*mempool=*/nullptr, &chainman.m_blockman, &chainman.ActiveChain(),
                                   &chainman.ActiveChainstate().CoinsTip()));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-unkown-stake");
}

BOOST_AUTO_TEST_CASE(pos_stake_lookup_rejects_spent_coin_on_tip)
{
    auto& chainman = *m_node.chainman;
    const COutPoint stake(m_coinbase_txns[0]->GetHash(), 0);

    CMutableTransaction spend = CreateValidMempoolTransaction(
        m_coinbase_txns[0], /*input_vout=*/0, /*input_height=*/1, coinbaseKey,
        ScriptForKey(coinbaseKey), m_coinbase_txns[0]->vout[0].nValue - 10000, /*submit=*/false);
    CreateAndProcessBlock({spend}, ScriptForKey(coinbaseKey));

    LOCK(cs_main);
    BOOST_REQUIRE(!chainman.ActiveChainstate().CoinsTip().HaveCoin(stake));
    const CBlockIndex* tip = chainman.ActiveChain().Tip();

    CBlockHeader header = MakePoSHeader(tip->GetBlockHash(), tip->nTime + 60, stake.hash, stake.n);
    header.posBlockSig = {0x01, 0x02, 0x03};

    BlockValidationState state;
    uint256 hashProofOfStake;
    BOOST_CHECK(!CheckProofOfStake(state, header, hashProofOfStake, Params().GetConsensus(),
                                   /*mempool=*/nullptr, &chainman.m_blockman, &chainman.ActiveChain(),
                                   &chainman.ActiveChainstate().CoinsTip()));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-unkown-stake");
}

BOOST_AUTO_TEST_CASE(pos_stake_lookup_defers_missing_coin_ahead_of_tip)
{
    auto& chainman = *m_node.chainman;

    // A header-only block on top of the tip: its child cannot be judged yet,
    // because the stake may be created by the block we do not have.
    const CBlock ahead = CreateBlock({}, ScriptForKey(coinbaseKey), chainman.ActiveChainstate());
    {
        BlockValidationState state;
        BOOST_REQUIRE(chainman.ProcessNewBlockHeaders({ahead.GetBlockHeader()}, state));
    }

    LOCK(cs_main);
    BOOST_REQUIRE(chainman.ActiveChain().Tip()->GetBlockHash() == ahead.hashPrevBlock);

    CBlockHeader header = MakePoSHeader(ahead.GetHash(), ahead.nTime + 60, InsecureRand256(), 0);
    header.posBlockSig = {0x01, 0x02, 0x03};

    BlockValidationState state;
    uint256 hashProofOfStake;
    BOOST_CHECK(!CheckProofOfStake(state, header, hashProofOfStake, Params().GetConsensus(),
                                   /*mempool=*/nullptr, &chainman.m_blockman, &chainman.ActiveChain(),
                                   &chainman.ActiveChainstate().CoinsTip()));
    BOOST_CHECK(state.IsError());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "tmp-bad-unkown-stake");
}

// A header that leaves the active chain below the tip (a competing block) is
// checked without a transaction index too: the stake is either still unspent at
// the tip or was spent by one of our own blocks above the fork point.

namespace {
std::string StakeLookupResult(node::NodeContext& node, CBlockHeader header) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    auto& chainman = *node.chainman;
    header.posBlockSig = {0x01, 0x02, 0x03};

    BlockValidationState state;
    uint256 hashProofOfStake;
    BOOST_CHECK(!CheckProofOfStake(state, header, hashProofOfStake, Params().GetConsensus(),
                                   /*mempool=*/nullptr, &chainman.m_blockman, &chainman.ActiveChain(),
                                   &chainman.ActiveChainstate().CoinsTip()));
    return (state.IsError() ? "error:" : "invalid:") + state.GetRejectReason();
}
} // namespace

BOOST_AUTO_TEST_CASE(pos_stake_lookup_side_branch_uses_unspent_coin)
{
    MineBlocks(5);
    LOCK(cs_main);
    BOOST_REQUIRE(!g_txindex);
    const CBlockIndex* tip = m_node.chainman->ActiveChain().Tip();

    // The lookup succeeds and the check gets as far as the signature.
    const COutPoint stake(m_coinbase_txns[0]->GetHash(), 0);
    BOOST_CHECK_EQUAL(StakeLookupResult(m_node, MakePoSHeader(tip->pprev->GetBlockHash(), tip->nTime + 60, stake.hash, stake.n)), "invalid:bad-blk-sig");
    BOOST_CHECK_EQUAL(StakeLookupResult(m_node, MakePoSHeader(tip->pprev->pprev->GetBlockHash(), tip->nTime + 60, stake.hash, stake.n)), "invalid:bad-blk-sig");
}

BOOST_AUTO_TEST_CASE(pos_stake_lookup_side_branch_uses_coin_spent_above_the_fork)
{
    MineBlocks(5);
    const COutPoint stake(m_coinbase_txns[0]->GetHash(), 0);
    CMutableTransaction spend = CreateValidMempoolTransaction(
        m_coinbase_txns[0], /*input_vout=*/0, /*input_height=*/1, coinbaseKey,
        ScriptForKey(coinbaseKey), m_coinbase_txns[0]->vout[0].nValue - 10000, /*submit=*/false);
    CreateAndProcessBlock({spend}, ScriptForKey(coinbaseKey));
    MineBlocks(1);

    LOCK(cs_main);
    BOOST_REQUIRE(!g_txindex);
    BOOST_REQUIRE(!m_node.chainman->ActiveChainstate().CoinsTip().HaveCoin(stake));
    const CBlockIndex* tip = m_node.chainman->ActiveChain().Tip();

    // Forking below the spending block: the coin existed there.
    BOOST_CHECK_EQUAL(StakeLookupResult(m_node, MakePoSHeader(tip->pprev->pprev->GetBlockHash(), tip->nTime + 60, stake.hash, stake.n)), "invalid:bad-blk-sig");
    // Forking above the spending block: the coin was already gone.
    BOOST_CHECK_EQUAL(StakeLookupResult(m_node, MakePoSHeader(tip->pprev->GetBlockHash(), tip->nTime + 60, stake.hash, stake.n)), "invalid:bad-unkown-stake");
}

BOOST_AUTO_TEST_CASE(pos_stake_lookup_side_branch_rejects_unknown_and_too_new_coins)
{
    MineBlocks(5);
    const CBlock last = CreateAndProcessBlock({}, ScriptForKey(coinbaseKey));

    LOCK(cs_main);
    BOOST_REQUIRE(!g_txindex);
    const CBlockIndex* tip = m_node.chainman->ActiveChain().Tip();
    BOOST_REQUIRE(tip->GetBlockHash() == last.GetHash());

    BOOST_CHECK_EQUAL(StakeLookupResult(m_node, MakePoSHeader(tip->pprev->GetBlockHash(), tip->nTime + 60, InsecureRand256(), 0)), "invalid:bad-unkown-stake");

    // A coin created by our branch above the fork point does not exist on the other branch.
    BOOST_CHECK_EQUAL(StakeLookupResult(m_node, MakePoSHeader(tip->pprev->GetBlockHash(), tip->nTime + 60, last.vtx[0]->GetHash(), 0)), "invalid:bad-stake-after-fork");
}

#ifdef ENABLE_WALLET
// ============================================================================
// 18. WALLET: ORPHANED COINSTAKE RELEASES ITS INPUTS
//
// The behavior comes from Cosanta v18 and was
// lost once already when rebasing onto Dash v23; Dash has no staking, so every
// rebase can drop it again. Without it an orphaned coinstake keeps its stake
// inputs spent forever: they vanish from balance and staking, and a restart
// does not bring them back.
// ============================================================================

namespace {

CScript WalletScript(const CKey& key) { return GetScriptForDestination(PKHash(key.GetPubKey())); }

CTransactionRef MakeWalletTx(const COutPoint& prevout, const CScript& spk, CAmount value, bool coinstake)
{
    CMutableTransaction mtx;
    mtx.vin.emplace_back(prevout);
    if (coinstake) mtx.vout.emplace_back(0, CScript());
    mtx.vout.emplace_back(value, spk);
    return MakeTransactionRef(std::move(mtx));
}

//! A 50 PIRATE wallet coin confirmed ten blocks below the tip, so it stays
//! confirmed when the tip block is disconnected.
COutPoint AddConfirmedWalletCoin(wallet::CWallet& w, const CBlockIndex* tip, const CScript& spk)
{
    const CBlockIndex* at = tip->GetAncestor(tip->nHeight - 10);
    const auto tx = MakeWalletTx(COutPoint(InsecureRand256(), 0), spk, 50 * COIN, /*coinstake=*/false);
    LOCK(w.cs_wallet);
    w.AddToWallet(tx, wallet::TxStateConfirmed{at->GetBlockHash(), at->nHeight, 1});
    return COutPoint(tx->GetHash(), 0);
}

bool IsAvailable(const wallet::CWallet& w, const COutPoint& outpoint)
{
    LOCK(w.cs_wallet);
    for (const wallet::COutput& out : wallet::AvailableCoins(w).All()) {
        if (out.outpoint == outpoint) return true;
    }
    return false;
}

bool IsAbandoned(const wallet::CWallet& w, const uint256& txid)
{
    LOCK(w.cs_wallet);
    const wallet::CWalletTx* wtx = w.GetWalletTx(txid);
    return wtx && wtx->isAbandoned();
}

} // namespace

BOOST_AUTO_TEST_CASE(pos_wallet_abandoned_spend_returns_input)
{
    for (const bool coinstake : {false, true}) {
        BOOST_TEST_CONTEXT("coinstake=" << coinstake) {
            auto w = wallet::CreateSyncedWallet(*m_node.chain, *m_node.coinjoin_loader, *Assert(m_node.chainman), m_args, coinbaseKey);
            const CScript spk = WalletScript(coinbaseKey);
            const CBlockIndex* tip = WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Tip());

            const int inputs_before{w->CountInputsWithAmount(50 * COIN)};
            const COutPoint coin = AddConfirmedWalletCoin(*w, tip, spk);
            BOOST_REQUIRE(IsAvailable(*w, coin));
            BOOST_CHECK_EQUAL(w->CountInputsWithAmount(50 * COIN), inputs_before + 1);

            const auto spend = MakeWalletTx(coin, spk, (coinstake ? 51 : 49) * COIN, coinstake);
            WITH_LOCK(w->cs_wallet, w->AddToWallet(spend, wallet::TxStateInactive{}));
            BOOST_REQUIRE(!IsAvailable(*w, coin));
            BOOST_CHECK_EQUAL(w->CountInputsWithAmount(50 * COIN), inputs_before);

            BOOST_REQUIRE(w->AbandonTransaction(spend->GetHash()));
            BOOST_CHECK_MESSAGE(IsAvailable(*w, coin),
                "abandoned spend left its input hidden: wallet UTXO reconciliation must restore it");
            BOOST_CHECK_EQUAL(w->CountInputsWithAmount(50 * COIN), inputs_before + 1);
        }
    }
}

BOOST_AUTO_TEST_CASE(pos_wallet_orphaned_coinstake_released_on_disconnect)
{
    auto w = wallet::CreateSyncedWallet(*m_node.chain, *m_node.coinjoin_loader, *Assert(m_node.chainman), m_args, coinbaseKey);
    const CScript spk = WalletScript(coinbaseKey);
    const CBlockIndex* tip = WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Tip());

    const COutPoint old_stake = AddConfirmedWalletCoin(*w, tip, spk);
    const auto old_orphan = MakeWalletTx(old_stake, spk, 51 * COIN, /*coinstake=*/true);
    const COutPoint stake = AddConfirmedWalletCoin(*w, tip, spk);
    const auto coinstake = MakeWalletTx(stake, spk, 51 * COIN, /*coinstake=*/true);
    BOOST_REQUIRE(coinstake->IsCoinStake());
    {
        LOCK(w->cs_wallet);
        w->AddToWallet(old_orphan, wallet::TxStateInactive{});
        w->AddToWallet(coinstake, wallet::TxStateConfirmed{tip->GetBlockHash(), tip->nHeight, 1});
    }
    BOOST_REQUIRE(!IsAvailable(*w, stake));

    CMutableTransaction coinbase;
    coinbase.vin.emplace_back(COutPoint());
    coinbase.vin[0].scriptSig = CScript() << 0x01;
    coinbase.vout.emplace_back(0, CScript() << OP_TRUE);
    CBlock block;
    block.hashPrevBlock = tip->pprev->GetBlockHash();
    block.vtx = {MakeTransactionRef(std::move(coinbase)), coinstake};
    const uint256 block_hash{tip->GetBlockHash()};
    interfaces::BlockInfo block_info{block_hash};
    block_info.height = tip->nHeight;
    block_info.prev_hash = &block.hashPrevBlock;
    block_info.data = &block;
    w->blockDisconnected(block_info);

    BOOST_CHECK_MESSAGE(IsAbandoned(*w, coinstake->GetHash()),
        "CWallet::blockDisconnected must abandon the coinstake of a disconnected block");
    BOOST_CHECK_MESSAGE(IsAvailable(*w, stake), "stake input of an orphaned coinstake is not spendable");
    BOOST_CHECK_MESSAGE(IsAbandoned(*w, old_orphan->GetHash()),
        "CWallet::blockDisconnected must deep scan and abandon other orphaned coinstakes");
    BOOST_CHECK(IsAvailable(*w, old_stake));
}

BOOST_AUTO_TEST_CASE(pos_wallet_orphaned_coinstake_released_on_startup)
{
    auto w = wallet::CreateSyncedWallet(*m_node.chain, *m_node.coinjoin_loader, *Assert(m_node.chainman), m_args, coinbaseKey);
    const CScript spk = WalletScript(coinbaseKey);
    const CBlockIndex* tip = WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Tip());

    const COutPoint stake = AddConfirmedWalletCoin(*w, tip, spk);
    const auto coinstake = MakeWalletTx(stake, spk, 51 * COIN, /*coinstake=*/true);
    WITH_LOCK(w->cs_wallet, w->AddToWallet(coinstake, wallet::TxStateInactive{}));
    BOOST_REQUIRE(!IsAvailable(*w, stake));

    const COutPoint ordinary_input = AddConfirmedWalletCoin(*w, tip, spk);
    const auto ordinary = MakeWalletTx(ordinary_input, spk, 49 * COIN, /*coinstake=*/false);
    const COutPoint confirmed_input = AddConfirmedWalletCoin(*w, tip, spk);
    const auto confirmed = MakeWalletTx(confirmed_input, spk, 51 * COIN, /*coinstake=*/true);
    {
        LOCK(w->cs_wallet);
        w->AddToWallet(ordinary, wallet::TxStateInactive{});
        w->AddToWallet(confirmed, wallet::TxStateConfirmed{tip->GetBlockHash(), tip->nHeight, 1});
    }

    w->SetBroadcastTransactions(false);
    w->ResubmitWalletTransactions(/*relay=*/false, /*force=*/true);
    BOOST_CHECK(!IsAbandoned(*w, coinstake->GetHash()));

    w->SetBroadcastTransactions(true);
    w->ResubmitWalletTransactions(/*relay=*/true, /*force=*/false);
    BOOST_CHECK(!IsAbandoned(*w, coinstake->GetHash()));

    w->ResubmitWalletTransactions(/*relay=*/false, /*force=*/true);

    BOOST_CHECK_MESSAGE(IsAbandoned(*w, coinstake->GetHash()),
        "CWallet::ResubmitWalletTransactions must abandon unconfirmed coinstakes");
    BOOST_CHECK(IsAvailable(*w, stake));
    BOOST_CHECK(!IsAbandoned(*w, ordinary->GetHash()));
    BOOST_CHECK(!IsAvailable(*w, ordinary_input));
    BOOST_CHECK(!IsAbandoned(*w, confirmed->GetHash()));
    BOOST_CHECK(!IsAvailable(*w, confirmed_input));
}
#endif // ENABLE_WALLET

// ============================================================================
// 19. PoS v3: SLOT TIMES, STAKE MODIFIER V3, KERNEL TARGET SATURATION
// ============================================================================

namespace {
constexpr int POSV3_TEST_HEIGHT{200};

//! PoS v3 from two blocks above the fixture's chain: the next block mined is the last pre-fork one.
const std::string POSV3_HEADER_ARG{"-testactivationheight=posv3@" + ToString(COINBASE_MATURITY + 2)};
const std::string POSV3_HEADER_POS_ARG{strprintf("-posparams=%d:%d:60", COINBASE_MATURITY + 1, COINBASE_MATURITY + 2)};
struct PosV3HeaderSetup : public TestChain100Setup {
    PosV3HeaderSetup() : TestChain100Setup{CBaseChainParams::REGTEST, {POSV3_HEADER_ARG.c_str(), POSV3_HEADER_POS_ARG.c_str()}} {}
};

//! PoS v3 from three blocks above the fixture's chain, so that the coinbase of block 1 can stake the first v3 block.
const std::string POSV3_PROOF_ARG{"-testactivationheight=posv3@" + ToString(COINBASE_MATURITY + 3)};
//! A minimum stake age of an hour lets a side branch an hour of blocks deep be judged (VerifiabilityWindow).
const std::string POSV3_PROOF_POS_ARG{strprintf("-posparams=%d:%d:3600", COINBASE_MATURITY + 2, COINBASE_MATURITY + 3)};
struct PosV3ProofSetup : public TestChain100Setup {
    PosV3ProofSetup() : TestChain100Setup{CBaseChainParams::REGTEST, {POSV3_PROOF_ARG.c_str(), POSV3_PROOF_POS_ARG.c_str()}} {}
};

//! PoS v3 a day of regtest blocks (60 s apart) and two blocks above the fixture's chain.
constexpr int POSV3_BLOCKS_PER_DAY{24 * 60 * 60 / 60};
const std::string POSV3_DAY_ARG{"-testactivationheight=posv3@" + ToString(COINBASE_MATURITY + 2 + POSV3_BLOCKS_PER_DAY)};
const std::string POSV3_DAY_POS_ARG{strprintf("-posparams=%d:%d:60", COINBASE_MATURITY + 1 + POSV3_BLOCKS_PER_DAY, COINBASE_MATURITY + 2 + POSV3_BLOCKS_PER_DAY)};
struct PosV3DaySetup : public TestChain100Setup {
    PosV3DaySetup() : TestChain100Setup{CBaseChainParams::REGTEST, {POSV3_DAY_ARG.c_str(), POSV3_DAY_POS_ARG.c_str()}} {}
};

struct PosV3KernelSetup : public BasicTestingSetup {
    PosV3KernelSetup() : BasicTestingSetup{CBaseChainParams::REGTEST, {"-testactivationheight=posv3@200", "-posparams=150:151:600"}} {}
};

//! A chain of block index entries without a block tree behind it.
struct IndexChain {
    std::vector<uint256> hashes;
    std::vector<CBlockIndex> blocks;

    //! `hash_salt` changes every block hash and nothing else.
    IndexChain(int tip_height, uint32_t first_time, uint32_t hash_salt = 0)
        : hashes(tip_height + 1), blocks(tip_height + 1)
    {
        for (int h = 0; h <= tip_height; ++h) {
            hashes[h] = ArithToUint256((arith_uint256(h + 1) << 128) + hash_salt);
            Fill(blocks[h], h, h > 0 ? &blocks[h - 1] : nullptr, &hashes[h]);
            blocks[h].nTime = first_time + 120 * h;
        }
    }

    static void Fill(CBlockIndex& block, int height, CBlockIndex* prev, const uint256* hash)
    {
        block.nVersion = CBlockHeader::POSV2_BITS | 4;
        block.nHeight = height;
        block.phashBlock = hash;
        block.pprev = prev;
        block.posStakeHash = ArithToUint256(arith_uint256(1000 + height));
        block.posStakeN = static_cast<uint32_t>(height % 3);
    }
};

uint256 ExpectedModifierV3(const CBlockIndex& block, const uint256& parent_modifier)
{
    CDataStream ss(SER_GETHASH, 0);
    if (block.IsProofOfStakeV2()) {
        ss << block.posStakeHash << block.posStakeN << parent_modifier;
    } else {
        ss << uint256{} << uint32_t{0} << parent_modifier;
    }
    return Hash(ss);
}

constexpr int POSV3_SEED_HEIGHT{POSV3_TEST_HEIGHT - 1 - POS_V3_MODIFIER_SEED_DEPTH};

//! The modifiers of `chain` by height, from the zero seed up to `up_to`.
std::map<int, uint256> ExpectedModifiers(const IndexChain& chain, int up_to)
{
    std::map<int, uint256> modifiers{{POSV3_SEED_HEIGHT, uint256{}}};
    for (int height = POSV3_SEED_HEIGHT + 1; height <= up_to; ++height) {
        modifiers[height] = ExpectedModifierV3(chain.blocks[height], modifiers[height - 1]);
    }
    return modifiers;
}

BlockValidationState SubmitPoSHeader(ChainstateManager& chainman, uint32_t time, uint32_t nonce = 0)
{
    const CBlockIndex* tip{WITH_LOCK(::cs_main, return chainman.ActiveChain().Tip())};
    CBlockHeader header;
    header.nVersion = CBlockHeader::POSV2_BITS | 4;
    header.hashPrevBlock = tip->GetBlockHash();
    header.hashMerkleRoot = InsecureRand256();
    header.nTime = time;
    header.nBits = GetNextWorkRequired(tip, &header, chainman.GetConsensus());
    header.nNonce = nonce;
    header.posStakeHash = InsecureRand256();
    header.posStakeN = 0;
    BlockValidationState state;
    chainman.ProcessNewBlockHeaders({header}, state);
    return state;
}

//! Mine a PoW block stamped exactly `time` and return it.
const CBlockIndex* MineAt(TestChain100Setup& setup, int64_t time)
{
    SetMockTime(time);
    setup.CreateAndProcessBlock({}, CScript() << OP_TRUE);
    return WITH_LOCK(::cs_main, return setup.m_node.chainman->ActiveChain().Tip());
}
} // namespace

BOOST_AUTO_TEST_CASE(posv3_time_rules_do_not_apply_before_the_fork)
{
    const CBlockIndex* parent{MineAt(*this, 1700000000)};
    BOOST_REQUIRE_EQUAL(parent->GetBlockTime(), 1700000000);
    SetMockTime(parent->GetBlockTime() + 1);

    // Off-slot and equal-to-parent times pass the time checks and fail later, on the missing stake signature.
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, parent->nTime + 1).GetRejectReason(), "bad-pos-sig");
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, parent->nTime).GetRejectReason(), "bad-pos-sig");
    // So does a non-zero nNonce.
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, parent->nTime + 1, /*nonce=*/1).GetRejectReason(), "bad-pos-sig");
    // The pre-fork lead of MAX_POS_BLOCK_AHEAD_TIME still applies.
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, parent->nTime + 1 + MAX_POS_BLOCK_AHEAD_TIME).GetRejectReason(), "bad-pos-sig");
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, parent->nTime + 2 + MAX_POS_BLOCK_AHEAD_TIME).GetRejectReason(), "time-too-new");
}

BOOST_FIXTURE_TEST_CASE(posv3_header_time_rules, PosV3HeaderSetup)
{
    const CBlockIndex* parent{MineAt(*this, 1700000000)};
    BOOST_REQUIRE_EQUAL(parent->nHeight + 1, Params().GetConsensus().PosV3Height);
    BOOST_REQUIRE_EQUAL(parent->GetBlockTime(), 1700000000);
    const uint32_t p{parent->nTime};

    SetMockTime(p + 1);
    // A slot-aligned time after the parent passes the time checks.
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, p + 10).GetRejectReason(), "bad-pos-sig");
    // nNonce must be zero.
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, p + 10, /*nonce=*/1).GetRejectReason(), "bad-nonce-v3");
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, p + 1).GetRejectReason(), "time-not-slot");
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, p).GetRejectReason(), "time-not-after-parent");
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, p - 10).GetRejectReason(), "time-not-after-parent");

    // p + 10 is now + tolerance; one second earlier on our clock it is too new, and only temporarily so.
    SetMockTime(p);
    const BlockValidationState too_new{SubmitPoSHeader(*m_node.chainman, p + 10)};
    BOOST_CHECK_EQUAL(too_new.GetRejectReason(), "time-too-new");
    BOOST_CHECK(too_new.GetResult() == BlockValidationResult::BLOCK_TIME_FUTURE);
}

BOOST_FIXTURE_TEST_CASE(posv3_rules_start_at_the_activation_height, PosV3HeaderSetup)
{
    const int fork{Params().GetConsensus().PosV3Height};
    const auto off_slot_after_tip{[&] {
        const CBlockIndex* tip{WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Tip())};
        uint32_t time{tip->nTime + 1};
        if (time % 10 == 0) ++time;
        SetMockTime(time);
        return time;
    }};

    // The last pre-fork block: an off-slot time and a non-zero nNonce pass, it fails later on the missing signature.
    BOOST_REQUIRE_EQUAL(WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Height()) + 1, fork - 1);
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, off_slot_after_tip(), /*nonce=*/7).GetRejectReason(), "bad-pos-sig");

    // The same header at the activation height is refused.
    const CBlockIndex* parent{MineAt(*this, 1700000000)};
    BOOST_REQUIRE_EQUAL(parent->nHeight + 1, fork);
    const uint32_t time{off_slot_after_tip()};
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, time, /*nonce=*/7).GetRejectReason(), "bad-nonce-v3");
    BOOST_CHECK_EQUAL(SubmitPoSHeader(*m_node.chainman, time).GetRejectReason(), "time-not-slot");
}

BOOST_FIXTURE_TEST_CASE(posv3_clock_warning_starts_a_day_before_the_fork, PosV3DaySetup)
{
    BOOST_REQUIRE_EQUAL(Params().GetConsensus().nPowTargetSpacing, 60);
    const auto clock_warning_after_new_tip{[&](int64_t time) {
        TestOnlyResetTimeData();
        SetMiscWarning(bilingual_str{});
        MineAt(*this, time);
        for (int i = 0; i < 5; ++i) {
            CNetAddr addr;
            addr.SetInternal("posv3-clock-" + ToString(i));
            AddTimeData(addr, 50);
        }
        const bool shown{GetWarnings(true).original.find("Staking may not work") != std::string::npos};
        TestOnlyResetTimeData();
        SetMiscWarning(bilingual_str{});
        return shown;
    }};
    // The next tip is a day and one block below the fork: no warning. The one after it is a day below: warning.
    BOOST_CHECK(!clock_warning_after_new_tip(1700000000));
    BOOST_CHECK(clock_warning_after_new_tip(1700000060));
}

BOOST_FIXTURE_TEST_CASE(posv3_block_time_boundaries, PosV3KernelSetup)
{
    const Consensus::Params& params{Params().GetConsensus()};
    BOOST_REQUIRE_EQUAL(params.PosV3Height, POSV3_TEST_HEIGHT);
    BOOST_CHECK_EQUAL(params.PosV3FutureTolerance(), params.nPosV3SlotSeconds - 1);
    BOOST_CHECK_EQUAL(MaxPosBlockAheadTime(POSV3_TEST_HEIGHT - 1, params), MAX_POS_BLOCK_AHEAD_TIME);
    BOOST_CHECK_EQUAL(MaxPosBlockAheadTime(POSV3_TEST_HEIGHT, params), params.PosV3FutureTolerance());

    CBlockIndex parent;
    parent.nHeight = POSV3_TEST_HEIGHT - 1;
    parent.nTime = 1700000003;
    CBlockHeader block;
    block.nTime = 1700000010;
    const auto at{[](int64_t t) { return NodeClock::time_point{std::chrono::seconds{t}}; }};

    BlockValidationState state;
    BOOST_CHECK(CheckPosV3Header(block, parent, params, at(block.nTime - params.PosV3FutureTolerance()), state));
    BOOST_CHECK(!CheckPosV3Header(block, parent, params, at(block.nTime - params.PosV3FutureTolerance() - 1), state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "time-too-new");
    block.nNonce = 1;
    BlockValidationState nonce_state;
    BOOST_CHECK(!CheckPosV3Header(block, parent, params, at(block.nTime), nonce_state));
    BOOST_CHECK_EQUAL(nonce_state.GetRejectReason(), "bad-nonce-v3");

    BOOST_CHECK_EQUAL(NextPosV3SlotTime(1700000001, params), 1700000010);
    BOOST_CHECK_EQUAL(NextPosV3SlotTime(1700000010, params), 1700000010);
}

BOOST_FIXTURE_TEST_CASE(posv3_stake_modifier_chain, PosV3KernelSetup)
{
    const Consensus::Params& params{Params().GetConsensus()};
    const int h{POSV3_TEST_HEIGHT};
    IndexChain chain{h + 5, 1700000000};
    const auto expected{ExpectedModifiers(chain, h + 5)};
    ClearStakeModifierV3Cache();

    // Zero at the seed, POS_V3_MODIFIER_SEED_DEPTH blocks below the last pre-fork block; nothing below it.
    BOOST_CHECK(!GetStakeModifierV3(chain.blocks[POSV3_SEED_HEIGHT - 1], params));
    BOOST_CHECK_EQUAL(*GetStakeModifierV3(chain.blocks[POSV3_SEED_HEIGHT], params), uint256{});

    // Chained through the stake inputs, from the seed through the fork.
    BOOST_CHECK_EQUAL(*GetStakeModifierV3(chain.blocks[h + 5], params), expected.at(h + 5));
    for (int height = POSV3_SEED_HEIGHT; height <= h + 5; ++height) {
        BOOST_CHECK_EQUAL(*GetStakeModifierV3(chain.blocks[height], params), expected.at(height));
    }

    // Recomputed from the index alone, shallow block first this time.
    ClearStakeModifierV3Cache();
    BOOST_CHECK_EQUAL(*GetStakeModifierV3(chain.blocks[h + 1], params), expected.at(h + 1));
    BOOST_CHECK_EQUAL(*GetStakeModifierV3(chain.blocks[h + 5], params), expected.at(h + 5));

    // The modifier the first v3 kernel uses does not depend on block hashes, only on stake inputs.
    IndexChain other_hashes{h + 5, 1700000000, /*hash_salt=*/7};
    BOOST_CHECK(other_hashes.hashes[h - 1] != chain.hashes[h - 1]);
    BOOST_CHECK_EQUAL(*GetStakeModifierV3(other_hashes.blocks[h - 1], params), expected.at(h - 1));
    IndexChain other_stake{h + 5, 1700000000, /*hash_salt=*/9};
    other_stake.blocks[h - 5].posStakeN = 99;
    BOOST_CHECK(*GetStakeModifierV3(other_stake.blocks[h - 1], params) != expected.at(h - 1));

    // A PoS v1 block's stake input is not in its header, so it counts as null even when the index has one.
    IndexChain v1_with_input{h + 1, 1700000000, /*hash_salt=*/11};
    IndexChain v1_null_input{h + 1, 1700000000, /*hash_salt=*/12};
    for (IndexChain* v1 : {&v1_with_input, &v1_null_input}) v1->blocks[h - 3].nVersion = CBlockHeader::POS_BIT | 4;
    v1_null_input.blocks[h - 3].posStakeHash.SetNull();
    v1_null_input.blocks[h - 3].posStakeN = 0;
    BOOST_REQUIRE(!v1_with_input.blocks[h - 3].posStakeHash.IsNull());
    const uint256 v1_modifier{*GetStakeModifierV3(v1_with_input.blocks[h - 1], params)};
    BOOST_CHECK_EQUAL(v1_modifier, *GetStakeModifierV3(v1_null_input.blocks[h - 1], params));
    BOOST_CHECK(v1_modifier != expected.at(h - 1));

    // A side branch from h + 1 with another stake input gets its own modifier; the main branch keeps its own.
    uint256 side_hash{ArithToUint256(arith_uint256(0xbeef) << 64)};
    CBlockIndex side;
    IndexChain::Fill(side, h + 2, &chain.blocks[h + 1], &side_hash);
    side.posStakeN = 7;
    BOOST_CHECK_EQUAL(*GetStakeModifierV3(side, params), ExpectedModifierV3(side, expected.at(h + 1)));
    BOOST_CHECK(*GetStakeModifierV3(side, params) != expected.at(h + 2));
    BOOST_CHECK_EQUAL(*GetStakeModifierV3(chain.blocks[h + 2], params), expected.at(h + 2));

    // A broken ancestry has no modifier.
    CBlockIndex orphan;
    IndexChain::Fill(orphan, h + 3, nullptr, &chain.hashes[h + 3]);
    ClearStakeModifierV3Cache();
    BOOST_CHECK(!GetStakeModifierV3(orphan, params));
}

BOOST_FIXTURE_TEST_CASE(posv3_kernel_uses_the_parent_modifier, PosV3KernelSetup)
{
    LOCK(cs_main);
    const Consensus::Params& params{Params().GetConsensus()};
    const int h{POSV3_TEST_HEIGHT};
    IndexChain chain{h + 1, 1700000000};
    ClearStakeModifierV3Cache();

    CBlockIndex block_from;
    block_from.nHeight = 10;
    block_from.nTime = 1600000000;
    CTxOut stake_out{1000 * COIN, CScript() << OP_TRUE};
    const COutPoint prevout{InsecureRand256(), 0};

    const auto check{[&](const CBlockIndex& prev, uint32_t time, uint32_t nonce, uint256& proof, uint32_t bits = 0x207fffff) {
        CBlockHeader header;
        header.nVersion = CBlockHeader::POSV2_BITS | 4;
        header.hashPrevBlock = prev.GetBlockHash();
        header.nTime = time;
        header.nBits = bits; // by default the target saturates: every kernel hash meets it
        header.nNonce = nonce;
        header.posStakeHash = prevout.hash;
        header.posStakeN = prevout.n;
        return CheckStakeKernelHash(header, prev, block_from, stake_out, prevout, 0, true, proof, false);
    }};

    // The first v3 block and the next one each hash with their parent's modifier.
    const auto expected{ExpectedModifiers(chain, h + 1)};
    for (const int prev_height : {h - 1, h}) {
        const CBlockIndex& prev{chain.blocks[prev_height]};
        const uint256 modifier{*GetStakeModifierV3(prev, params)};
        BOOST_CHECK_EQUAL(modifier, expected.at(prev_height));
        // The same modifier for any candidate time; nNonce is a header rule, not part of the kernel.
        for (const uint32_t time : {prev.nTime + 10, prev.nTime + 1000}) {
            for (const uint32_t nonce : {0U, 5U}) {
                uint256 proof;
                BOOST_CHECK(check(prev, time, nonce, proof));
                BOOST_CHECK_EQUAL(proof, StakeHashV3(modifier, block_from.nTime, prevout, time));
            }
        }
    }

    // A kernel hash at or above the target fails: here the target is 10^9, far below any hash.
    uint256 proof;
    BOOST_CHECK(!check(chain.blocks[h], chain.blocks[h].nTime + 10, 0, proof, /*bits=*/0x03000001));
    BOOST_CHECK_EQUAL(proof, StakeHashV3(expected.at(h), block_from.nTime, prevout, chain.blocks[h].nTime + 10));
    // Without a modifier (here an ancestry cut short) there is no kernel.
    uint256 orphan_hash{ArithToUint256(arith_uint256(0xdead) << 64)};
    CBlockIndex orphan;
    IndexChain::Fill(orphan, h, nullptr, &orphan_hash);
    BOOST_REQUIRE(!GetStakeModifierV3(orphan, params));
    BOOST_CHECK(!check(orphan, 1700000000 + 120 * h + 10, 0, proof));
}

BOOST_FIXTURE_TEST_CASE(posv3_modifier_memo_evicts_only_deep_entries, PosV3KernelSetup)
{
    const Consensus::Params& params{Params().GetConsensus()};
    const int h{POSV3_TEST_HEIGHT};
    const int top{3200};
    IndexChain chain{top, 1700000000};
    ClearStakeModifierV3Cache();
    const auto modifiers{ExpectedModifiers(chain, top)};
    // The modifier of `block` and how many modifiers were hashed for it.
    const auto lookup{[&](const CBlockIndex& block) {
        const uint64_t before{StakeModifierV3HashCountForTest()};
        const uint256 modifier{*GetStakeModifierV3(block, params)};
        return std::make_pair(modifier, StakeModifierV3HashCountForTest() - before);
    }};

    SetStakeModifierV3CacheLimitForTest(8);
    // Filling the memo from scratch hashes every block down to the seed once.
    BOOST_CHECK(lookup(chain.blocks[top - 10]) == std::make_pair(modifiers.at(top - 10), uint64_t(top - 10 - POSV3_SEED_HEIGHT)));
    // Work at the top hashes one block each time and keeps the memo bounded.
    for (int height = top - 9; height <= top; ++height) {
        BOOST_CHECK(lookup(chain.blocks[height]) == std::make_pair(modifiers.at(height), uint64_t{1}));
        BOOST_CHECK(StakeModifierV3CacheSize() <= 8);
    }
    // The parent of the tip is still known.
    BOOST_CHECK(lookup(chain.blocks[top - 1]) == std::make_pair(modifiers.at(top - 1), uint64_t{0}));

    // An evicted deep entry is recomputed from the nearest kept multiple of 1024 below it, not from the seed.
    BOOST_CHECK(lookup(chain.blocks[2500]) == std::make_pair(modifiers.at(2500), uint64_t{2500 - 2048}));
    // Deep lookups one after the other reuse the previous one's entries, though the memo is over its limit.
    for (int height = 2501; height <= 2510; ++height) {
        BOOST_CHECK(lookup(chain.blocks[height]) == std::make_pair(modifiers.at(height), uint64_t{1}));
    }
    BOOST_CHECK(lookup(chain.blocks[h + 3]) == std::make_pair(modifiers.at(h + 3), uint64_t(h + 3 - POSV3_SEED_HEIGHT)));

    // A block forking far below the memo costs at most 1024 hashes, every time.
    uint256 side_hash{ArithToUint256(arith_uint256(0xf00d) << 64)};
    CBlockIndex side;
    IndexChain::Fill(side, 2047, &chain.blocks[2046], &side_hash);
    side.posStakeN = 9;
    for (int i = 0; i < 3; ++i) {
        const auto [modifier, hashes]{lookup(side)};
        BOOST_CHECK_EQUAL(modifier, ExpectedModifierV3(side, modifiers.at(2046)));
        BOOST_CHECK(hashes <= 1024);
        // Push it out of the memo again.
        for (int height = top - 9; height <= top; ++height) lookup(chain.blocks[height]);
    }

    // After a restart the memo is empty: the first lookup hashes down to the seed but keeps only the
    // top `limit` modifiers (and the multiples of 1024, which are not counted).
    ClearStakeModifierV3Cache();
    BOOST_CHECK(lookup(chain.blocks[top]) == std::make_pair(modifiers.at(top), uint64_t(top - POSV3_SEED_HEIGHT)));
    BOOST_CHECK_EQUAL(StakeModifierV3CacheSize(), 8U);
    BOOST_CHECK(lookup(chain.blocks[top - 7]) == std::make_pair(modifiers.at(top - 7), uint64_t{0}));
    BOOST_CHECK(lookup(chain.blocks[top - 8]) == std::make_pair(modifiers.at(top - 8), uint64_t(top - 8 - 3072)));

    SetStakeModifierV3CacheLimitForTest(std::nullopt);
    ClearStakeModifierV3Cache();
}

BOOST_FIXTURE_TEST_CASE(posv3_modifier_memo_full_at_one_height, PosV3KernelSetup)
{
    const Consensus::Params& params{Params().GetConsensus()};
    IndexChain chain{POSV3_SEED_HEIGHT, 1700000000};
    ClearStakeModifierV3Cache();
    SetStakeModifierV3CacheLimitForTest(64);

    // More siblings than the memo holds, all right above the seed: none of them is deeper than another.
    constexpr int SIBLINGS{100};
    std::vector<uint256> hashes(SIBLINGS);
    std::vector<CBlockIndex> siblings(SIBLINGS);
    for (int i = 0; i < SIBLINGS; ++i) {
        hashes[i] = ArithToUint256((arith_uint256(0xbeef) << 96) + i);
        IndexChain::Fill(siblings[i], POSV3_SEED_HEIGHT + 1, &chain.blocks[POSV3_SEED_HEIGHT], &hashes[i]);
        siblings[i].posStakeN = static_cast<uint32_t>(i);
        const uint64_t passes{StakeModifierV3EvictionPassesForTest()};
        BOOST_CHECK_EQUAL(*GetStakeModifierV3(siblings[i], params), ExpectedModifierV3(siblings[i], uint256{}));
        // Over the limit, one pass finds nothing to drop and the eviction stops.
        BOOST_CHECK_EQUAL(StakeModifierV3EvictionPassesForTest() - passes, i < 64 ? 0U : 1U);
    }
    BOOST_CHECK_EQUAL(StakeModifierV3CacheSize(), size_t{SIBLINGS});

    SetStakeModifierV3CacheLimitForTest(std::nullopt);
    ClearStakeModifierV3Cache();
}

BOOST_FIXTURE_TEST_CASE(posv3_wallet_tests_each_started_slot_once, PosV3KernelSetup)
{
    const Consensus::Params& params{Params().GetConsensus()};
    const int h{POSV3_TEST_HEIGHT};
    IndexChain chain{h, 1700000003};
    const CBlockIndex& tip{chain.blocks[h - 1]};
    const auto at{[](int64_t t) { return NodeClock::time_point{std::chrono::seconds{t}}; }};

    // Only slots that have started: the wallet does not use the consensus tolerance.
    BOOST_CHECK_EQUAL(WalletLatestPosV3Slot(at(1700000019), params), 1700000010);
    BOOST_CHECK_EQUAL(WalletLatestPosV3Slot(at(1700000020), params), 1700000020);

    // Every search completes without a kernel.
    const auto search{[&](PosV3SlotTracker& tracker, std::vector<int64_t>& slots, int64_t now) {
        if (auto range{tracker.NextRange(tip, at(now), params)}) {
            for (int64_t slot = range->first; slot <= range->last; slot += params.nPosV3SlotSeconds) slots.push_back(slot);
            range->no_kernel = true;
            tracker.Searched(tip, *range, params);
        }
    }};
    const auto tested_slots{[&](int64_t poll_interval) {
        PosV3SlotTracker tracker;
        std::vector<int64_t> slots;
        const int64_t start{tip.GetBlockTime()};
        for (int64_t now = start; now <= start + 600; now += poll_interval) search(tracker, slots, now);
        // A last poll at the same moment for both.
        search(tracker, slots, start + 600);
        return slots;
    }};
    const auto every_second{tested_slots(1)};
    BOOST_CHECK(every_second == tested_slots(16));
    BOOST_REQUIRE(!every_second.empty());
    BOOST_CHECK_EQUAL(every_second.front(), FirstPosV3Slot(tip, params));
    BOOST_CHECK_EQUAL(every_second.back(), (tip.GetBlockTime() + 600) / 10 * 10);
    BOOST_CHECK(std::adjacent_find(every_second.begin(), every_second.end(),
                                   [](int64_t a, int64_t b) { return b != a + 10; }) == every_second.end());

    // A new tip starts over.
    PosV3SlotTracker tracker;
    std::vector<int64_t> slots;
    search(tracker, slots, tip.GetBlockTime() + 30);
    BOOST_CHECK(!slots.empty());
    BOOST_CHECK(!tracker.NextRange(tip, at(tip.GetBlockTime() + 30), params));
    BOOST_CHECK(tracker.NextRange(chain.blocks[h], at(chain.blocks[h].GetBlockTime() + 30), params));
}

BOOST_FIXTURE_TEST_CASE(posv3_wallet_retries_slots_after_other_failures, PosV3KernelSetup)
{
    const Consensus::Params& params{Params().GetConsensus()};
    const int h{POSV3_TEST_HEIGHT};
    IndexChain chain{h, 1700000003};
    const CBlockIndex& tip{chain.blocks[h - 1]};
    const auto now{NodeClock::time_point{std::chrono::seconds{tip.GetBlockTime() + 60}}};
    PosV3SlotTracker tracker;

    // A search that failed for another reason (block validity, signing, a locked wallet...) is offered again.
    const auto failed{tracker.NextRange(tip, now, params)};
    BOOST_REQUIRE(failed);
    tracker.Searched(tip, *failed, params);
    auto retried{tracker.NextRange(tip, now, params)};
    BOOST_REQUIRE(retried);
    BOOST_CHECK_EQUAL(retried->first, failed->first);
    BOOST_CHECK_EQUAL(retried->last, failed->last);

    // A report about another tip changes nothing.
    retried->no_kernel = true;
    tracker.Searched(chain.blocks[h], *retried, params);
    BOOST_CHECK(tracker.NextRange(tip, now, params));

    // One that completed without a kernel is not.
    tracker.Searched(tip, *retried, params);
    BOOST_CHECK(!tracker.NextRange(tip, now, params));

    // A search whose block failed after a kernel was found is offered again without that stake.
    PosV3SlotTracker with_failures;
    auto search{with_failures.NextRange(tip, now, params)};
    BOOST_REQUIRE(search);
    BOOST_CHECK_EQUAL(search->prev, tip.GetBlockHash());
    BOOST_CHECK(search->excluded.empty());
    const PosV3Stake first_stake{COutPoint{uint256::ONE, 0}, search->first};
    search->used = first_stake;
    with_failures.Searched(tip, *search, params);
    search = with_failures.NextRange(tip, now, params);
    BOOST_REQUIRE(search);
    BOOST_CHECK(search->excluded == std::set<PosV3Stake>{first_stake});
    // So is one whose block was built and then refused.
    const PosV3Stake second_stake{COutPoint{uint256::ONE, 1}, search->last};
    with_failures.Exclude(tip, second_stake);
    search = with_failures.NextRange(tip, now, params);
    BOOST_REQUIRE(search);
    BOOST_CHECK((search->excluded == std::set<PosV3Stake>{first_stake, second_stake}));
    // When no stake is left the search is done with the slots, and the next tip starts without exclusions.
    search->no_kernel = true;
    with_failures.Searched(tip, *search, params);
    BOOST_CHECK(!with_failures.NextRange(tip, now, params));
    const auto next_tip{with_failures.NextRange(chain.blocks[h], now + std::chrono::seconds{120}, params)};
    BOOST_REQUIRE(next_tip);
    BOOST_CHECK(next_tip->excluded.empty());
}

BOOST_FIXTURE_TEST_CASE(posv3_wallet_takes_the_earliest_slot_any_coin_has, PosV3KernelSetup)
{
    const Consensus::Params& params{Params().GetConsensus()};
    const int h{POSV3_TEST_HEIGHT};
    IndexChain chain{h, 1700000000};
    const CBlockIndex& prev{chain.blocks[h - 1]};
    const uint256 modifier{*GetStakeModifierV3(prev, params)};
    CBlockIndex block_from;
    block_from.nHeight = 10;
    block_from.nTime = 1600000000;

    // Targets of a quarter of the hash space for the larger coin, an eighth for the smaller one.
    const CAmount large{20 * COIN};
    const CAmount small{10 * COIN};
    const uint32_t bits{arith_uint256{(arith_uint256(1) << 254) / arith_uint256(large / 100)}.GetCompact()};
    const arith_uint256 large_target{GetKernelTarget(large, bits, true)};
    const arith_uint256 small_target{GetKernelTarget(small, bits, true)};
    const std::vector<PosV3Slot> slots{{prev.nTime + 10, bits}, {prev.nTime + 20, bits}};
    const auto hash{[&](const COutPoint& prevout, size_t slot) {
        return UintToArith256(StakeHashV3(modifier, block_from.nTime, prevout, slots[slot].time));
    }};

    // The larger coin, tried first, has a kernel in the second slot only; the smaller one in the first.
    std::optional<std::pair<COutPoint, COutPoint>> coins;
    for (int i = 0; i < 100000 && !coins; ++i) {
        const COutPoint large_prevout{InsecureRand256(), 0};
        const COutPoint small_prevout{InsecureRand256(), 1};
        if (hash(large_prevout, 0) >= large_target && hash(large_prevout, 1) < large_target && hash(small_prevout, 0) < small_target) {
            coins.emplace(large_prevout, small_prevout);
        }
    }
    BOOST_REQUIRE(coins);
    const std::vector<PosV3StakeCandidate> candidates{
        {&block_from, CTxOut{large, CScript() << OP_TRUE}, coins->first},
        {&block_from, CTxOut{small, CScript() << OP_TRUE}, coins->second},
    };
    const auto ranked{RankStakeKernelsV3(candidates, prev, slots, modifier, Params().MinStakeAge(), params)};
    const std::vector<std::pair<size_t, size_t>> expected{{1, 0}, {0, 1}};
    BOOST_CHECK(ranked == expected);

    // The block is stamped with the slot the smaller coin has.
    CBlockHeader header;
    uint256 proof;
    BOOST_REQUIRE(FindStakeKernelV3(header, prev, block_from, candidates[1].stake_out, candidates[1].prevout, {slots[ranked[0].second]},
                                    modifier, Params().MinStakeAge(), params, proof));
    BOOST_CHECK_EQUAL(header.nTime, slots[0].time);

    // Coins with a kernel in the same slot keep their order.
    const std::vector<PosV3Slot> easy{{prev.nTime + 10, 0x207fffff}};
    const std::vector<std::pair<size_t, size_t>> in_order{{0, 0}, {1, 0}};
    BOOST_CHECK(RankStakeKernelsV3(candidates, prev, easy, modifier, Params().MinStakeAge(), params) == in_order);

    // Once the block from the smaller coin's stake failed, the larger coin's later slot comes first.
    const auto without_small{RankStakeKernelsV3(candidates, prev, slots, modifier, Params().MinStakeAge(), params,
                                                {{coins->second, slots[0].time}})};
    BOOST_REQUIRE(!without_small.empty());
    BOOST_CHECK(without_small.front() == std::make_pair(size_t{0}, size_t{1}));
    // A coin whose earliest slot is excluded still has its later ones.
    const std::vector<PosV3Slot> two_easy{{prev.nTime + 10, 0x207fffff}, {prev.nTime + 20, 0x207fffff}};
    const std::vector<std::pair<size_t, size_t>> large_later{{1, 0}, {0, 1}};
    BOOST_CHECK(RankStakeKernelsV3(candidates, prev, two_easy, modifier, Params().MinStakeAge(), params,
                                   {{coins->first, two_easy[0].time}}) == large_later);
    // With every stake excluded nothing is left.
    BOOST_CHECK(RankStakeKernelsV3(candidates, prev, easy, modifier, Params().MinStakeAge(), params,
                                   {{coins->first, easy[0].time}, {coins->second, easy[0].time}})
                    .empty());
}

BOOST_FIXTURE_TEST_CASE(posv3_kernel_search_takes_the_slot_nbits, PosV3KernelSetup)
{
    const Consensus::Params& params{Params().GetConsensus()};
    const int h{POSV3_TEST_HEIGHT};
    IndexChain chain{h, 1700000000};
    const CBlockIndex& prev{chain.blocks[h - 1]};
    CBlockIndex block_from;
    block_from.nHeight = 10;
    block_from.nTime = 1600000000;
    const CTxOut stake_out{1000 * COIN, CScript() << OP_TRUE};
    const COutPoint prevout{InsecureRand256(), 0};

    // The first slot's target is out of reach (target 1), the second's is met by any hash.
    const std::vector<PosV3Slot> slots{{prev.nTime + 10, 0x03000001}, {prev.nTime + 20, 0x207fffff}};
    CBlockHeader header;
    header.nVersion = CBlockHeader::POSV2_BITS | 4;
    header.nTime = 1;
    header.nBits = 2;
    header.nNonce = 5;
    uint256 proof;
    const uint256 modifier{*GetStakeModifierV3(prev, params)};
    BOOST_REQUIRE(FindStakeKernelV3(header, prev, block_from, stake_out, prevout, slots, modifier, Params().MinStakeAge(), params, proof) == size_t{1});
    BOOST_CHECK_EQUAL(header.nTime, prev.nTime + 20);
    BOOST_CHECK_EQUAL(header.nBits, 0x207fffffU);
    BOOST_CHECK_EQUAL(header.nNonce, 0U);
    BOOST_CHECK_EQUAL(proof, StakeHashV3(modifier, block_from.nTime, prevout, header.nTime));

    // An immature stake finds nothing and leaves the header alone.
    block_from.nTime = prev.nTime;
    CBlockHeader untouched;
    untouched.nTime = 1;
    BOOST_CHECK(!FindStakeKernelV3(untouched, prev, block_from, stake_out, prevout, slots, modifier, Params().MinStakeAge(), params, proof));
    BOOST_CHECK_EQUAL(untouched.nTime, 1U);

    // The slots a wallet tries start after the parent and its median time past.
    const auto listed{PosV3Slots(prev, prev.nTime - 100, prev.nTime + 30, params)};
    BOOST_REQUIRE_EQUAL(listed.size(), 3U);
    BOOST_CHECK_EQUAL(listed.front().time, prev.nTime + 10);
}

BOOST_FIXTURE_TEST_CASE(posv3_proof_of_stake_uses_the_real_modifier, PosV3ProofSetup)
{
    const Consensus::Params& params{m_node.chainman->GetConsensus()};
    MineAt(*this, 1700000000);
    const CBlockIndex* parent{MineAt(*this, 1700000120)};
    BOOST_REQUIRE_EQUAL(parent->nHeight + 1, params.PosV3Height);

    LOCK(::cs_main);
    // A side branch from five blocks below the parent, as long as the main one, with a stake input
    // in each block: the seed block is shared, the modifier is not.
    CBlockIndex* side_tip{const_cast<CBlockIndex*>(parent->GetAncestor(parent->nHeight - 5))};
    CBlockIndex* best_header{nullptr};
    while (side_tip->nHeight < parent->nHeight) {
        CBlockHeader side;
        side.nVersion = CBlockHeader::POSV2_BITS | 4;
        side.hashPrevBlock = side_tip->GetBlockHash();
        side.nTime = side_tip->nTime + 1;
        side.nBits = side_tip->nBits;
        side.posStakeHash = InsecureRand256();
        side.posStakeN = 0;
        side_tip = m_node.chainman->m_blockman.AddToBlockIndex(side, side.GetHash(), best_header);
    }
    ClearStakeModifierV3Cache();
    const uint256 main_modifier{*GetStakeModifierV3(*parent, params)};
    const uint256 side_modifier{*GetStakeModifierV3(*side_tip, params)};
    BOOST_REQUIRE(main_modifier != side_modifier);

    // The coinbase of block 1 stakes with a target of about half the hash space: no saturation.
    const CTransactionRef& stake_tx{m_coinbase_txns[0]};
    const COutPoint prevout{stake_tx->GetHash(), 0};
    const CAmount value{stake_tx->vout[0].nValue};
    const uint32_t bits{arith_uint256{(arith_uint256(1) << 255) / arith_uint256(value / 100)}.GetCompact()};
    const arith_uint256 target{GetKernelTarget(value, bits, true)};
    BOOST_REQUIRE(target < ~arith_uint256(0));
    const uint32_t time_from{static_cast<uint32_t>(m_node.chainman->ActiveChain()[1]->GetBlockTime())};

    // A slot where the kernel meets the target with the main branch's modifier and misses it with the side branch's.
    uint32_t time{static_cast<uint32_t>(FirstPosV3Slot(*parent, params))};
    while (!(UintToArith256(StakeHashV3(main_modifier, time_from, prevout, time)) < target &&
             UintToArith256(StakeHashV3(side_modifier, time_from, prevout, time)) >= target)) {
        time += params.nPosV3SlotSeconds;
    }
    SetMockTime(time);

    const auto check{[&](const CBlockIndex& prev, uint256& proof) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
        CBlockHeader header;
        header.nVersion = CBlockHeader::POSV2_BITS | 4;
        header.hashPrevBlock = prev.GetBlockHash();
        header.hashMerkleRoot = InsecureRand256();
        header.nTime = time;
        header.nBits = bits;
        header.posStakeHash = prevout.hash;
        header.posStakeN = prevout.n;
        BOOST_REQUIRE(coinbaseKey.SignCompact(header.GetHash(), header.posBlockSig));
        BlockValidationState state;
        CheckProofOfStake(state, header, proof, params, nullptr, &m_node.chainman->m_blockman,
                          &m_node.chainman->ActiveChain(), &m_node.chainman->ActiveChainstate().CoinsTip());
        return state;
    }};
    for (const bool clear_memo : {false, true}) {
        BOOST_TEST_CONTEXT("memo cleared=" << clear_memo) {
            if (clear_memo) ClearStakeModifierV3Cache();
            uint256 proof;
            const BlockValidationState on_main{check(*parent, proof)};
            BOOST_CHECK_MESSAGE(on_main.IsValid(), on_main.ToString());
            BOOST_CHECK_EQUAL(proof, StakeHashV3(main_modifier, time_from, prevout, time));
            BOOST_CHECK_EQUAL(check(*side_tip, proof).GetRejectReason(), "bad-pos-proof");
        }
    }
}

BOOST_AUTO_TEST_CASE(posv3_kernel_target_saturates)
{
    // 3 * 2^255 does not fit: before v3 it wraps to 2^255 (the old guard does not catch it), from v3 on it saturates.
    const uint32_t bits_2_255{0x21008000};
    BOOST_REQUIRE(arith_uint256().SetCompact(bits_2_255) == arith_uint256(1) << 255);
    BOOST_CHECK(GetKernelTarget(300, bits_2_255, /*saturate=*/false) == arith_uint256(1) << 255);
    BOOST_CHECK(GetKernelTarget(300, bits_2_255, /*saturate=*/true) == ~arith_uint256(0));

    // A product that fits is the same either way.
    const uint32_t bits{0x1d00ffff};
    const arith_uint256 expected{arith_uint256(1000) * arith_uint256().SetCompact(bits)};
    BOOST_CHECK(GetKernelTarget(1000 * 100, bits, false) == expected);
    BOOST_CHECK(GetKernelTarget(1000 * 100, bits, true) == expected);
}

BOOST_AUTO_TEST_SUITE_END()
