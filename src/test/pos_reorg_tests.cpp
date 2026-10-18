// Copyright (c) 2018-2026 The PirateCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/standard.h>
#include <test/util/logging.h>
#include <test/util/setup_common.h>
#include <txmempool.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pos_reorg_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(disconnected_coinstake_skips_mempool_acceptance)
{
    auto& chainman = *m_node.chainman;
    auto& chainstate = chainman.ActiveChainstate();
    auto& mempool = *m_node.mempool;
    const CScript script{GetScriptForRawPubKey(coinbaseKey.GetPubKey())};
    mineBlocks(2);

    FillableSigningProvider provider;
    provider.AddKey(coinbaseKey);
    CMutableTransaction coinstake;
    coinstake.vin.emplace_back(COutPoint{m_coinbase_txns[0]->GetHash(), 0});
    coinstake.vout.emplace_back(0, CScript{});
    coinstake.vout.emplace_back(m_coinbase_txns[0]->vout[0].nValue, script);
    BOOST_REQUIRE(SignSignature(provider, *m_coinbase_txns[0], coinstake, 0, SIGHASH_ALL));
    const auto stake_tx = MakeTransactionRef(coinstake);
    BOOST_REQUIRE(stake_tx->IsCoinStake());

    const auto ordinary_tx = CreateValidMempoolTransaction(
        m_coinbase_txns[2], 0, 3, coinbaseKey, script,
        m_coinbase_txns[2]->vout[0].nValue - COIN, /*submit=*/true);

    // A missing reorg guard still rejects coinstake in PreChecks. Observe the
    // redundant admission attempt, and prove the log observation is active.
    const std::string rejection_log{"AcceptToMemoryPool: " + stake_tx->GetHash().ToString() + " coinstake"};
    {
        ASSERT_DEBUG_LOG(rejection_log);
        LOCK(cs_main);
        const auto result = chainman.ProcessTransaction(stake_tx);
        BOOST_REQUIRE(result.m_result_type == MempoolAcceptResult::ResultType::INVALID);
        BOOST_CHECK_EQUAL(result.m_state.GetRejectReason(), "coinstake");
    }

    // Legacy PoS blocks remain supported for PirateCash chain compatibility.
    // Use normal block validation and real, signed spends of mature coins.
    CBlock block{CreateBlock({coinstake, ordinary_tx}, script, chainstate)};
    block.nFlags |= CBlockIndex::BLOCK_PROOF_OF_STAKE;
    CMutableTransaction coinbase{*block.vtx[0]};
    coinbase.vout[0].SetEmpty();
    block.vtx[0] = MakeTransactionRef(std::move(coinbase));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.fChecked = false;
    block.m_checked_merkle_root = false;
    BOOST_REQUIRE(block.IsProofOfStake());
    BOOST_REQUIRE(!block.IsProofOfStakeV2());
    BOOST_REQUIRE(chainman.ProcessNewBlock(std::make_shared<const CBlock>(block), true, nullptr));
    CBlockIndex* tip;
    {
        LOCK(cs_main);
        tip = chainstate.m_chain.Tip();
        BOOST_REQUIRE_EQUAL(tip->GetBlockHash(), block.GetHash());
        BOOST_CHECK_EQUAL(mempool.size(), 0U);
    }

    bool attempted_stake_admission{false};
    {
        DebugLogHelper reject_log{rejection_log, [&](const std::string* line) {
            if (line) attempted_stake_admission = true;
            return false;
        }};
        BlockValidationState state;
        BOOST_REQUIRE(chainstate.InvalidateBlock(state, tip));
    }
    BOOST_CHECK_MESSAGE(!attempted_stake_admission, "Disconnected coinstake was submitted to mempool acceptance");
    {
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(chainstate.m_chain.Tip()->GetBlockHash(), block.hashPrevBlock);
        BOOST_CHECK(!mempool.exists(stake_tx->GetHash()));
        BOOST_CHECK(mempool.exists(ordinary_tx.GetHash()));
        BOOST_CHECK_EQUAL(mempool.size(), 1U);
    }
}

BOOST_AUTO_TEST_SUITE_END()
