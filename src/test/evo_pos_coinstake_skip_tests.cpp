// Copyright (c) 2024 The Cosanta Core developers
// Copyright (c) 2026 The PirateCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Tests that PoS coinstake transaction at vtx[1] is properly skipped
// in evo functions that iterate block transactions:
//   - CalcCbTxMerkleRootQuorums  (src/evo/cbtx.cpp)
//   - BuildNewListFromBlock      (src/evo/deterministicmns.cpp)
//
// Regression test: these skips were lost during the v19.3 rebase.
//

#include <test/util/setup_common.h>

#include <chainparams.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <validation.h>

#include <evo/cbtx.h>
#include <evo/deterministicmns.h>
#include <evo/specialtx.h>
#include <llmq/context.h>

#include <boost/test/unit_test.hpp>

// ---------------------------------------------------------------------------
// Helper: build a fake coinstake-like transaction that intentionally has
// nVersion=3 and nType=TRANSACTION_QUORUM_COMMITMENT.  This is the worst-case
// scenario: if the coinstake skip is missing, the loop will try to parse
// the coinstake payload as a CFinalCommitmentTxPayload and fail.
// ---------------------------------------------------------------------------
static CMutableTransaction MakeFakeCoinstakeTx_WorstCase()
{
    CMutableTransaction tx;
    // Coinstake has a real input (not null prevout)
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(uint256S("abcd"), 0); // non-null

    // Coinstake signature: first vout is empty
    tx.vout.resize(2);
    tx.vout[0].nValue = 0;       // empty first output = coinstake marker
    tx.vout[0].scriptPubKey.clear();
    tx.vout[1].nValue = 100 * COIN;
    tx.vout[1].scriptPubKey = CScript() << OP_DUP << OP_HASH160
                                        << std::vector<unsigned char>(20, 0x01)
                                        << OP_EQUALVERIFY << OP_CHECKSIG;

    // Worst case: set nVersion=3 and nType=TRANSACTION_QUORUM_COMMITMENT
    // Without the coinstake skip, the loop will try GetTxPayload<CFinalCommitmentTxPayload>
    // on this transaction and fail with "bad-qc-payload-*".
    tx.nVersion = 3;
    tx.nType = TRANSACTION_QUORUM_COMMITMENT;

    // Put garbage in vExtraPayload so GetTxPayload will definitely fail
    tx.vExtraPayload = std::vector<unsigned char>{0xDE, 0xAD, 0xBE, 0xEF};

    return tx;
}

// ---------------------------------------------------------------------------
// Helper: build a fake coinstake with nVersion=3 and nType=TRANSACTION_PROVIDER_REGISTER.
// Without the coinstake skip in BuildNewListFromBlock, the loop will try to
// parse this as a CProRegTx and fail.
// ---------------------------------------------------------------------------
static CMutableTransaction MakeFakeCoinstakeTx_ProReg()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(uint256S("abcd"), 0);

    tx.vout.resize(2);
    tx.vout[0].nValue = 0;
    tx.vout[0].scriptPubKey.clear();
    tx.vout[1].nValue = 100 * COIN;
    tx.vout[1].scriptPubKey = CScript() << OP_DUP << OP_HASH160
                                        << std::vector<unsigned char>(20, 0x01)
                                        << OP_EQUALVERIFY << OP_CHECKSIG;

    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_REGISTER;
    tx.vExtraPayload = std::vector<unsigned char>{0xDE, 0xAD, 0xBE, 0xEF};

    return tx;
}

// =========================================================================
BOOST_FIXTURE_TEST_SUITE(evo_pos_coinstake_skip_tests, TestChainDIP3V19Setup)

// -------------------------------------------------------------------------
// Test 1: CalcCbTxMerkleRootQuorums must skip coinstake at vtx[1]
//
// If the skip is missing, CalcCbTxMerkleRootQuorums will call
// GetTxPayload<CFinalCommitmentTxPayload> on the coinstake and return
// false with "bad-qc-payload-calc-cbtx-quorummerkleroot".
// -------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(calc_cbtx_merkle_root_quorums_skips_coinstake)
{
    // Build a PoW block first (our reference)
    CBlock powBlock = CreateBlock({}, coinbaseKey);

    // Calculate quorum merkle root for the PoW block — should succeed
    uint256 merkleRootPoW;
    CValidationState statePoW;
    {
        LOCK(cs_main);
        BOOST_CHECK(CalcCbTxMerkleRootQuorums(
            powBlock, ::ChainActive().Tip(),
            *m_node.llmq_ctx->quorum_block_processor,
            merkleRootPoW, statePoW));
    }

    // Now create a PoS version of the same block:
    // - Set POS_BIT in nVersion
    // - Insert a fake coinstake at vtx[1] with worst-case nType
    CBlock posBlock = powBlock;
    posBlock.nVersion |= CBlockHeader::POS_BIT;

    // Insert coinstake at position 1 (after coinbase)
    auto fakeCoinstake = MakeFakeCoinstakeTx_WorstCase();
    posBlock.vtx.insert(posBlock.vtx.begin() + 1, MakeTransactionRef(std::move(fakeCoinstake)));

    // Verify the block really looks like PoS with coinstake
    BOOST_CHECK(posBlock.IsProofOfStake());
    BOOST_CHECK(posBlock.vtx.size() >= 2);
    BOOST_CHECK(posBlock.vtx[1]->IsCoinStake());

    // CalcCbTxMerkleRootQuorums MUST succeed (skip coinstake, don't parse it)
    uint256 merkleRootPoS;
    CValidationState statePoS;
    {
        LOCK(cs_main);
        bool result = CalcCbTxMerkleRootQuorums(
            posBlock, ::ChainActive().Tip(),
            *m_node.llmq_ctx->quorum_block_processor,
            merkleRootPoS, statePoS);

        // If the coinstake skip is missing, this will be false with
        // reason "bad-qc-payload-calc-cbtx-quorummerkleroot"
        BOOST_CHECK_MESSAGE(result,
            "CalcCbTxMerkleRootQuorums failed on PoS block — "
            "coinstake at vtx[1] was not skipped. Reason: " +
            statePoS.GetRejectReason());
    }

    // The quorum merkle root should be the same — the coinstake adds no
    // quorum commitments, so PoW and PoS blocks with identical regular txs
    // should produce the same root.
    BOOST_CHECK_EQUAL(merkleRootPoW, merkleRootPoS);
}

// -------------------------------------------------------------------------
// Test 2: BuildNewListFromBlock must skip coinstake at vtx[1]
//
// The first loop processes special transactions (ProRegTx, ProUpServTx, etc).
// If coinstake has nVersion=3 and nType=TRANSACTION_PROVIDER_REGISTER,
// the loop will try to parse it as CProRegTx and fail.
//
// The second loop checks if any MN collateral is spent by tx inputs.
// A coinstake's vin[0] could accidentally match a collateral outpoint,
// causing a masternode to be incorrectly removed from the list.
// -------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(build_new_list_from_block_skips_coinstake)
{
    // Build a PoW block first — should succeed
    CBlock powBlock = CreateBlock({}, coinbaseKey);

    CDeterministicMNList mnListPoW;
    CValidationState statePoW;
    {
        LOCK(cs_main);
        LOCK(deterministicMNManager->cs);
        BOOST_CHECK(deterministicMNManager->BuildNewListFromBlock(
            powBlock, ::ChainActive().Tip(), statePoW,
            ::ChainstateActive().CoinsTip(), mnListPoW, false));
    }

    // Now create a PoS version with a coinstake that looks like ProRegTx
    CBlock posBlock = powBlock;
    posBlock.nVersion |= CBlockHeader::POS_BIT;

    auto fakeCoinstake = MakeFakeCoinstakeTx_ProReg();
    posBlock.vtx.insert(posBlock.vtx.begin() + 1, MakeTransactionRef(std::move(fakeCoinstake)));

    BOOST_CHECK(posBlock.IsProofOfStake());
    BOOST_CHECK(posBlock.vtx[1]->IsCoinStake());

    // BuildNewListFromBlock MUST succeed (skip coinstake, don't parse it as ProRegTx)
    CDeterministicMNList mnListPoS;
    CValidationState statePoS;
    {
        LOCK(cs_main);
        LOCK(deterministicMNManager->cs);
        bool result = deterministicMNManager->BuildNewListFromBlock(
            posBlock, ::ChainActive().Tip(), statePoS,
            ::ChainstateActive().CoinsTip(), mnListPoS, false);

        // If the coinstake skip is missing, this will fail with
        // "bad-protx-payload" because it tried to parse coinstake as CProRegTx
        BOOST_CHECK_MESSAGE(result,
            "BuildNewListFromBlock failed on PoS block — "
            "coinstake at vtx[1] was not skipped. Reason: " +
            statePoS.GetRejectReason());
    }

    // MN lists should be identical — the coinstake adds no MN operations
    BOOST_CHECK_EQUAL(mnListPoW.GetAllMNsCount(), mnListPoS.GetAllMNsCount());
}

// -------------------------------------------------------------------------
// Test 3: BuildNewListFromBlock collateral check must skip coinstake vin
//
// The second loop in BuildNewListFromBlock checks if any tx input spends
// an MN collateral. A coinstake's inputs must NOT be checked for this —
// staking should never accidentally remove a masternode from the list.
// -------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(build_new_list_collateral_check_skips_coinstake)
{
    // Create a PoS block with a normal coinstake (nVersion=1, nType=NORMAL)
    // but whose vin[0].prevout could theoretically match an MN collateral.
    CBlock posBlock = CreateBlock({}, coinbaseKey);
    posBlock.nVersion |= CBlockHeader::POS_BIT;

    CMutableTransaction coinstake;
    coinstake.nVersion = 1;
    coinstake.nType = TRANSACTION_NORMAL;
    // Use a random outpoint — won't match any real collateral, but this test
    // verifies the code doesn't even try to match coinstake inputs.
    coinstake.vin.resize(1);
    coinstake.vin[0].prevout = COutPoint(uint256S("1234567890abcdef"), 0);
    coinstake.vout.resize(2);
    coinstake.vout[0].nValue = 0;
    coinstake.vout[0].scriptPubKey.clear();
    coinstake.vout[1].nValue = 50 * COIN;
    coinstake.vout[1].scriptPubKey = CScript() << OP_TRUE;

    posBlock.vtx.insert(posBlock.vtx.begin() + 1, MakeTransactionRef(std::move(coinstake)));

    BOOST_CHECK(posBlock.IsProofOfStake());
    BOOST_CHECK(posBlock.vtx[1]->IsCoinStake());

    // Should succeed without issues
    CDeterministicMNList mnList;
    CValidationState state;
    {
        LOCK(cs_main);
        LOCK(deterministicMNManager->cs);
        bool result = deterministicMNManager->BuildNewListFromBlock(
            posBlock, ::ChainActive().Tip(), state,
            ::ChainstateActive().CoinsTip(), mnList, false);

        BOOST_CHECK_MESSAGE(result,
            "BuildNewListFromBlock failed on PoS block with normal coinstake. Reason: " +
            state.GetRejectReason());
    }
}

BOOST_AUTO_TEST_SUITE_END()
