// Copyright (c) 2018-2026 The PirateCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos_header_presync.h>

#include <consensus/validation.h>
#include <serialize.h>
#include <span.h>
#include <streams.h>
#include <sync.h>
#include <util/hasher.h>
#include <version.h>

#include <algorithm>
#include <atomic>
#include <unordered_set>

namespace pos_presync {

namespace {
constexpr int HEADER_SER_TYPE{SER_NETWORK | SER_POSMARKER};

GlobalMutex g_anchored_mutex;
std::unordered_set<uint256, SaltedTxidHasher> g_anchored GUARDED_BY(g_anchored_mutex);
std::atomic<bool> g_anchored_empty{true};
} // namespace

int VerifiabilityWindow(int64_t min_stake_age, int64_t block_spacing)
{
    if (min_stake_age <= 0 || block_spacing <= 0) return 0;
    return static_cast<int>(min_stake_age / block_spacing);
}

int VerifiabilityWindow(const CChainParams& params)
{
    return VerifiabilityWindow(params.MinStakeAge(), params.GetConsensus().nPowTargetSpacing);
}

std::optional<Anchor> NextAnchor(const MapCheckpoints& anchors, int height)
{
    const auto it{anchors.upper_bound(height)};
    if (it == anchors.end()) return std::nullopt;
    return Anchor{it->first, it->second};
}

std::optional<Anchor> LastAnchor(const MapCheckpoints& anchors)
{
    if (anchors.empty()) return std::nullopt;
    return Anchor{anchors.rbegin()->first, anchors.rbegin()->second};
}

SegmentCollector::SegmentCollector(const MapCheckpoints& anchors, size_t max_bytes)
    : m_anchors{anchors}, m_max_bytes{max_bytes} {}

bool SegmentCollector::Start(NodeId owner, const Anchor& base)
{
    Reset();
    const auto target{NextAnchor(m_anchors, base.height)};
    if (!target) return false;
    m_active = true;
    m_owner = owner;
    m_tail = base;
    m_target = *target;
    return true;
}

void SegmentCollector::Reset()
{
    m_active = false;
    m_ready = false;
    m_owner = -1;
    m_tail = Anchor{};
    m_target = Anchor{};
    m_data = {};
    m_hashes = {};
}

AddResult SegmentCollector::Add(NodeId from, const std::vector<CBlockHeader>& headers)
{
    if (!m_active || from != m_owner) return AddResult::WRONG_PEER;
    if (m_ready || headers.empty() || headers.front().hashPrevBlock != m_tail.hash) return AddResult::NOT_CONTINUING;

    // Headers past the anchor are dropped: they belong to the next segment.
    const size_t wanted{std::min<size_t>(headers.size(), m_target.height - m_tail.height)};

    std::vector<uint256> hashes;
    hashes.reserve(wanted);
    uint256 prev{m_tail.hash};
    for (size_t i = 0; i < wanted; ++i) {
        if (headers[i].hashPrevBlock != prev) return AddResult::NON_CONTINUOUS;
        prev = headers[i].GetHash();
        hashes.push_back(prev);
    }

    const int new_height{m_tail.height + static_cast<int>(wanted)};
    if (new_height == m_target.height && prev != m_target.hash) {
        Reset();
        return AddResult::ANCHOR_MISMATCH;
    }

    CVectorWriter writer{HEADER_SER_TYPE, PROTOCOL_VERSION, m_data, m_data.size()};
    for (size_t i = 0; i < wanted; ++i) {
        writer << headers[i];
    }
    m_hashes.insert(m_hashes.end(), hashes.begin(), hashes.end());
    if (BufferedBytes() > m_max_bytes) {
        Reset();
        return AddResult::TOO_LARGE;
    }

    m_tail = Anchor{new_height, prev};
    if (new_height != m_target.height) return AddResult::NEED_MORE;
    m_ready = true;
    return AddResult::SEGMENT_READY;
}

Segment SegmentCollector::TakeSegment()
{
    Segment segment;
    if (!m_ready) return segment;

    segment.headers.resize(m_hashes.size());
    SpanReader reader{HEADER_SER_TYPE, PROTOCOL_VERSION, m_data};
    for (CBlockHeader& header : segment.headers) {
        reader >> header;
    }
    segment.hashes = std::move(m_hashes);

    const NodeId owner{m_owner};
    const Anchor base{m_target};
    Start(owner, base);
    return segment;
}

void MarkAnchored(const std::vector<uint256>& hashes)
{
    LOCK(g_anchored_mutex);
    g_anchored.insert(hashes.begin(), hashes.end());
    g_anchored_empty = g_anchored.empty();
}

bool IsAnchoredHeader(const uint256& hash)
{
    if (g_anchored_empty) return false;
    LOCK(g_anchored_mutex);
    return g_anchored.count(hash) > 0;
}

void ClearAnchored()
{
    LOCK(g_anchored_mutex);
    g_anchored.clear();
    g_anchored_empty = true;
}

bool IsHeaderDeferred(const BlockValidationState& state)
{
    return state.IsError() && state.GetRejectReason().starts_with("tmp-");
}

NodeId HeaderTail::Owner() const
{
    LOCK(m_mutex);
    return m_headers.empty() ? -1 : m_owner;
}

bool HeaderTail::Empty() const
{
    LOCK(m_mutex);
    return m_headers.empty();
}

bool HeaderTail::PeerHasMore() const
{
    LOCK(m_mutex);
    return m_peer_has_more;
}

bool HeaderTail::Store(NodeId from, const std::vector<CBlockHeader>& headers, size_t first, bool peer_has_more,
                       std::chrono::seconds now)
{
    LOCK(m_mutex);
    if (first >= headers.size() || headers.size() - first > m_max_headers) return false;
    if (!m_headers.empty()) return false;

    m_headers.assign(headers.begin() + first, headers.end());
    m_owner = from;
    m_peer_has_more = peer_has_more;
    m_retry_now = false;
    m_last_tip.SetNull();
    m_last_attempt = now;
    return true;
}

std::vector<CBlockHeader> HeaderTail::NextChunk(NodeId peer, const uint256& tip, std::chrono::seconds now, size_t max_count)
{
    LOCK(m_mutex);
    if (m_headers.empty() || peer != m_owner) return {};
    if (!m_retry_now && tip == m_last_tip && now - m_last_attempt < std::chrono::seconds{1}) return {};

    m_retry_now = false;
    m_last_tip = tip;
    m_last_attempt = now;
    const size_t count{std::min(max_count, m_headers.size())};
    return {m_headers.begin(), m_headers.begin() + count};
}

void HeaderTail::Consumed(size_t count)
{
    LOCK(m_mutex);
    count = std::min(count, m_headers.size());
    if (count == 0) return;
    m_headers.erase(m_headers.begin(), m_headers.begin() + count);
    m_retry_now = !m_headers.empty();
}

uint256 HeaderTail::FrontParent() const
{
    LOCK(m_mutex);
    return m_headers.empty() ? uint256{} : m_headers.front().hashPrevBlock;
}

void HeaderTail::Clear()
{
    LOCK(m_mutex);
    m_headers.clear();
}

void HeaderTail::ForgetPeer(NodeId peer)
{
    LOCK(m_mutex);
    if (peer == m_owner) m_headers.clear();
}

} // namespace pos_presync
