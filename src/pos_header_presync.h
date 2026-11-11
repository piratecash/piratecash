// Copyright (c) 2018-2026 The PirateCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POS_HEADER_PRESYNC_H
#define BITCOIN_POS_HEADER_PRESYNC_H

#include <chainparams.h>
#include <net_types.h>
#include <primitives/block.h>
#include <uint256.h>

#include <sync.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

class BlockValidationState;

/**
 * Header presync below the last checkpoint.
 *
 * A PoSv2 header can only be verified once the block holding its stake is in
 * the active chain, so headers far ahead of the tip cannot enter the block
 * index through the usual path. Below the last checkpoint ("anchor") they are
 * collected outside the index instead, one anchor-to-anchor segment at a time,
 * and handed over only when the segment's last header hashes to the anchor.
 * Nothing collected here is trusted before that match.
 */
namespace pos_presync {

/** How many blocks ahead of the tip a PoSv2 header is expected to be verifiable. An estimate for thresholds only. */
int VerifiabilityWindow(int64_t min_stake_age, int64_t block_spacing);
int VerifiabilityWindow(const CChainParams& params);

struct Anchor {
    int height{0};
    uint256 hash;
};

/** First anchor strictly above `height`. */
std::optional<Anchor> NextAnchor(const MapCheckpoints& anchors, int height);
std::optional<Anchor> LastAnchor(const MapCheckpoints& anchors);

enum class AddResult {
    NEED_MORE,       //!< headers buffered, the anchor is not reached yet
    SEGMENT_READY,   //!< the anchor matched; call TakeSegment()
    WRONG_PEER,      //!< not from the owning peer; nothing changed
    NOT_CONTINUING,  //!< does not extend the buffered tail; nothing changed
    NON_CONTINUOUS,  //!< headers of one message do not link to each other; nothing changed
    ANCHOR_MISMATCH, //!< the header at the anchor height has a different hash; collector reset
    BAD_SIGNATURE,   //!< a PoSv2 signature has the wrong size; nothing changed. It is outside the hash, so the peer may be a relayer
    TOO_LARGE,       //!< byte limit exceeded; collector reset
};

struct Segment {
    std::vector<CBlockHeader> headers;
    std::vector<uint256> hashes; //!< hashes[i] == headers[i].GetHash()
    int first_height{0};         //!< height of headers[0]
};

/** Collects one segment from a single peer. Not thread safe: the caller serializes access. */
class SegmentCollector
{
public:
    SegmentCollector(const MapCheckpoints& anchors, size_t max_bytes);

    /** Begin collecting the segment that follows `base`, a block already in the index. Returns false if no anchor lies above it. */
    bool Start(NodeId owner, const Anchor& base);
    void Reset();

    bool IsActive() const { return m_active; }
    NodeId Owner() const { return m_owner; }
    /** Last buffered header, or the base while the buffer is empty. */
    const Anchor& Tail() const { return m_tail; }
    size_t BufferedBytes() const { return m_data.size() + m_hashes.size() * sizeof(uint256); }
    size_t BufferedCount() const { return m_hashes.size(); }

    AddResult Add(NodeId from, const std::vector<CBlockHeader>& headers);

    /**
     * Hand over the matched segment and move on to the next one. The collector goes
     * inactive after the last anchor. PoSv2 headers get the proof-of-stake flag set:
     * it is not part of the hash, so the peer's value is not relied on.
     */
    Segment TakeSegment();

private:
    const MapCheckpoints& m_anchors;
    const size_t m_max_bytes;

    bool m_active{false};
    bool m_ready{false};
    NodeId m_owner{-1};
    Anchor m_tail;
    Anchor m_target;
    std::vector<unsigned char> m_data; //!< headers in network serialization
    std::vector<uint256> m_hashes;
};

/**
 * Hashes of headers that came out of a matched segment and are being moved
 * into the block index. Their authenticity follows from the hash chain up to
 * the anchor, so the stake proof is not checked for them.
 */
void MarkAnchored(const std::vector<uint256>& hashes);
bool IsAnchoredHeader(const uint256& hash);
/** Same, without hashing the header while no segment is being moved. */
bool IsAnchoredHeader(const CBlockHeader& header);
void ClearAnchored();

/** The header was not judged: its stake cannot be looked up until the tip gets closer. */
bool IsHeaderDeferred(const BlockValidationState& state);

/**
 * Headers of one message that were deferred, kept until the tip is close enough
 * to check them. One peer owns the tail at a time; nothing in it is trusted.
 */
class HeaderTail
{
public:
    explicit HeaderTail(size_t max_headers) : m_max_headers{max_headers} {}

    /** Owning peer, or -1 while the tail is empty. */
    NodeId Owner() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    bool Empty() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** Whether the message the tail came from was full, so the peer has more to send. */
    bool PeerHasMore() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Keep `headers[first..]`. Refused, with nothing changed, while a tail is held or when the limit is exceeded. */
    bool Store(NodeId from, const std::vector<CBlockHeader>& headers, size_t first, bool peer_has_more,
               std::chrono::seconds now) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Front of the tail when it is `peer`'s and a retry is due: the tip moved, the
     * previous chunk was accepted, or a second passed. Empty otherwise.
     */
    std::vector<CBlockHeader> NextChunk(NodeId peer, const uint256& tip, std::chrono::seconds now, size_t max_count)
        EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** Drop `count` accepted headers from the front. */
    void Consumed(size_t count) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** Block the tail waits on: the parent of its first header. Null while the tail is empty. */
    uint256 FrontParent() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void Clear() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void ForgetPeer(NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

private:
    const size_t m_max_headers;

    mutable Mutex m_mutex;
    std::deque<CBlockHeader> m_headers GUARDED_BY(m_mutex);
    NodeId m_owner GUARDED_BY(m_mutex){-1};
    bool m_peer_has_more GUARDED_BY(m_mutex){false};
    bool m_retry_now GUARDED_BY(m_mutex){false};
    uint256 m_last_tip GUARDED_BY(m_mutex);
    std::chrono::seconds m_last_attempt GUARDED_BY(m_mutex){0};
};

} // namespace pos_presync

#endif // BITCOIN_POS_HEADER_PRESYNC_H
