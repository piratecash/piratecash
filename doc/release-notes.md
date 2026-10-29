# PirateCash Core version v23.1.10

Release is now available from:

  <https://p.cash/en/download/>

This is a new patch version release. It fixes a governance issue that could
stop a superblock from paying any proposals, and hardens DKG and BLS signature
verification against misbehaving quorum members and peers.

The upstream changes are adapted for the PirateCash network without replacing
PirateCash-specific consensus and service changes.

PirateCash-specific changes in this release also harden Proof-of-Stake header
and block validation, fix the header-only stake double-spend scan and improve
stake input auto-combining.

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

## Governance

- Masternodes no longer vote NO-FUNDING on a superblock trigger before that
  superblock's maturity window opens, when they have no candidate of their own
  to compare it against.
  These early votes could keep a trigger with the correct payments from
  reaching the funding threshold, so the superblock paid no proposals.
  Triggers that don't match a masternode's candidate still receive a NO vote
  once the window opens.
- Masternodes no longer vote on triggers until they are fully synced. A
  masternode restarting inside the maturity window could otherwise vote NO on
  the correct trigger before it was able to build its own candidate.

Masternodes running older versions still cast the early NO votes, so this
protection becomes effective as masternodes upgrade. Masternode operators
should upgrade before the next superblock.

## Security and denial-of-service hardening

These issues do not affect consensus and do not put funds at risk.

- BLS batch verification (signature shares, recovered signatures, InstantSend
  locks and DKG messages) now weights each signature with a secret random
  factor before aggregating. Previously two invalid signatures whose errors
  cancelled out could pass as a valid batch. A peer could use this to disrupt
  InstantSend or ChainLock signing, get honest peers penalised, or get honest
  DKG members marked bad.
- Batched verification of DKG contributions is weighted the same way, so
  colluding members can no longer give another member invalid secret-key
  shares that pass verification, which could leave that member unable to sign
  for the quorum. This did not expose the quorum key.
- A DKG member copying another member's entire contribution could prevent the
  whole quorum from forming. Members whose verification vectors share a
  coefficient are now excluded from the commitment instead of the round
  failing. This excludes both the copying member and the member it copied,
  and both receive the usual PoSe penalty for failed DKG participation.
- Colluding DKG members could stop chosen honest members from building their
  secret-key share, which could prevent a quorum from forming without any
  member being blamed. The same condition made a node penalise an honest peer
  serving quorum data during share recovery. Equal secret-key shares from
  different members are now accepted; the resulting key share is still
  verified against the quorum's verification vector.

Like the governance fix, the DKG fixes protect a quorum once enough of its
members run this version. Weighted batch verification does more work per
message, so masternodes may see higher CPU use during DKG and signing.

# PirateCash-specific changes

## Dash Core v23.1.10 base

PirateCash Core v23.1.10 is built from the Dash Core v23.1.10 codebase. Upstream
changes are adapted without replacing PirateCash consensus, network parameters,
staking, rewards or Corsa integration.

## Proof-of-Stake validation hardening

- Crafted PoS headers that reference an output index outside the referenced
  transaction are now rejected before that output is accessed.
- Blocks whose coinbase transaction has no outputs are now rejected before the
  pre-v18 PoS coinbase rules inspect its first output.
- After the PoSv2 fork height, legacy block versions are now rejected during
  header acceptance as well as during full block connection.
- Fixed a header-validation loop that could stop advancing while checking an
  unvalidated PoS fork for reuse of a stake input. The scan now follows each
  header's parent back toward the fork point, stops safely at genesis or a
  validated or non-PoS block, and reliably rejects repeated use of the same
  staking output in the header-only tail.

## Stake input auto-combining

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

# Compatibility with earlier releases

The following sections describe integration and migration behavior introduced
in earlier PirateCash releases and retained in v23.1.10.

## High-Performance Masternodes

High-performance masternodes are intended to host Platform services in addition
to existing masternode responsibilities such as ChainLocks and InstantSend.

Activation of the v19 hard fork enables registration of 40000 PIRATE collateral
masternodes. In v19.0.0, regular masternodes and high-performance masternodes
have equivalent rewards and voting power per 10000 PIRATE collateral.

## PIP-0001 masternode Corsa requirement

This release retains Stage 1 of [PIP-0001](pips/pip-0001.md), the masternode
messenger service integration. Masternodes must run a local Corsa messenger
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
v23.1.10 for mixing, masternode list handling and ChainLocks operation.

## RPC, command and configuration compatibility

Current RPC and command behavior includes:

- `protx register_evo`, `protx register_fund_evo`,
  `protx register_prepare_evo` and `protx update_service_evo`
- `protx register_legacy`, `protx register_fund_legacy` and
  `protx register_prepare_legacy`
- `cleardiscouraged`
- `upgradewallet`
- `wipewallettxes`
- `piratecash-wallet wipetxes`
- `masternodelist` modes including `recent` and `evo`
- `protx list evo`
- additional quorum and BLS scheme fields in related RPC responses

Command-line and configuration changes include:

- `llmqplatform` option for devnet
- `unsafesqlitesync` option for testing and debugging
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
- build, test and documentation fixes inherited from Dash Core v23.1.10

## Backports from Bitcoin Core

This release includes many updates from Bitcoin Core v0.18 through v0.21, as
well as selected updates from Bitcoin Core v22 and newer versions. Changes that
do not align with Dash or PirateCash network behavior, such as SegWit and RBF, are
excluded from these backports.


# v23.1.10 Change log

PirateCash Core v23.1.10 is based on Dash Core v23.1.10.

For the upstream Dash Core patch changes included in this release, see:

- [Upstream Dash Core changes][set-of-changes]

PirateCash-specific changes are tracked in the PirateCash Core repository history:

- <https://github.com/piratecash/piratecash>


# Credits

Thanks to everyone who directly contributed to this release, submitted issues,
reviewed pull requests, helped with release candidates, maintained
infrastructure, or helped translate the project.

The upstream Dash Core v23.1.10 changes merged into this release include
contributions from:

- PastaPastaPasta
- UdjinM6

Thanks also go to Dash Core and Bitcoin Core developers for the upstream work
this release builds on.


# Older releases

PirateCash was forked from Dash Core.

These upstream Dash Core releases are considered obsolete. Their release
notes can be found here:

- [v23.1.9](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-23.1.9.md) released Oct/29/2026
- [v23.1.8](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-23.1.8.md) released Jul/29/2026
- [v23.1.7](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-23.1.7.md) released Jun/30/2026
- [v23.1.5](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-23.1.5.md) released Jun/19/2026
- [v23.1.4](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-23.1.4.md) released Jun/18/2026
- [v23.1.3](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-23.1.3.md) released May/28/2026
- [v23.1.2](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-23.1.2.md) released Mar/12/2026
- [v23.1.0](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-23.1.0.md) released Feb/15/2026
- [v23.0.2](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-23.0.2.md) released Dec/4/2025
- [v23.0.0](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-23.0.0.md) released Nov/10/2025
- [v22.1.3](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-22.1.3.md) released Jul/15/2025
- [v22.1.2](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-22.1.2.md) released Apr/15/2025
- [v22.1.1](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-22.1.1.md) released Feb/17/2025
- [v22.1.0](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-22.1.0.md) released Feb/10/2025
- [v22.0.0](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-22.0.0.md) released Dec/12/2024
- [v21.1.1](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-21.1.1.md) released Oct/22/2024
- [v21.1.0](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-21.1.0.md) released Aug/8/2024
- [v21.0.2](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-21.0.2.md) released Aug/1/2024
- [v21.0.0](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-21.0.0.md) released Jul/25/2024
- [v20.1.1](https://github.com/dashpay/dash/blob/master/doc/release-notes/dash/release-notes-20.1.1.md) released April/3/2024

[set-of-changes]: https://github.com/dashpay/dash/compare/v23.1.9...dashpay:v23.1.10
