// Copyright (c) 2015-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <functional>
#include <iterator>
#include <limits>
#include <list>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with DGW */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);

    // Historical Dash blocks, reused with PirateCash target spacings below.
    static const std::vector<std::pair<uint32_t, uint32_t>> mainnet_data = {
        { 1408728124, 0x1b104be1U }, { 1408728332, 0x1b10e09eU }, { 1408728479, 0x1b11a33cU },
        { 1408728495, 0x1b121cf3U }, { 1408728608, 0x1b11951eU }, { 1408728744, 0x1b11abacU },
        { 1408728756, 0x1b118d9cU }, { 1408728950, 0x1b1123f9U }, { 1408729116, 0x1b1141bfU },
        { 1408729179, 0x1b110764U }, { 1408729305, 0x1b107556U }, { 1408729474, 0x1b104297U },
        { 1408729576, 0x1b1063d0U }, { 1408729587, 0x1b10e878U }, { 1408729647, 0x1b0dfaffU },
        { 1408729678, 0x1b0c9ab8U }, { 1408730179, 0x1b0c03d6U }, { 1408730862, 0x1b0dd168U },
        { 1408730914, 0x1b10b864U }, { 1408731242, 0x1b0fed89U }, { 1408731256, 0x1b113ff1U },
        { 1408732229, 0x1b10460bU }, { 1408732257, 0x1b13b83fU }, { 1408732489, 0x1b1418d4U }
    };

    // Construct a chain of block index entries
    std::list<CBlockIndex> blockidx;
    CBlockIndex* blockIndexLast{nullptr};

    for (const auto& [nTime, nBits] : mainnet_data) {
        auto& entry = blockidx.emplace_back();
        entry.nTime = nTime;
        entry.nBits = nBits;
        entry.pprev = blockIndexLast;
        blockIndexLast = &entry;
    }
    blockIndexLast->nHeight = chainParams->GetConsensus().nPowDGWHeight;
    assert(mainnet_data.size() == blockidx.size());

    CBlockHeader blockHeader;
    blockHeader.nTime = 1408732505;
    // Preserve the original Dash block #123457 result at its 150-second spacing.
    auto dash_consensus = chainParams->GetConsensus();
    dash_consensus.nPowTargetSpacing = 150;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(blockIndexLast, &blockHeader, dash_consensus), 0x1b1441deU);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(blockIndexLast, &blockHeader, chainParams->GetConsensus()), 0x1b195256U);

    // test special rules for slow blocks on devnet/testnet
    const auto chainParamsDev = CreateChainParams(*m_node.args, CBaseChainParams::DEVNET);
    // These rules are checked before PoS v3 (see get_next_work_posv3_transition).
    auto dev_consensus = chainParamsDev->GetConsensus();
    dev_consensus.PosV3Height = std::numeric_limits<int>::max();

    // make sure normal rules apply
    blockHeader.nTime = 1408732505;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(blockIndexLast, &blockHeader, dev_consensus), 0x1b321f05U);

    blockHeader.nTime = blockIndexLast->nTime + 4 * dev_consensus.nPowTargetSpacing;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(blockIndexLast, &blockHeader, dev_consensus), 0x1b321f05U);

    // 10x higher target
    ++blockHeader.nTime;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(blockIndexLast, &blockHeader, dev_consensus), 0x1c00c8f8U);
    blockHeader.nTime = blockIndexLast->nTime + 8 * dev_consensus.nPowTargetSpacing;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(blockIndexLast, &blockHeader, dev_consensus), 0x1c00c8f8U);
    blockHeader.nTime = blockIndexLast->nTime + 2 * 60 * 60;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(blockIndexLast, &blockHeader, dev_consensus), 0x1c00c8f8U);
    // lowest diff possible
    ++blockHeader.nTime;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(blockIndexLast, &blockHeader, dev_consensus), 0x207fffffU);
    blockHeader.nTime = blockIndexLast->nTime + 3 * 60 * 60;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(blockIndexLast, &blockHeader, dev_consensus), 0x207fffffU);
}

/* PoS v3: DGW scales targets of pre-v3 blocks in its window by 2^nPosV3TargetShift */
BOOST_AUTO_TEST_CASE(get_next_work_posv3_transition)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    auto consensus = chainParams->GetConsensus();
    const int fork{1300000};
    consensus.PosV3Height = fork;
    auto no_v3 = consensus;
    no_v3.PosV3Height = std::numeric_limits<int>::max();
    BOOST_REQUIRE_EQUAL(consensus.nPosV3TargetShift, 4);

    // 0x1b100000 times 16 is exactly 0x1c010000, so the scaled window can be written out in compact form.
    const uint32_t pre_bits{0x1b100000};
    const uint32_t pre_bits_x16{0x1c010000};
    const uint32_t v3_bits{0x1c00a000};
    BOOST_REQUIRE(arith_uint256().SetCompact(pre_bits_x16) == arith_uint256().SetCompact(pre_bits) << 4);

    // Blocks from fork - 30 to fork + 30, 120 s apart (the timespan stays inside DGW's clamps).
    using Chain = std::list<CBlockIndex>;
    const auto make_chain{[&](Chain& chain, const std::function<uint32_t(int)>& bits_at) {
        CBlockIndex* prev{nullptr};
        for (int i = 0; i < 61; ++i) {
            CBlockIndex& entry{chain.emplace_back()};
            entry.nHeight = fork - 30 + i;
            entry.nTime = 1700000000 + 120 * i + (i % 3) * 7;
            entry.nBits = bits_at(entry.nHeight);
            entry.pprev = prev;
            prev = &entry;
        }
    }};
    const auto at{[&](Chain& chain, int height) { return &*std::next(chain.begin(), height - (fork - 30)); }};
    CBlockHeader header;

    Chain real;
    make_chain(real, [&](int h) { return h < fork ? pre_bits : v3_bits; });

    // First v3 block: the whole window is pre-fork, so 16x the old result.
    Chain all_pre_x16;
    make_chain(all_pre_x16, [&](int) { return pre_bits_x16; });
    const uint32_t first_v3{GetNextWorkRequired(at(real, fork - 1), &header, consensus)};
    BOOST_CHECK_EQUAL(first_v3, GetNextWorkRequired(at(all_pre_x16, fork - 1), &header, no_v3));
    const arith_uint256 old_target{arith_uint256().SetCompact(GetNextWorkRequired(at(real, fork - 1), &header, no_v3))};
    const arith_uint256 new_target{arith_uint256().SetCompact(first_v3)};
    BOOST_CHECK(new_target > (old_target << 4) - (old_target >> 10) && new_target < (old_target << 4) + (old_target >> 10));
    // The last pre-fork block is not affected.
    BOOST_CHECK_EQUAL(GetNextWorkRequired(at(real, fork - 2), &header, consensus), GetNextWorkRequired(at(real, fork - 2), &header, no_v3));

    // Block fork + 23: its window still holds block fork - 1, which alone is scaled.
    Chain last_pre_x16;
    make_chain(last_pre_x16, [&](int h) { return h < fork ? pre_bits_x16 : v3_bits; });
    const uint32_t mixed{GetNextWorkRequired(at(real, fork + 22), &header, consensus)};
    BOOST_CHECK_EQUAL(mixed, GetNextWorkRequired(at(last_pre_x16, fork + 22), &header, no_v3));
    BOOST_CHECK(mixed != GetNextWorkRequired(at(real, fork + 22), &header, no_v3));

    // Block fork + 24: the window holds only v3 blocks, nothing is scaled.
    BOOST_CHECK_EQUAL(GetNextWorkRequired(at(real, fork + 23), &header, consensus), GetNextWorkRequired(at(real, fork + 23), &header, no_v3));

    // A scaled target above the limit counts as the limit. Block fork + 23 with the pre-fork block at the limit
    // (0x1f00ffff) and 23 v3 blocks at T = 0x1c00a000, over exactly the target timespan: the window "average" is
    // (24 * T + powLimit) / 25 = 0x1e0a3d71. Unclamped, 16 * 0x1f00ffff would make it 0x1f00a3d6.
    BOOST_REQUIRE_EQUAL(UintToArith256(consensus.powLimit).GetCompact(), 0x1f00ffffU);
    std::vector<CBlockIndex> window(24);
    for (size_t i = 0; i < window.size(); ++i) {
        window[i].nHeight = fork - 1 + int(i);
        window[i].nBits = i == 0 ? 0x1f00ffff : v3_bits;
        window[i].nTime = 1700000000 + 100 * i;
        window[i].pprev = i == 0 ? nullptr : &window[i - 1];
    }
    window.back().nTime = window.front().nTime + 24 * consensus.nPowTargetSpacing;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&window.back(), &header, consensus), 0x1e0a3d71U);
}

/* PoS v3: the testnet relaxation scales a pre-v3 parent's target the same way */
BOOST_AUTO_TEST_CASE(min_difficulty_relaxation_across_posv3)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::TESTNET);
    auto consensus = chainParams->GetConsensus();
    const int fork{1300000};
    consensus.PosV3Height = fork;
    BOOST_REQUIRE(consensus.fPowAllowMinDifficultyBlocks);
    const arith_uint256 limit{UintToArith256(consensus.powLimit)};

    CBlockIndex parent;
    parent.nBits = 0x1b100000;
    parent.nTime = 1700000000;
    const arith_uint256 parent_target{arith_uint256().SetCompact(parent.nBits)};
    CBlockHeader header;
    header.nTime = parent.nTime + 4 * consensus.nPowTargetSpacing + 10;

    // The first v3 block relaxes from 16x the pre-v3 parent's target.
    parent.nHeight = fork - 1;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&parent, &header, consensus), arith_uint256{(parent_target << 4) * 10}.GetCompact());
    // A v3 parent is not scaled again, and neither is a parent below the fork.
    parent.nHeight = fork;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&parent, &header, consensus), arith_uint256{parent_target * 10}.GetCompact());
    parent.nHeight = fork - 2;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&parent, &header, consensus), arith_uint256{parent_target * 10}.GetCompact());

    // Relaxed beyond the limit, it is the limit.
    parent.nHeight = fork - 1;
    parent.nBits = arith_uint256{limit >> 6}.GetCompact();
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&parent, &header, consensus), limit.GetCompact());

    // With devnet's limit of 2^255 - 1 a parent target of 2^252 would overflow: 2^252 << 4 is 2^256, which wraps to zero.
    const auto dev_params = CreateChainParams(*m_node.args, CBaseChainParams::DEVNET);
    auto dev = dev_params->GetConsensus();
    dev.PosV3Height = fork;
    BOOST_REQUIRE(dev.fPowAllowMinDifficultyBlocks);
    BOOST_REQUIRE(arith_uint256().SetCompact(0x20100000) == arith_uint256(1) << 252);
    parent.nBits = 0x20100000;
    header.nTime = parent.nTime + 4 * dev.nPowTargetSpacing + 10;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&parent, &header, dev), UintToArith256(dev.powLimit).GetCompact());
}

/* Test the constraint on the upper bound for next work */
// BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
// {
//     const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);

//     int64_t nLastRetargetTime = 1231006505; // Block #0
//     CBlockIndex pindexLast;
//     pindexLast.nHeight = 2015;
//     pindexLast.nTime = 1233061996;  // Block #2015
//     pindexLast.nBits = 0x1d00ffff;
//     BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), 0x1d00ffffU);
// }

/* Test the constraint on the lower bound for actual time taken */
// BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
// {
//     const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);

//     int64_t nLastRetargetTime = 1279008237; // Block #66528
//     CBlockIndex pindexLast;
//     pindexLast.nHeight = 68543;
//     pindexLast.nTime = 1279297671;  // Block #68543
//     pindexLast.nBits = 0x1c05a3f4;
//     BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), 0x1c0168fdU);
// }

/* Test the constraint on the upper bound for actual time taken */
// BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
// {
//     const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);

//     int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
//     CBlockIndex pindexLast;
//     pindexLast.nHeight = 46367;
//     pindexLast.nTime = 1269211443;  // Block #46367
//     pindexLast.nBits = 0x1c387f6f;
//     BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), 0x1d00e1fdU);
// }

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, CBaseChainParams::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, CBaseChainParams::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, CBaseChainParams::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, CBaseChainParams::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, CBaseChainParams::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p2 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p3 = &blocks[InsecureRandRange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

void sanity_check_chainparams(const ArgsManager& args, std::string chainName)
{
    const auto chainParams = CreateChainParams(args, chainName);
    const auto consensus = chainParams->GetConsensus();

    // hash genesis is correct
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // target timespan is an even multiple of spacing
    BOOST_CHECK_EQUAL(consensus.nPowTargetTimespan % consensus.nPowTargetSpacing, 0);

    // genesis nBits is positive and doesn't overflow
    arith_uint256 pow_compact;
    bool neg, over;
    pow_compact.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);
    BOOST_CHECK(!neg && pow_compact != 0);
    BOOST_CHECK(!over);
    if (chainName == CBaseChainParams::TESTNET) {
        // The historical testnet genesis exceeds powLimit and uses the legacy genesis exception.
        CBlock genesis{chainParams->GenesisBlock()};
        genesis.fChecked = false;
        genesis.m_checked_merkle_root = false;
        BOOST_CHECK_EQUAL(genesis.nTime, 1541202300U);
        BOOST_CHECK_EQUAL(genesis.nBits, 0x1f04ade3U);
        BOOST_CHECK(UintToArith256(consensus.powLimit) < pow_compact);
        BOOST_CHECK(!CheckProofOfWork(genesis.GetHash(), genesis.nBits, consensus));
        BlockValidationState genesis_state;
        BOOST_CHECK_MESSAGE(CheckBlock(genesis, genesis_state, consensus), genesis_state.ToString());

        ++genesis.nTime;
        genesis.fChecked = false;
        genesis.m_checked_merkle_root = false;
        BlockValidationState invalid_state;
        BOOST_CHECK(!CheckBlock(genesis, invalid_state, consensus));
        BOOST_CHECK_EQUAL(invalid_state.GetRejectReason(), "high-hash");
    } else {
        BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);
    }

    // Check historical retarget results and the applicable multiplication bound.
    if (!consensus.fPowNoRetargeting) {
        arith_uint256 targ_max{UintToArith256(uint256S("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"))};
        if (chainName == CBaseChainParams::MAIN || chainName == CBaseChainParams::TESTNET) {
            // PirateCash retains BTC retarget overflow for historical consensus compatibility.
            // These fixed vectors include both sides of the first overflow boundary.
            CBlockIndex last;
            last.nBits = UintToArith256(consensus.powLimit).GetCompact();
            last.nTime = 4 * consensus.nPowTargetTimespan;
            BOOST_REQUIRE_EQUAL(last.nBits, 0x1f00ffffU);
            const std::vector<std::pair<int64_t, uint32_t>> legacy_btc_targets{
                {7200, 0x1e3fffc0U}, {28800, 0x1f00ffffU},
                {65537, 0x1f00ffffU}, {65538, 0x1d024686U},
                {72000, 0x1e3972b0U}, {86400, 0x1f00b972U}, {115200, 0x1f00ffffU},
            };
            for (const auto& [timespan, expected] : legacy_btc_targets) {
                BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&last, last.GetBlockTime() - timespan, consensus), expected);
            }
            // Bound the multiplication in DGW, which replaces BTC retargeting at nPowDGWHeight.
            targ_max /= 24 * consensus.nPowTargetSpacing * 3;
        } else {
            targ_max /= consensus.nPowTargetTimespan*4;
        }
        // for devnets pow-no-retargeting may work as non-expected but it's a breaking change to fix it
        // TODO: remove this special case for devnet
        if (chainName != CBaseChainParams::DEVNET) {
            BOOST_CHECK(UintToArith256(consensus.powLimit) < targ_max);
        }
    }
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, CBaseChainParams::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, CBaseChainParams::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET_sanity)
{
    sanity_check_chainparams(*m_node.args, CBaseChainParams::TESTNET);
}

BOOST_AUTO_TEST_CASE(ChainParams_DEVNET_sanity)
{
    gArgs.SoftSetBoolArg("-devnet", true);
    sanity_check_chainparams(*m_node.args, CBaseChainParams::DEVNET);
    gArgs.ForceRemoveArg("devnet");
}

BOOST_AUTO_TEST_SUITE_END()
