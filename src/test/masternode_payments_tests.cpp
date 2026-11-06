// Copyright (c) 2026 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternode/payments.h>

#include <chain.h>
#include <evo/chainhelper.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <tinyformat.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(masternode_payments_tests)

namespace {
CTxOut MakeOut(CAmount value, uint8_t script_byte)
{
    CScript script;
    script << OP_RETURN << std::vector<uint8_t>{script_byte};
    return CTxOut{value, script};
}
} // namespace

BOOST_AUTO_TEST_CASE(strict_matches_simple)
{
    const std::vector<CTxOut> expected{MakeOut(100, 0x01), MakeOut(200, 0x02)};
    const std::vector<CTxOut> actual{MakeOut(100, 0x01), MakeOut(200, 0x02), MakeOut(50, 0x03)};

    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual, /*strict_multiplicity=*/true), -1);
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual, /*strict_multiplicity=*/false), -1);
}

BOOST_AUTO_TEST_CASE(strict_missing_output_fails)
{
    const std::vector<CTxOut> expected{MakeOut(100, 0x01), MakeOut(200, 0x02)};
    const std::vector<CTxOut> actual{MakeOut(100, 0x01)};

    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual, /*strict_multiplicity=*/true), 1);
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual, /*strict_multiplicity=*/false), 1);
}

// Regression: with two identical expected masternode payments, strict multiplicity
// matching must require two identical coinbase outputs. A single output should
// NOT satisfy both expected outputs (which was the bug pre-v24).
BOOST_AUTO_TEST_CASE(strict_duplicate_expected_requires_duplicate_actual)
{
    const std::vector<CTxOut> expected{MakeOut(100, 0x01), MakeOut(100, 0x01)};

    // Only one matching actual output: strict must reject, legacy must accept.
    const std::vector<CTxOut> actual_one{MakeOut(100, 0x01), MakeOut(50, 0x02)};
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual_one, /*strict_multiplicity=*/true), 1);
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual_one, /*strict_multiplicity=*/false), -1);

    // Two matching actual outputs: both modes accept.
    const std::vector<CTxOut> actual_two{MakeOut(100, 0x01), MakeOut(100, 0x01), MakeOut(50, 0x02)};
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual_two, /*strict_multiplicity=*/true), -1);
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual_two, /*strict_multiplicity=*/false), -1);
}

// Pre-v24 path retains the old existence-only behaviour: a single actual output
// can satisfy any number of identical expected outputs.
BOOST_AUTO_TEST_CASE(legacy_existence_only_matches_duplicates)
{
    const std::vector<CTxOut> expected{MakeOut(100, 0x01), MakeOut(100, 0x01), MakeOut(100, 0x01)};
    const std::vector<CTxOut> actual{MakeOut(100, 0x01)};

    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual, /*strict_multiplicity=*/false), -1);
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual, /*strict_multiplicity=*/true), 1);
}

BOOST_AUTO_TEST_CASE(empty_expected_is_trivially_matched)
{
    const std::vector<CTxOut> expected{};
    const std::vector<CTxOut> actual{MakeOut(100, 0x01)};
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual, /*strict_multiplicity=*/true), -1);
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual, /*strict_multiplicity=*/false), -1);
}

BOOST_AUTO_TEST_CASE(strict_amount_must_match_exactly)
{
    const std::vector<CTxOut> expected{MakeOut(100, 0x01)};
    const std::vector<CTxOut> actual{MakeOut(99, 0x01)};
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual, /*strict_multiplicity=*/true), 0);
    BOOST_CHECK_EQUAL(FindUnmatchedMasternodePayment(expected, actual, /*strict_multiplicity=*/false), 0);
}

// Regression for Dash mainnet block 332320: unsynced nodes must accept old-budget
// payments, while synced nodes must enforce the block reward limit.
// Use regtest because PirateCash mainnet has no old-budget interval.
BOOST_FIXTURE_TEST_CASE(old_budget_window_accepted_while_unsynced, RegTestingSetup)
{
    const Consensus::Params& consensus{m_node.chainman->GetConsensus()};
    const int nBlockHeight{((consensus.nBudgetPaymentsStartBlock + consensus.nBudgetPaymentsCycleBlocks - 1) /
                           consensus.nBudgetPaymentsCycleBlocks) * consensus.nBudgetPaymentsCycleBlocks};
    BOOST_REQUIRE(nBlockHeight >= consensus.nBudgetPaymentsStartBlock);
    BOOST_REQUIRE(nBlockHeight < consensus.nSuperblockStartBlock);
    BOOST_REQUIRE(nBlockHeight % consensus.nBudgetPaymentsCycleBlocks < consensus.nBudgetPaymentsWindowBlocks);

    CBlockIndex pindexPrev;
    pindexPrev.nHeight = nBlockHeight - 1;

    constexpr CAmount blockReward{508031847};
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout = {MakeOut(blockReward, 0x01), MakeOut(117600000000, 0x02)};

    CBlock block;
    block.vtx.push_back(MakeTransactionRef(coinbase));

    auto& mn_payments{*Assert(m_node.chain_helper)->mn_payments};
    LOCK(cs_main);
    const CChain& active_chain{m_node.chainman->ActiveChain()};

    std::string strError;
    BOOST_CHECK(mn_payments.IsBlockValueValid(active_chain, block, &pindexPrev, blockReward, strError,
                                              SuperBlockCheckType::NoCheck));
    BOOST_CHECK(strError.empty());

    // The enforcing branch is only reached by a node that is synced and has no
    // chainlock at that height, and it still rejects the over-reward block.
    BOOST_CHECK(!mn_payments.IsBlockValueValid(active_chain, block, &pindexPrev, blockReward, strError,
                                               SuperBlockCheckType::AllowDuplicates));
    BOOST_CHECK_EQUAL(strError, strprintf("coinbase pays too much at height %d (actual=118108031847 vs limit=508031847), "
                                        "exceeded block reward, old budgets are disabled", nBlockHeight));
}

BOOST_AUTO_TEST_SUITE_END()
