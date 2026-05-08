# Cosanta Core version v19.0.0

Release is now available from:

  <https://cosa.is/downloads/>

This is the first major release of the Cosanta Core 19.x.x series. It is based
on Dash Core v19.3.0 and includes the Dash Core 19.0.0 through 19.3.0 feature
set, improvements and bug fixes adapted for the Cosanta network.

This release is mandatory for all nodes.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/cosanta/cosanta-core/issues>


# Upgrading and downgrading

## How to Upgrade

If you are running an older version, shut it down. Wait until it has completely
shut down, which might take a few minutes for older versions, then run the
installer on Windows or copy over `/Applications/Cosanta-Qt` on macOS or
`cosantad`/`cosanta-qt` on Linux.

When upgrading from a version older than v19.0.0, Cosanta Core will run a
migration process on first startup. This is expected to complete quickly, but
can take up to thirty minutes on some systems. After this migration, a downgrade
to an older version is only possible with a reindex or a full resync.

Masternode operators should upgrade Sentinel to v1.7.3 or newer if Sentinel is
used as part of their deployment.

## Downgrade warning

### Downgrade to a version < v19.0.0

Downgrading to a version older than v19.0.0 is not supported due to database
changes. If you need to use an older version, you must either reindex or resync
the whole chain.


# Notable changes

## Dash Core v19.3.0 base

Cosanta Core v19.0.0 is built from the Dash Core v19.3.0 codebase. This means
the Cosanta release includes the major v19 feature work as well as the v19.1,
v19.2 and v19.3 maintenance fixes that followed the original Dash v19.0.0
release.

## High-Performance Masternodes

A new high-performance masternode type has been added. High-performance
masternodes are intended to host Platform services in addition to existing
masternode responsibilities such as ChainLocks and InstantSend.

Activation of the v19 hard fork enables registration of 40000 COSA collateral
masternodes. In v19.0.0, regular masternodes and high-performance masternodes
have equivalent rewards and voting power per 10000 COSA collateral.

## BLS scheme upgrade

The v19 hard fork migrates remaining BLS public key and signature usage to the
basic BLS scheme, aligning serialization with IETF standards. This affects
network messages, quorum commitments, deterministic masternode lists, ProTx
transactions and related RPC behavior.

The release also includes the later Dash v19 fixes for BLS database migration
and historical masternode list handling, improving compatibility for upgraded
nodes and light clients.

## Wallet changes

Cosanta Core no longer automatically creates new wallets on startup. Existing
wallets specified by `-wallet`, `cosanta.conf` or `settings.json` are loaded as
before. If a specified wallet does not exist, Cosanta Core logs a warning
instead of creating a new wallet automatically.

New wallets can be created through the GUI, the `cosanta-wallet create` command
or the `createwallet` RPC.

## P2P and network changes

Support for BIP61 reject messages has been removed, including the
`-enablebip61` option. Debugging and testing should use node logs and RPCs such
as `submitblock`, `getblocktemplate`, `sendrawtransaction` and
`testmempoolaccept`.

CoinJoin-related network messages were updated to improve support for light
clients. The release also includes Dash v19.2 and v19.3 fixes for mixing,
masternode list handling and ChainLocks operation.

## RPC, command and configuration changes

New or updated RPC and command behavior includes:

- `protx register_hpmn`, `protx register_fund_hpmn`,
  `protx register_prepare_hpmn` and `protx update_service_hpmn`
- `protx register_legacy`, `protx register_fund_legacy` and
  `protx register_prepare_legacy`
- `cleardiscouraged`
- `upgradewallet`
- `wipewallettxes`
- `cosanta-wallet wipetxes`
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

Please check `help <command>`, `cosantad --help` or the Qt wallet command-line
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
- build, test and documentation fixes inherited from Dash Core v19.3.0

## Backports from Bitcoin Core

This release includes many updates from Bitcoin Core v0.18 through v0.21, as
well as selected updates from Bitcoin Core v22 and newer versions. Changes that
do not align with Dash or Cosanta network behavior, such as SegWit and RBF, are
excluded from these backports.


# v19.0.0 Change log

Cosanta Core v19.0.0 is based on Dash Core v19.3.0.

For upstream Dash Core changes included in this release, see:

- <https://github.com/dashpay/dash/compare/v18.2.2...dashpay:v19.3.0>

Cosanta-specific changes are tracked in the Cosanta Core repository history:

- <https://github.com/cosanta/cosanta-core>


# Credits

Thanks to everyone who directly contributed to this release, submitted issues,
reviewed pull requests, helped with release candidates, maintained
infrastructure, or helped translate the project.

Thanks also go to Dash Core and Bitcoin Core developers for the upstream work
this release builds on.


# Older releases

Cosanta was forked from Dash Core after the v0.15 release.
