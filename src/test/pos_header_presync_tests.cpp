// Copyright (c) 2018-2026 The PirateCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos_header_presync.h>

#include <chainparams.h>
#include <primitives/block.h>
#include <uint256.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <vector>

using pos_presync::AddResult;
using pos_presync::Anchor;
using pos_presync::SegmentCollector;

BOOST_FIXTURE_TEST_SUITE(pos_header_presync_tests, BasicTestingSetup)

namespace {
constexpr NodeId PEER{7};
constexpr size_t NO_LIMIT{1 << 20};
const uint256 BASE_HASH{uint256S("aa")};

//! `count` linked headers on top of `prev`; every third one is PoSv2 with a signature and flags.
std::vector<CBlockHeader> MakeChain(uint256 prev, size_t count, uint32_t salt = 0)
{
    std::vector<CBlockHeader> headers;
    for (size_t i = 0; i < count; ++i) {
        CBlockHeader header;
        header.nVersion = (i % 3 == 2) ? int32_t(CBlockHeader::POSV2_BITS | 4) : 1;
        header.hashPrevBlock = prev;
        header.nTime = 1000 + i;
        header.nBits = 0x207fffff;
        header.nNonce = salt;
        if (header.IsProofOfStakeV2()) {
            header.posStakeHash = uint256S("bb");
            header.posStakeN = i;
            header.posBlockSig = {0x30, 0x01, static_cast<unsigned char>(i)};
            header.nFlags = 1;
        }
        prev = header.GetHash();
        headers.push_back(header);
    }
    return headers;
}

std::vector<CBlockHeader> Slice(const std::vector<CBlockHeader>& headers, size_t from, size_t to)
{
    return {headers.begin() + from, headers.begin() + to};
}
} // namespace

BOOST_AUTO_TEST_CASE(window_follows_chain_parameters)
{
    BOOST_CHECK_EQUAL(pos_presync::VerifiabilityWindow(8 * 60 * 60, 120), 240);
    BOOST_CHECK_EQUAL(pos_presync::VerifiabilityWindow(24 * 60 * 60, 150), 576);
    BOOST_CHECK_EQUAL(pos_presync::VerifiabilityWindow(24 * 60 * 60, 0), 0);
    BOOST_CHECK_EQUAL(pos_presync::VerifiabilityWindow(0, 120), 0);

    const auto main_params{CreateChainParams(*m_node.args, CBaseChainParams::MAIN)};
    BOOST_CHECK_EQUAL(pos_presync::VerifiabilityWindow(*main_params), 240);
}

BOOST_AUTO_TEST_CASE(anchor_lookup)
{
    const MapCheckpoints anchors{{10, uint256S("01")}, {20, uint256S("02")}};

    BOOST_CHECK_EQUAL(pos_presync::NextAnchor(anchors, 0)->height, 10);
    BOOST_CHECK_EQUAL(pos_presync::NextAnchor(anchors, 9)->height, 10);
    BOOST_CHECK_EQUAL(pos_presync::NextAnchor(anchors, 10)->height, 20);
    BOOST_CHECK(!pos_presync::NextAnchor(anchors, 20));
    BOOST_CHECK(pos_presync::LastAnchor(anchors)->hash == uint256S("02"));
    BOOST_CHECK(!pos_presync::LastAnchor(MapCheckpoints{}));
}

BOOST_AUTO_TEST_CASE(segment_is_handed_over_only_on_anchor_match)
{
    const auto chain{MakeChain(BASE_HASH, 10)};
    const MapCheckpoints anchors{{5, chain[4].GetHash()}, {8, chain[7].GetHash()}};
    SegmentCollector collector{anchors, NO_LIMIT};

    BOOST_REQUIRE(collector.Start(PEER, Anchor{0, BASE_HASH}));
    BOOST_CHECK(collector.Add(PEER, Slice(chain, 0, 3)) == AddResult::NEED_MORE);
    BOOST_CHECK(collector.TakeSegment().headers.empty());
    BOOST_CHECK_EQUAL(collector.Tail().height, 3);

    // The message runs past the anchor: the excess is dropped.
    BOOST_CHECK(collector.Add(PEER, Slice(chain, 3, 9)) == AddResult::SEGMENT_READY);
    BOOST_CHECK(collector.Add(PEER, Slice(chain, 5, 8)) == AddResult::NOT_CONTINUING);

    const auto first{collector.TakeSegment()};
    BOOST_REQUIRE_EQUAL(first.headers.size(), 5U);
    for (size_t i = 0; i < first.headers.size(); ++i) {
        BOOST_CHECK(first.hashes[i] == chain[i].GetHash());
        BOOST_CHECK(first.headers[i].GetHash() == chain[i].GetHash());
        BOOST_CHECK(first.headers[i].posBlockSig == chain[i].posBlockSig);
        BOOST_CHECK_EQUAL(first.headers[i].nFlags, chain[i].nFlags);
    }

    // Collection continues from the anchor with the same peer.
    BOOST_CHECK(collector.IsActive());
    BOOST_CHECK_EQUAL(collector.Tail().height, 5);
    BOOST_CHECK_EQUAL(collector.BufferedCount(), 0U);
    BOOST_CHECK(collector.Add(PEER, Slice(chain, 5, 10)) == AddResult::SEGMENT_READY);
    BOOST_CHECK_EQUAL(collector.TakeSegment().headers.size(), 3U);

    // No anchor is left above the last one.
    BOOST_CHECK(!collector.IsActive());
    BOOST_CHECK(collector.Add(PEER, Slice(chain, 8, 10)) == AddResult::WRONG_PEER);
}

BOOST_AUTO_TEST_CASE(start_needs_an_anchor_above_the_base)
{
    const MapCheckpoints anchors{{5, uint256S("01")}};
    SegmentCollector collector{anchors, NO_LIMIT};

    BOOST_CHECK(!collector.Start(PEER, Anchor{5, uint256S("01")}));
    BOOST_CHECK(!collector.IsActive());
}

BOOST_AUTO_TEST_CASE(unusable_messages_leave_the_buffer_untouched)
{
    const auto chain{MakeChain(BASE_HASH, 10)};
    const MapCheckpoints anchors{{10, chain[9].GetHash()}};
    SegmentCollector collector{anchors, NO_LIMIT};
    BOOST_REQUIRE(collector.Start(PEER, Anchor{0, BASE_HASH}));
    BOOST_REQUIRE(collector.Add(PEER, Slice(chain, 0, 4)) == AddResult::NEED_MORE);
    const size_t bytes{collector.BufferedBytes()};

    BOOST_CHECK(collector.Add(PEER + 1, Slice(chain, 4, 6)) == AddResult::WRONG_PEER);
    BOOST_CHECK(collector.Add(PEER, {}) == AddResult::NOT_CONTINUING);
    BOOST_CHECK(collector.Add(PEER, Slice(chain, 5, 7)) == AddResult::NOT_CONTINUING);

    auto broken{Slice(chain, 4, 8)};
    broken[2].hashPrevBlock = uint256S("cc");
    BOOST_CHECK(collector.Add(PEER, broken) == AddResult::NON_CONTINUOUS);

    BOOST_CHECK_EQUAL(collector.BufferedBytes(), bytes);
    BOOST_CHECK_EQUAL(collector.Tail().height, 4);
    BOOST_CHECK(collector.Add(PEER, Slice(chain, 4, 10)) == AddResult::SEGMENT_READY);
}

BOOST_AUTO_TEST_CASE(anchor_mismatch_discards_the_segment)
{
    const auto chain{MakeChain(BASE_HASH, 6)};
    const auto forged{MakeChain(BASE_HASH, 6, /*salt=*/1)};
    const MapCheckpoints anchors{{6, chain[5].GetHash()}};
    SegmentCollector collector{anchors, NO_LIMIT};

    BOOST_REQUIRE(collector.Start(PEER, Anchor{0, BASE_HASH}));
    BOOST_CHECK(collector.Add(PEER, Slice(forged, 0, 3)) == AddResult::NEED_MORE);
    BOOST_CHECK(collector.Add(PEER, Slice(forged, 3, 6)) == AddResult::ANCHOR_MISMATCH);

    BOOST_CHECK(!collector.IsActive());
    BOOST_CHECK_EQUAL(collector.BufferedBytes(), 0U);
    BOOST_CHECK(collector.TakeSegment().headers.empty());
}

BOOST_AUTO_TEST_CASE(byte_limit_discards_the_segment)
{
    const auto chain{MakeChain(BASE_HASH, 10)};
    const MapCheckpoints anchors{{10, chain[9].GetHash()}};
    SegmentCollector collector{anchors, /*max_bytes=*/400};

    BOOST_REQUIRE(collector.Start(PEER, Anchor{0, BASE_HASH}));
    BOOST_CHECK(collector.Add(PEER, Slice(chain, 0, 2)) == AddResult::NEED_MORE);
    BOOST_CHECK(collector.Add(PEER, Slice(chain, 2, 9)) == AddResult::TOO_LARGE);
    BOOST_CHECK(!collector.IsActive());
    BOOST_CHECK_EQUAL(collector.BufferedBytes(), 0U);
}

BOOST_AUTO_TEST_CASE(anchored_hashes)
{
    pos_presync::ClearAnchored();
    BOOST_CHECK(!pos_presync::IsAnchoredHeader(uint256S("01")));

    pos_presync::MarkAnchored({uint256S("01"), uint256S("02")});
    BOOST_CHECK(pos_presync::IsAnchoredHeader(uint256S("01")));
    BOOST_CHECK(pos_presync::IsAnchoredHeader(uint256S("02")));
    BOOST_CHECK(!pos_presync::IsAnchoredHeader(uint256S("03")));

    pos_presync::ClearAnchored();
    BOOST_CHECK(!pos_presync::IsAnchoredHeader(uint256S("01")));
}

BOOST_AUTO_TEST_SUITE_END()
