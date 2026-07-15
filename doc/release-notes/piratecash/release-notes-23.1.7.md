# PirateCash Core version v23.1.7

Release is now available from:

  <https://p.cash/en/download/>

This release ports PirateCash Core to the Dash Core v23.1.7 codebase. PirateCash
is shipping the Dash Core v23 series as one release, so all applicable upstream
changes from Dash Core v23.0.0 through v23.1.7 are backported into PirateCash and
summarized in this document together with PirateCash-specific consensus and
service changes.

This release is mandatory for all nodes.

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

Masternode operators should upgrade Sentinel to v1.7.3 or newer if Sentinel is
used as part of their deployment.

Masternode operators must also configure the local Corsa messenger RPC required
by [PIP-0001](../../pips/pip-0001.md). Starting with v19.0.0, PirateCash Core
refuses to start as a masternode unless the local authenticated Corsa service
check passes.

## Downgrade warning

### Downgrade to a version < v23.0.0

Downgrading to a version older than v23.0.0 is not supported due to database
changes. If you need to use an older version, you must either reindex or resync
the whole chain.


# Release Notes

## Backported from Dash Core v23.0.0 through v23.1.7

The changes in this section were developed in Dash Core and backported into
PirateCash Core. They are adapted without replacing PirateCash consensus,
network parameters, staking, rewards or Corsa integration. PirateCash did not
publish the intermediate Dash v23 releases separately; v23.1.7 contains the
cumulative applicable changes from the entire range.

### Database, indexes and migration

- EvoDB uses a new masternode-state format that supports extended addresses and
  future data upgrades. Migration is automatic, but downgrading below v23.0.0
  requires a reindex.
- EvoDB diffs are verified and, where possible, repaired automatically at
  startup. The new `evodb verify` and `evodb repair` RPCs provide manual
  diagnostics and repair, and `-forceevodbrepair` forces re-verification.
- The compact block-filter index was upgraded to version 2 and now includes
  fields from special transactions, bringing it to feature parity with bloom
  filters for light clients. Existing block-filter indexes are rebuilt
  automatically.
- Block validation and reindex performance were improved, and unnecessary
  transaction-ID collection during initial block download was removed to avoid
  unbounded ChainLock signer memory use.

### InstantSend, quorums and security

- Quorum messages now use a dedicated priority queue. Recovered signatures are
  relayed proactively, signing shares can be processed by multiple workers, and
  InstantSend height lookups use a dedicated cache. These changes reduce lock
  contention, redundant work and InstantSend confirmation latency.
- ChainLock signing and processing races were fixed, including serialization of
  concurrent chain-tip signing attempts and correction of quorum-label updates.
- BLS deserialization and key generation now reject identity elements. BLS
  migration, benchmark and missing-verification-vector failure cases were also
  fixed.
- Pushed DKG messages are accepted only from verified masternodes, bounded in
  size and structurally validated before retention. Malformed signatures can no
  longer trigger an assertion during batch verification.
- InstantSend locks with oversized input sets are rejected before expensive
  processing, and pending lock queues are bounded to prevent unbounded memory
  growth.
- Peer receive-buffer backpressure no longer causes the socket handler to spin
  at full CPU, and oversized governance vote-sync bloom filters are rejected to
  prevent CPU amplification.
- Invalid oversized LLMQ messages are rejected early, reducing their processing
  cost.

### P2P, masternodes and special transactions

- Extended masternode addresses replace legacy single-endpoint fields and allow
  multiple Core P2P, Platform P2P and Platform HTTPS endpoints. Related ProTx
  inputs and RPC output now use the consolidated `addresses` representation,
  while legacy fields remain available where compatibility requires them.
- Compact block filters can be enabled automatically for masternodes to improve
  light-client privacy and pruned-node support.
- UNIX domain sockets are supported for proxy and selected ZMQ connections.
- Masternodes trickle transactions to non-masternode peers instead of relaying
  them immediately, reducing information leakage while retaining fast
  masternode-to-masternode propagation.
- Recovered-signature and InstantSend lock relay behavior was corrected across
  the v23.1 patch releases, including compatibility with peers requesting
  recovered signatures.
- Asset lock transactions that Platform cannot process are treated as
  non-standard and are not relayed. This covers transactions with more than 100
  inputs, transactions larger than 20480 bytes and unsupported v2 payloads.
- Conflicting ProRegTx or ProUpServTx transactions with the same Platform node
  ID can no longer coexist in the mempool.

### Wallet and Qt interface

- Wallet change-output amounts are randomized to reduce transaction
  fingerprinting.
- Descriptor wallets are fully supported rather than experimental. New
  descriptor wallets include a mobile CoinJoin descriptor so funds mixed by a
  compatible mobile wallet remain visible after mnemonic import.
- Wallet and mnemonic passphrases may contain null characters. Wallet
  encryption, HD-chain loading and decryption error handling were hardened.
- The wallet creation flow verifies that the recovery phrase was recorded, and
  existing HD wallets can display their recovery phrase from the Settings menu.
  Wallets can also be restored from backup through the GUI.
- The Qt interface adds configurable dust-attack protection, duplicate-recipient
  warnings, experimental external-signer support and a restored Send action for
  external signers.
- Governance and masternode views were refreshed with dedicated models, status
  icons, filters, detailed dialogs, proposal voting and proposal-budget
  reporting. The tabs can be shown or hidden without restarting the client.
- GUI configuration is stored in `settings.json`, allowing settings to apply to
  both the GUI and daemon. Values in `settings.json` take precedence over the
  configuration file.
- Wallet RPCs now report the wallet's last processed block. The `receivedby`
  family includes coinbase transactions by default, with options controlling
  immature coinbase handling and legacy behavior.

### RPC, REST, configuration and interfaces

- New RPCs include `evodb verify`, `evodb repair`, `coinjoin status`,
  `newkeypool` and the updated extended-address ProTx operations.
- `getblock` verbosity level 3 and the REST block endpoint include transaction
  input `prevout` data. REST header and block-filter-header counts can be passed
  as optional query parameters.
- `getislocks` includes the requested lock ID, `listdescriptors` reports CoinJoin
  descriptors and next indexes, and ProTx update and revoke RPCs support a
  consistent optional `submit` field.
- Removed or deprecated interfaces include `instantsendtoaddress`,
  `masternode current`, `masternode winner`, `getpoolinfo`, legacy masternode
  registration RPCs and the deprecated `addresses`/`reqSigs` result fields.
- `-shutdownnotify` runs a command synchronously before shutdown. Proxy, RPC and
  P2P port validation is stricter, while `-maxconnections=0` disables listening
  and DNS seeding unless explicitly overridden.
- StatsD supports IPv6 and URL-form hosts. Invalid statistics settings now fail
  at startup instead of silently disabling statistics, and obsolete statistics
  options were removed.

### Performance, stability, build and dependencies

- Header synchronization was made almost twice as fast through optimized
  low-level math. Block connection avoids repeated EHF-signal validation, and
  networking lock contention was reduced when penalizing invalid peers.
- Hot paths avoid unnecessary hash-map construction and duplicate InstantSend
  database lookups.
- Qt was updated from 5.15.14 to 5.15.18, including fixes for
  CVE-2025-4211, CVE-2025-5455 and CVE-2025-30348.
- Upstream build targets moved to Windows 10 and macOS 14. Compiler requirements
  were updated, and later patches fixed Debian 13, GCC 16 and other compiler
  compatibility issues.
- The v23.1 patch releases also correct wallet loading, EvoDB migration and
  repair, BLS scheme selection, masternode update notifications, InstantSend
  relay, external-signer GUI behavior, checkpoint data and several assertion or
  crash conditions.


# PirateCash-specific changes

## High-Performance Masternodes

A new high-performance masternode type has been added. High-performance
masternodes are intended to host Platform services in addition to existing
masternode responsibilities such as ChainLocks and InstantSend.

Activation of the v19 hard fork enables registration of 40000 PIRATE collateral
masternodes. In v19.0.0, regular masternodes and high-performance masternodes
have equivalent rewards and voting power per 10000 PIRATE collateral.

## PIP-0001 masternode Corsa requirement

This release ships Stage 1 of [PIP-0001](../../pips/pip-0001.md), the masternode
messenger service integration. Masternodes must now run a local Corsa messenger
node and configure PirateCash Core with its authenticated RPC credentials.

The required options are:

- `-corsarpcuser=<user>`
- `-corsarpcpassword=<pw>`
- `-corsarpcport=<port>`

When `-masternodeblsprivkey` is set, `piratecashd` probes
`127.0.0.1:<port>/rpc/v1/system/node_status` before entering masternode mode.
The Corsa node must be reachable, must return the required node status fields,
must satisfy the network minimum Corsa protocol version, and must reject a
deliberately invalid authentication probe. If the check fails, PirateCash Core
exits instead of starting the masternode. Regular full nodes, wallets and
`piratecash-cli` are not affected.

After startup, active masternodes run a background heartbeat monitor for the
same local Corsa endpoint. In Stage 1, heartbeat failures are logged but do not
apply PoSe penalties. Network-visible checks and PoSe enforcement are reserved
for later PIP-0001 stages. See
[doc/release-notes-pip-0001.md](../../release-notes-pip-0001.md) for the detailed
operator notes.

## BLS scheme upgrade

The v19 hard fork migrates remaining BLS public key and signature usage to the
basic BLS scheme, aligning serialization with IETF standards. This affects
network messages, quorum commitments, deterministic masternode lists, ProTx
transactions and related RPC behavior.

The release also includes the later Dash v19 fixes for BLS database migration
and historical masternode list handling, improving compatibility for upgraded
nodes and light clients.

## Wallet changes

PirateCash Core no longer automatically creates new wallets on startup. Existing
wallets specified by `-wallet`, `piratecash.conf` or `settings.json` are loaded as
before. If a specified wallet does not exist, PirateCash Core logs a warning
instead of creating a new wallet automatically.

New wallets can be created through the GUI, the `piratecash-wallet create`
command or the `createwallet` RPC.

## P2P and network changes

Support for BIP61 reject messages has been removed, including the
`-enablebip61` option. Debugging and testing should use node logs and RPCs such
as `submitblock`, `getblocktemplate`, `sendrawtransaction` and
`testmempoolaccept`.

CoinJoin-related network messages were updated to improve support for light
clients. The release also includes the applicable Dash Core fixes through
v23.1.7 for mixing, masternode list handling and ChainLocks operation.

## RPC, command and configuration changes

New or updated RPC and command behavior includes:

- `protx register_hpmn`, `protx register_fund_hpmn`,
  `protx register_prepare_hpmn` and `protx update_service_hpmn`
- `protx register_legacy`, `protx register_fund_legacy` and
  `protx register_prepare_legacy`
- `cleardiscouraged`
- `upgradewallet`
- `wipewallettxes`
- `piratecash-wallet wipetxes`
- `masternodelist` modes including `recent` and `hpmn`
- `protx list hpmn`
- additional quorum and BLS scheme fields in related RPC responses

Command-line and configuration changes include:

- new `llmqplatform` option for devnet
- new `unsafesqlitesync` option
- removed `enablebip61`
- changed `llmqinstantsend` and `llmqinstantsenddip0024` handling on regtest
- invalid `-rpcauth` values now cause startup failure
- `-blockversion` is allowed on non-mainnet networks

Please check `help <command>`, `piratecashd --help` or the Qt wallet command-line
options dialog for detailed information.

## Other fixes and improvements

This release also includes:

- fixes for v19 hard fork activation and database migration behavior
- improved support for historical masternode list data on light clients
- ability to keep ChainLocks enforced while disabling signing of new ChainLocks
- wallet GUI improvements for large rescans and long-running wallet operations
- fixes for startup with an empty `settings.json`
- reduced sensitive value logging for masternode and spork keys
- block processing optimizations
- BLS library update to version 1.3.0
- applicable build, test and documentation fixes inherited from Dash Core
  v23.0.0 through v23.1.7

## Backports from Bitcoin Core

This release includes many updates from Bitcoin Core v0.18 through v0.21, as
well as selected updates from Bitcoin Core v22 and newer versions. Changes that
do not align with Dash or PirateCash network behavior, such as SegWit and RBF,
are excluded from these backports.


# v23.1.7 Change log

PirateCash Core v23.1.7 contains the applicable Dash Core changes from v23.0.0
through v23.1.7 as one backported release. The complete upstream code range is:

- <https://github.com/dashpay/dash/compare/v22.1.3...dashpay:v23.1.7>

Detailed upstream release notes:

- [Dash Core v23.0.0](../dash/release-notes-23.0.0.md)
- [Dash Core v23.0.2](../dash/release-notes-23.0.2.md)
- [Dash Core v23.1.0](../dash/release-notes-23.1.0.md)
- [Dash Core v23.1.2](../dash/release-notes-23.1.2.md)
- [Dash Core v23.1.3](../dash/release-notes-23.1.3.md)
- [Dash Core v23.1.4](../dash/release-notes-23.1.4.md)
- [Dash Core v23.1.5](../dash/release-notes-23.1.5.md)
- [Dash Core v23.1.7](https://github.com/dashpay/dash/blob/v23.1.7/doc/release-notes.md)

PirateCash-specific changes are tracked in the PirateCash Core repository
history:

- <https://github.com/piratecash/piratecash>


# Credits

Thanks to everyone who directly contributed to this release, submitted issues,
reviewed pull requests, helped with release candidates, maintained
infrastructure, or helped translate the project.

The upstream Dash Core v23.0.0 through v23.1.7 changes backported into this
release include contributions from:

- Kittywhiskers Van Gogh
- knst
- Konstantin Akimov
- Odysseas Gabrielides
- PastaClaw
- PastaPastaPasta
- thephez
- UdjinM6
- Vijay
- zxccxccxz

Thanks also go to Dash Core and Bitcoin Core developers for the upstream work
this release builds on.


# Older releases

PirateCash was forked from Dash Core.
