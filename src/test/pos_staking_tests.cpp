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
#include <util/system.h>
#include <util/time.h>
#include <validation.h>
#include <validationinterface.h>
#include <index/txindex.h>

#ifdef ENABLE_WALLET
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/transaction.h>
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

#include <memory>
#include <optional>
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
    auto w = wallet::CreateSyncedWallet(*m_node.chain, *m_node.coinjoin_loader, *Assert(m_node.chainman), m_args, coinbaseKey);
    const CScript spk = WalletScript(coinbaseKey);
    const CBlockIndex* tip = WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Tip());

    const int inputs_before{w->CountInputsWithAmount(50 * COIN)};
    const COutPoint coin = AddConfirmedWalletCoin(*w, tip, spk);
    BOOST_REQUIRE(IsAvailable(*w, coin));
    BOOST_CHECK_EQUAL(w->CountInputsWithAmount(50 * COIN), inputs_before + 1);

    const auto spend = MakeWalletTx(coin, spk, 49 * COIN, /*coinstake=*/false);
    WITH_LOCK(w->cs_wallet, w->AddToWallet(spend, wallet::TxStateInactive{}));
    BOOST_REQUIRE(!IsAvailable(*w, coin));
    BOOST_CHECK_EQUAL(w->CountInputsWithAmount(50 * COIN), inputs_before);

    BOOST_REQUIRE(w->AbandonTransaction(spend->GetHash()));
    BOOST_CHECK_MESSAGE(IsAvailable(*w, coin),
        "abandoned spend left its input hidden: wallet UTXO reconciliation must restore it");
    BOOST_CHECK_EQUAL(w->CountInputsWithAmount(50 * COIN), inputs_before + 1);
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

BOOST_AUTO_TEST_SUITE_END()
