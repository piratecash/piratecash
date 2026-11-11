// Copyright (c) 2018-2026 The PirateCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos_header_presync.h>

#include <chainparams.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <uint256.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <vector>

using pos_presync::AddResult;
using pos_presync::Anchor;
using pos_presync::HeaderTail;
using pos_presync::SegmentCollector;

BOOST_FIXTURE_TEST_SUITE(pos_header_presync_tests, BasicTestingSetup)

using namespace std::chrono_literals;

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
            header.posBlockSig.assign(65, static_cast<unsigned char>(i));
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

BOOST_AUTO_TEST_CASE(anchor_spacing)
{
    // A segment between two anchors is buffered in memory as a whole, and anchors
    // sit on multiples of the step so that every release extends the same grid.
    constexpr int ANCHOR_STEP{50000};

    for (const auto& chain : {CBaseChainParams::MAIN, CBaseChainParams::TESTNET}) {
        const auto params{CreateChainParams(*m_node.args, chain)};
        const auto& anchors{params->Checkpoints().mapCheckpoints};
        const int last_height{anchors.rbegin()->first};
        int prev_height{0};
        for (const auto& [height, hash] : anchors) {
            BOOST_CHECK_MESSAGE(height - prev_height <= ANCHOR_STEP, chain << ": gap below anchor " << height);
            BOOST_CHECK_MESSAGE(height % ANCHOR_STEP == 0 || height == last_height, chain << ": anchor " << height << " is off the grid");
            prev_height = height;
        }
    }
}

BOOST_AUTO_TEST_CASE(last_anchor_is_the_minimum_chain_work_block)
{
    // nMinimumChainWork and defaultAssumeValid are taken from the same block. Presync
    // ends at the last anchor, so that block has to be the last anchor.
    for (const auto& chain : {CBaseChainParams::MAIN, CBaseChainParams::TESTNET}) {
        const auto params{CreateChainParams(*m_node.args, chain)};
        const auto last{pos_presync::LastAnchor(params->Checkpoints().mapCheckpoints)};
        BOOST_REQUIRE_MESSAGE(last, chain);
        BOOST_CHECK_MESSAGE(!params->GetConsensus().nMinimumChainWork.IsNull(), chain);
        BOOST_CHECK_MESSAGE(last->hash == params->GetConsensus().defaultAssumeValid, chain);
    }
}

BOOST_AUTO_TEST_CASE(regtest_has_no_anchors_above_genesis)
{
    const auto params{CreateChainParams(*m_node.args, CBaseChainParams::REGTEST)};
    BOOST_CHECK(!pos_presync::NextAnchor(params->Checkpoints().mapCheckpoints, 0));
    BOOST_CHECK(params->GetConsensus().nMinimumChainWork.IsNull());
}

BOOST_AUTO_TEST_CASE(posv2_signature_size_is_enforced)
{
    auto chain{MakeChain(BASE_HASH, 6)};
    const MapCheckpoints anchors{{6, chain[5].GetHash()}};
    SegmentCollector collector{anchors, NO_LIMIT};
    BOOST_REQUIRE(collector.Start(PEER, Anchor{0, BASE_HASH}));

    // The signature is outside the hash: the chain still links and still ends in the anchor.
    BOOST_REQUIRE(chain[2].IsProofOfStakeV2());
    auto oversized{chain};
    oversized[2].posBlockSig.resize(1000);
    BOOST_CHECK(collector.Add(PEER, oversized) == AddResult::BAD_SIGNATURE);
    auto missing{chain};
    missing[2].posBlockSig.clear();
    BOOST_CHECK(collector.Add(PEER, missing) == AddResult::BAD_SIGNATURE);
    BOOST_CHECK_EQUAL(collector.BufferedCount(), 0U);

    // Older headers carry no such signature.
    BOOST_REQUIRE(!chain[0].IsProofOfStakeV2() && chain[0].posBlockSig.empty());
    BOOST_CHECK(collector.Add(PEER, chain) == AddResult::SEGMENT_READY);
}

BOOST_AUTO_TEST_CASE(matched_segment_marks_posv2_headers_as_proof_of_stake)
{
    auto chain{MakeChain(BASE_HASH, 3)};
    const MapCheckpoints anchors{{3, chain[2].GetHash()}};
    SegmentCollector collector{anchors, NO_LIMIT};
    BOOST_REQUIRE(collector.Start(PEER, Anchor{0, BASE_HASH}));

    chain[2].nFlags = 0;
    BOOST_REQUIRE(collector.Add(PEER, chain) == AddResult::SEGMENT_READY);
    const auto segment{collector.TakeSegment()};
    BOOST_CHECK_EQUAL(segment.headers[2].nFlags & 1, 1U);
    BOOST_CHECK_EQUAL(segment.headers[0].nFlags, 0U);
}

BOOST_AUTO_TEST_CASE(only_tmp_errors_count_as_deferred)
{
    BlockValidationState deferred;
    deferred.Error("tmp-bad-unkown-stake");
    BOOST_CHECK(pos_presync::IsHeaderDeferred(deferred));

    BlockValidationState failed;
    failed.Error("out of disk space");
    BOOST_CHECK(!pos_presync::IsHeaderDeferred(failed));

    BlockValidationState invalid;
    invalid.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "tmp-forged-reason");
    BOOST_CHECK(!pos_presync::IsHeaderDeferred(invalid));

    BOOST_CHECK(!pos_presync::IsHeaderDeferred(BlockValidationState{}));
}

BOOST_AUTO_TEST_CASE(tail_is_retried_when_the_tip_moves)
{
    const auto chain{MakeChain(BASE_HASH, 10)};
    const uint256 tip_a{uint256S("a1")}, tip_b{uint256S("a2")};
    HeaderTail tail{/*max_headers=*/100};
    BOOST_CHECK_EQUAL(tail.Owner(), -1);
    BOOST_CHECK(tail.FrontParent().IsNull());

    BOOST_REQUIRE(tail.Store(PEER, chain, /*first=*/4, /*peer_has_more=*/true, 100s));
    BOOST_CHECK_EQUAL(tail.Owner(), PEER);
    BOOST_CHECK(tail.FrontParent() == chain[3].GetHash());
    BOOST_CHECK(tail.NextChunk(PEER + 1, tip_a, 100s, 3).empty());

    const auto first{tail.NextChunk(PEER, tip_a, 100s, 3)};
    BOOST_REQUIRE_EQUAL(first.size(), 3U);
    BOOST_CHECK(first[0].GetHash() == chain[4].GetHash());

    // Nothing was accepted and the tip did not move: wait a second.
    BOOST_CHECK(tail.NextChunk(PEER, tip_a, 100s, 3).empty());
    BOOST_CHECK_EQUAL(tail.NextChunk(PEER, tip_a, 101s, 3).size(), 3U);
    BOOST_CHECK_EQUAL(tail.NextChunk(PEER, tip_b, 101s, 3).size(), 3U);

    // Accepted headers leave the front and the next chunk is due at once.
    tail.Consumed(2);
    BOOST_CHECK(tail.FrontParent() == chain[5].GetHash());
    const auto second{tail.NextChunk(PEER, tip_b, 101s, 10)};
    BOOST_REQUIRE_EQUAL(second.size(), 4U);
    BOOST_CHECK(second[0].GetHash() == chain[6].GetHash());

    tail.Consumed(4);
    BOOST_CHECK(tail.Empty());
    BOOST_CHECK_EQUAL(tail.Owner(), -1);
    BOOST_CHECK(tail.PeerHasMore());
}

BOOST_AUTO_TEST_CASE(held_tail_is_not_replaced)
{
    const auto chain{MakeChain(BASE_HASH, 10)};
    HeaderTail tail{/*max_headers=*/6};

    BOOST_CHECK(!tail.Store(PEER, chain, 10, false, 100s));
    BOOST_CHECK(!tail.Store(PEER, chain, 3, false, 100s)); // over the limit
    BOOST_REQUIRE(tail.Store(PEER, chain, 4, /*peer_has_more=*/true, 100s));

    BOOST_CHECK(!tail.Store(PEER + 1, chain, 8, false, 100s));
    BOOST_CHECK(!tail.Store(PEER, chain, 8, false, 100s));
    BOOST_CHECK_EQUAL(tail.Owner(), PEER);
    BOOST_CHECK(tail.PeerHasMore());
    BOOST_CHECK_EQUAL(tail.NextChunk(PEER, uint256S("a1"), 100s, 10).size(), 6U);

    tail.ForgetPeer(PEER + 1);
    BOOST_CHECK(!tail.Empty());
    tail.ForgetPeer(PEER);
    BOOST_CHECK(tail.Empty());
    BOOST_CHECK(tail.Store(PEER + 1, chain, 8, false, 100s));
    tail.Clear();
    BOOST_CHECK(tail.Store(PEER, chain, 8, false, 100s));
}

BOOST_AUTO_TEST_SUITE_END()
