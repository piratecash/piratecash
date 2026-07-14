# PirateCash Core version v23.1.7

Release is now available from:

  <https://p.cash/en/download/>

This release ports PirateCash Core to the Dash Core v23.1.7 codebase. It includes
the upstream feature set, security hardening and build fixes adapted for the
PirateCash network, together with PirateCash-specific consensus and service changes.

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
by [PIP-0001](pips/pip-0001.md). Starting with v19.0.0, PirateCash Core refuses
to start as a masternode unless the local authenticated Corsa service check
passes.

## Downgrade warning

### Downgrade to a version < v23.0.0

Downgrading to a version older than v23.0.0 is not supported due to database
changes. If you need to use an older version, you must either reindex or resync
the whole chain.


# Release Notes

## Security

This release hardens several peer-to-peer message handlers against
denial-of-service from remote peers. These issues do not affect consensus and do
not put funds at risk, but they could be used to crash or degrade nodes -
masternodes in particular - so upgrading is recommended.

- Networking: a peer whose receive buffer filled up could keep the socket-handler
  thread spinning at 100% CPU for the duration of the backpressure. The thread now
  falls back to its normal poll wait while such peers are paused.
- LLMQ / DKG: pushed DKG messages are now accepted only from verified masternodes,
  are bounded in size, and are structurally validated before being retained;
  malformed signatures can no longer trigger an assertion failure during batch
  signature verification.
- BLS: verifying a DKG contribution share whose verification vector was never
  received no longer dereferences a null pointer.
- InstantSend: locks with an oversized input set are now rejected before any
  expensive processing, and the queues holding not-yet-verified and
  awaiting-transaction locks are bounded to prevent unbounded memory growth.
- Governance: vote-sync requests carrying a bloom filter outside the permitted size
  are rejected, preventing a CPU-amplification stall of P2P message processing.

## Build

- Fixed GCC 16 build failures in warning-enabled builds by tightening header
  includes and initializing LevelDB compaction output size.

# PirateCash-specific changes

## Dash Core v23.1.7 base

PirateCash Core v23.1.7 is built from the Dash Core v23.1.7 codebase. Upstream
changes are adapted without replacing PirateCash consensus, network parameters,
staking, rewards or Corsa integration.

## High-Performance Masternodes

A new high-performance masternode type has been added. High-performance
masternodes are intended to host Platform services in addition to existing
masternode responsibilities such as ChainLocks and InstantSend.

Activation of the v19 hard fork enables registration of 40000 PIRATE collateral
masternodes. In v19.0.0, regular masternodes and high-performance masternodes
have equivalent rewards and voting power per 10000 PIRATE collateral.

## PIP-0001 masternode Corsa requirement

This release ships Stage 1 of [PIP-0001](pips/pip-0001.md), the masternode
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
for later PIP-0001 stages. See [doc/release-notes-pip-0001.md](release-notes-pip-0001.md)
for the detailed operator notes.

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

New wallets can be created through the GUI, the `piratecash-wallet create` command
or the `createwallet` RPC.

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
- build, test and documentation fixes inherited from Dash Core v23.1.7

## Backports from Bitcoin Core

This release includes many updates from Bitcoin Core v0.18 through v0.21, as
well as selected updates from Bitcoin Core v22 and newer versions. Changes that
do not align with Dash or PirateCash network behavior, such as SegWit and RBF, are
excluded from these backports.


# v23.1.7 Change log

PirateCash Core v23.1.7 is based on Dash Core v23.1.7.

For the upstream Dash Core patch changes included in this release, see:

- <https://github.com/dashpay/dash/compare/v23.1.5...dashpay:v23.1.7>

PirateCash-specific changes are tracked in the PirateCash Core repository history:

- <https://github.com/piratecash/piratecash>


# Credits

Thanks to everyone who directly contributed to this release, submitted issues,
reviewed pull requests, helped with release candidates, maintained
infrastructure, or helped translate the project.

The upstream Dash Core v23.1.7 changes merged into this release include
contributions from:

- knst
- PastaPastaPasta

Thanks also go to Dash Core and Bitcoin Core developers for the upstream work
this release builds on.


# Older releases

PirateCash was forked from Dash Core.
