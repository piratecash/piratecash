// Copyright (c) 2014-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(subsidy_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const auto& consensus = chainParams->GetConsensus();
    constexpr int HALVING_INTERVAL = 1 << 20;

    // The previous height selects the reward. Difficulty no longer changes the
    // 50 PIRATE base, while the v18 reduction and treasury have separate boundaries.
    const struct {
        uint32_t bits;
        int height;
        CAmount base;
        CAmount treasury_before_v20;
        CAmount treasury_v20;
    } cases[]{
        {0x1c4a47c4, 4249, 50 * COIN, 0, 0},
        {0x1c4a47c4, 4501, 50 * COIN, 0, 0},
        {0x1c29ec00, 5464, 50 * COIN, 0, 0},
        {0x1c29ec00, 5465, 50 * COIN, 0, 0},
        {0x1c08ba34, 17588, 50 * COIN, 0, 0},
        {0x1b10cf42, 99999, 50 * COIN, 0, 0},
        {0x1b11548e, HALVING_INTERVAL - 1, 50 * COIN, 0, 0},
        {0x1b10d50b, HALVING_INTERVAL, 25 * COIN, 0, 0},
        {0x1b10d50b, static_cast<int>(consensus.nRewForkDecreaseV18), 25 * COIN, 0, 0},
        {0x1b10d50b, static_cast<int>(consensus.nRewForkDecreaseV18 + 1), 150, 0, 0},
        {0x1b10d50b, static_cast<int>(consensus.nRestoreRewardV18), 150, 0, 0},
        {0x1b10d50b, static_cast<int>(consensus.nRestoreRewardV18 + 1), 25 * COIN, 0, 0},
        {0x1b10d50b, consensus.nBudgetPaymentsStartBlock, 25 * COIN, 0, 0},
        {0x1b10d50b, consensus.nBudgetPaymentsStartBlock + 1, 25 * COIN, 250000000, 500000000},
        {0x1b10d50b, 2 * HALVING_INTERVAL - 1, 25 * COIN, 250000000, 500000000},
        {0x1b10d50b, 2 * HALVING_INTERVAL, 1250000000, 125000000, 250000000},
        {0x1b10d50b, 32 * HALVING_INTERVAL - 1, 2, 0, 0},
        {0x1b10d50b, 32 * HALVING_INTERVAL, 1, 0, 0},
        {0x1b10d50b, 33 * HALVING_INTERVAL - 1, 1, 0, 0},
        {0x1b10d50b, 33 * HALVING_INTERVAL, 0, 0, 0},
        {0x1b10d50b, 34 * HALVING_INTERVAL, 0, 0, 0},
    };
    for (const auto& test : cases) {
        for (const bool v20 : {false, true}) {
            BOOST_TEST_CONTEXT("previous height=" << test.height << ", v20=" << v20) {
                const CAmount treasury = v20 ? test.treasury_v20 : test.treasury_before_v20;
                BOOST_CHECK_EQUAL(GetBlockSubsidyInner(test.bits, test.height, consensus, v20), test.base - treasury);
                BOOST_CHECK_EQUAL(GetSuperblockSubsidyInner(test.bits, test.height, consensus, v20), treasury);
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
