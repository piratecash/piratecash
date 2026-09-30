# PirateCash Core version v23.1.8

Release is now available from:

  <https://p.cash/en/download/>

This is a new patch version release, fixing four remotely reachable crashes and
bringing further hardening of the peer-to-peer message handlers along with
Proof-of-Stake, wallet, networking, RPC and build fixes.
Upgrading is **strongly recommended** for all nodes, and required for
masternodes.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/piratecash/piratecash/issues>


# Upgrading and downgrading

## How to Upgrade

If you are running an older version, shut it down. Wait until it has completely
shut down, which might take a few minutes for older versions, then run the
installer on Windows or copy over `/Applications/PirateCash-Qt` on macOS or
`piratecashd`/`piratecash-qt` on Linux.

When upgrading from a version older than v19.0.0, PirateCash Core will run a
migration process on first startup. This is expected to complete quickly, but
can take up to thirty minutes on some systems. After this migration, a downgrade
to an older version is only possible with a reindex or a full resync.

Masternode operators must also configure the local Corsa messenger RPC required
by [PIP-0001](https://github.com/piratecash/piratecash/blob/master/doc/pips/pip-0001.md).
Starting with v19.0.0, PirateCash Core refuses to start as a masternode unless
the local authenticated Corsa service check passes.

## Downgrade warning

### Downgrade to a version < v23.0.0

Downgrading to a version older than v23.0.0 is not supported due to database
changes. If you need to use an older version, you must either reindex or resync
the whole chain.


# Release Notes

## Critical fixes

This release fixes four crashes that a remote party could trigger. None of
them affect consensus rules or put funds at risk, but each one can take a node
offline, so all operators should upgrade promptly.

- Fixed a crash while removing provider transactions that a masternode's
  operator-key change invalidates. Those transactions are collected before any
  of them are removed, so when one was an in-mempool descendant of another it
  was already erased along with its ancestor, and the stale entry was then
  dereferenced. Such entries are now skipped. This is reachable whenever a block
  carries a provider registrar update or revocation for a masternode that has
  chained service updates pending in the mempool.
- Fixed a crash caused by an unvalidated LLMQ type in a `qsigshare` message. A
  masternode that received a signature share naming an LLMQ type its chain does
  not register would index a per-type quorum cache that is only populated for
  known types, aborting the process. Unregistered types are now rejected before
  the lookup, and the affected cache lookups no longer create missing entries.
- Fixed a crash caused by a quorum commitment naming a block with no parent,
  such as the genesis block. The parentless block index reached a non-null
  precondition and terminated the process instead of failing validation, which
  no exception handler could contain. Commitments with a parentless quorum base
  block are now rejected, and the LLMQ activation check treats a null
  predecessor as "not enabled" rather than a contract violation.
- Fixed a crash during Proof-of-Stake header validation when a crafted header
  referenced a real staking transaction but supplied an out-of-range output
  index. The peer-controlled index is now checked before transaction outputs
  are accessed, and malformed headers are rejected.

## Security

This release continues the hardening of peer-to-peer message handlers against
denial-of-service from remote peers. These issues do not affect consensus and do
not put funds at risk, but they could be used to crash or degrade nodes -
masternodes in particular - so upgrading is recommended.

- LLMQ / signing: the queues of not-yet-verified recovered signatures and
  signature shares are now bounded, and the vectors carried by the QSIGSHARE,
  QSIGSESANN, QSIGSHARESINV, QGETSIGSHARES and QBSIGSHARES messages are bounded
  before any allocation or decoding takes place. The number of signing share
  sessions a single peer may announce is also capped, so a peer can no longer
  grow that per-peer state without limit (dash#7351).
- LLMQ / DKG: the number of encrypted contribution blobs in a DKG contribution
  is now checked against the quorum's lower bound as well as its upper bound.
- LLMQ / quorum data: the verification vector and encrypted contribution
  vectors in QDATA responses are validated against their expected sizes before
  any BLS decoding is performed.
- Transaction relay: an oversized `notfound` message is now penalised rather
  than silently ignored (dash#7348).
- ChainLocks: the cache of seen ChainLock signatures is now bounded.
- Governance: per-object vote sync requests are now throttled per peer, and
  governance object and vote responses are only accepted from a peer if that
  peer announced them or they were requested from it, using the net-layer
  per-peer request tracker. Governance vote signatures are bounded when read
  from the network and must use one of the two legitimate encodings.
- CoinJoin: the vectors carried by CoinJoin mixing messages are bounded before
  allocation, and a non-participant can no longer abort another session's
  signing phase. An invalid `dstx` message now carries a misbehaviour score
  instead of being dropped for free (dash#7347).
- Bloom filters: filterload and filteradd payloads are bounded before
  allocation.
- Sporks: spork signatures are bounded during deserialization, and malformed
  spork messages now attribute misbehaviour to the sending peer.
- Compact block relay: batched hardening backported from upstream Bitcoin Core
  (dash#7398), including detection of mutated blocks as a defence-in-depth
  measure.

## RPC

- `protx listdiff` no longer reports an always-zero `platformP2PPort` /
  `platformHTTPPort` for masternodes registered with extended addresses; the
  live Platform ports are reported instead.

## GUI

- The PoSe score column is no longer hidden together with banned masternodes in
  the masternode list.
- Fixed an abort when scaling widgets whose font was set in pixels rather than
  points (for example by a stylesheet's `font-size: Npx`); such fonts are now
  converted to a point size instead of being assumed to have one (dash#7465).
- Introduced a framework for sourcing and applying data with dedicated feeds,
  used by the Masternode and Proposal list views for improved data flow and
  separation of concerns (dash#7146).
- Added a new "Proposal Information" widget to the Information tab with an
  interactive donut chart showing proposal budget allocation (dash#7159).
- Added distinct widgets for PirateCash-specific reporting in the Debug window,
  including dedicated Information and Network tabs (dash#7118).
- Added support for reporting `OP_RETURN` payloads as Data Transactions in the
  transaction list (dash#7144).
- Added Tahoe-styled icons for macOS with runtime styling for each network type,
  an updated bundle icon and a mask-based tray icon (dash#7180).
- Filter preferences in the masternode list are now persisted across sessions
  (dash#7148).
- Fixed overview page font double scaling, minimum-width calculation, `SERVICE`
  and `STATUS` column sorting, and common-type filtering in the masternode list
  (dash#7147).
- Fixed `labelError` styling by moving it from `proposalcreate.ui` into
  `general.css` for consistency (dash#7145).
- Fixed banned masternodes incorrectly returning status 0 instead of their
  actual ban status (dash#7157).

## Bug fixes

- Fixed masternode update notifications where the old and new masternode lists
  were swapped, causing incorrect change detection (dash#7154).
- BLS deserialization and key generation now reject identity elements
  (dash#7193).
- Fixed quorum labels not being correctly reseated when new quorum types are
  inserted (dash#7191).
- Block transaction IDs are no longer collected during initial block download,
  preventing unbounded memory growth in `ChainLockSigner` (dash#7208).
- Serialized `TrySignChainTip` to prevent concurrent signing races that could
  split signing shares across different block hashes (dash#7209).
- EvoDB repair is now correctly skipped while reindexing (dash#7222).
- Parallel block fetching during initial block download now respects each
  peer's advertised starting height and does not request blocks the peer has not
  claimed to possess.

## Build and CI

- Fixed a CMake compatibility error when building the freetype dependency with
  newer CMake (dash#7372).
- Stabilized the `-par` / `-parbls` help text (and the generated man pages) so
  they no longer embed the core count of the build machine.
- Updated GitHub Actions pins for the Node 24 runtime.
- Fixed the circular-dependencies lint script under Python 3.15.
- Added Perl to the Guix environment required for the PirateCash OpenSSL build.
- Fixed PirateCash Windows installer generation and packaging.

## Tests

- Governance inventory cache coverage moved from a functional test to unit
  tests, and governance vote test fixtures are now wire-valid.

# PirateCash-specific changes

## Dash Core v23.1.8 base

PirateCash Core v23.1.8 is built from the Dash Core v23.1.8 codebase. Upstream
changes are adapted without replacing PirateCash consensus, network parameters,
staking, rewards or Corsa integration.

## Proof of Stake and Wallet

- Blocks whose coinbase transaction has no outputs are now rejected before the
  pre-v18 PoS coinbase rules inspect its first output.
- After the PoSv2 fork height, legacy block versions are now rejected during
  header acceptance as well as during full block connection.
- Fixed a header-validation loop that could stop advancing while checking an
  unvalidated PoS fork for reuse of a stake input. The scan now follows each
  header's parent back toward the fork point, stops safely at genesis or a
  validated or non-PoS block, and reliably rejects repeated use of the same
  staking output in the header-only tail.
- Fixed a lock-order inversion while signing Proof-of-Stake blocks with an
  encrypted wallet that could deadlock staking after prolonged operation.

Stake input auto-combining has been improved. Previously, a staking output could
settle just below twice `-stakesplitthreshold`, after which small staking reward
outputs could accumulate without being combined. The wallet can now sweep small
inputs into a kernel that is already above the split threshold, and the normal
split pass turns the result back into threshold-sized outputs.

The new `-stakecombinemax=<n>` option sets the largest input, in PIRATE, that may
be swept in this situation. It defaults to `100`; `0` disables this additional
sweep, and values at or above `-stakesplitthreshold` are limited to one PIRATE
below that threshold. The effective value is reported by `getstakingstatus` as
`stakecombinemax`.

Auto-combine candidates must be confirmed, mature and still unspent. Selection
now respects `-reservebalance` and `-inputstakeprotect`, and coinstake
construction observes the block's remaining size and signature-operation
budgets. When staking is enabled, a configured `-blockmaxsize` below 2000 bytes
produces a warning and the minimum space required for a PoS block is reserved.

## P2P and network changes

- The minimum masternode protocol version is now 70240. `ADDRV2` compatibility
  continues to use its actual introduction version, 70223.
- Masternodes now require Corsa protocol version 28, which adds the datagram
  transport required for further network development.

## Miscellaneous

- Renamed `bitcoin-util` manpage and test references to `piratecash-util`
  (dash#7221).

## Interfaces

- Consolidated masternode counts into a single struct and exposed ChainLock,
  InstantSend, credit pool and quorum statistics through the node interface
  (dash#7160).

## Performance improvements

- Replaced two heavy `HashMap` constructions with linear lookups in hot paths
  where the maps were rarely used, reducing overhead (dash#7176).


# v23.1.8 Change log

See the detailed [set of changes][set-of-changes].


# Credits

Thanks to everyone who directly contributed to this release:

- Dmitriy Korniychuk
- Konstantin Akimov
- PastaClaw
- PastaPastaPasta
- UdjinM6

As well as everyone who submitted issues, reviewed pull requests and helped
debug the release candidates. Thanks also go to Dash Core and Bitcoin Core
developers for the upstream work this release builds on.


# Older releases

These releases are considered obsolete. Old PirateCash release notes can be
found here:

- [v23.1.7](release-notes-23.1.7.md) released Jul/11/2026
- [v22.1.4](release-notes-22.1.4.md)
- [v21.1.1](release-notes-21.1.1.md)
- [v20.1.1](release-notes-20.1.1.md)

[set-of-changes]: https://github.com/piratecash/piratecash/compare/v23.1.7-pirate...v23.1.8-pirate
