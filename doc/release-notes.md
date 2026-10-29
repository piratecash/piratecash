# PirateCash Core version v23.1.9

Release is now available from:

  <https://p.cash/en/download/>

This is a new patch version release. It fixes a consensus-affecting
transaction-encoding bug, several remotely reachable crashes and
denial-of-service issues in the masternode, DKG, signing and governance paths,
and backports a number of hardening and robustness fixes from later releases.

The upstream changes are adapted for the PirateCash network without replacing
PirateCash-specific consensus and service changes.

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

## Consensus

- Transactions whose version field is negative are now rejected once DIP3 is
  active. The version and type share one 32-bit field; a negative version was
  sign-extended when a transaction was re-serialized, so a block containing such
  a transaction could be accepted by a node that received the original bytes and
  rejected by every node that received the re-serialized form, leading to a
  persistent chain split. The new rule rejects these transactions uniformly. No
  valid or historical transaction is affected and transaction ids are unchanged.

## Crash fixes

These issues do not affect consensus rules or put funds at risk, but each one
can take a node offline.

- Fixed an abort during the DKG when a quorum member sent a contribution whose
  decrypted secret-key share was invalid: the share reached an aggregation
  assertion and terminated the process on every honest member of the quorum.
  Invalid shares are now rejected before aggregation.
- Fixed block-template failures caused by provider transactions: a
  `ProUpServTx` with an invalid type, a `ProRegTx` reusing collateral already
  referenced by an in-mempool update, and a pending owner key colliding with a
  Platform node id are now rejected at mempool acceptance.
- Bounded the masternode-list caches by size so a peer cannot drive unbounded
  memory growth between blocks.
- Bounded `DomainPort` deserialization so a small provider transaction can no
  longer make the node allocate and zero very large buffers while holding
  `cs_main`.
- Decoding an unknown-version provider transaction through the verbose decode
  RPCs no longer dereferences a missing network-info object.
- `masternode payments` no longer aborts at the genesis block, and the
  masternode-list RPCs no longer cache a list for a block outside the active
  chain.
- Evonodes no longer abort at credit-pool snapshot boundaries when the pool is
  first constructed outside a block connection (for example by
  `getassetunlockstatuses`).
- Flat-file (block and undo) positions are now stored and seeked as 64-bit
  values. Block-undo data can be several times the size of its block, so a busy
  chain could push a data file past the point a 32-bit offset can address; the
  write and seek paths no longer narrow the position to 32 bits, which avoided a
  silent wrap (and, on Windows, a failure past 2 GiB).

## InstantSend

- Provider (masternode) transactions are no longer given InstantSend locks.
  Locking them served no purpose and interacted badly with lock eviction on
  mempool removal. Software that waited for an InstantSend lock on a provider
  transaction should wait for a ChainLock or for confirmations instead.

## Security and denial-of-service hardening

These issues do not affect consensus and do not put funds at risk.

- DKG contribution verification: a single verification-vector coefficient
  shared between two members no longer fails the whole verification batch.
  Previously that marked unrelated honest members' contributions invalid and
  caused them to be complained about; contributions are now verified
  individually, so only the offending member is affected.
- Signature-share batch verification is now scoped per sender, so one peer can
  no longer get an honest peer penalised or have a forged share stored in place
  of an honest one. Malformed detached quorum share batches are rejected, and
  signing intake is gated against the active-quorum set before any quorum is
  retrieved.
- Governance: an orphan vote now requires a valid masternode signature before
  it is cached, the number of orphan parent objects re-requested per tick is
  bounded, governance vote signature checks are memoised to bound the cost of a
  sync request, a vote is no longer recorded before it is validated, and a
  failed BLS vote-signature check is logged under the governance category
  instead of unconditionally, so a peer cannot fill `debug.log` with it.
- Socket exception monitoring is now consistent between the `poll()` and
  `select()` backends, and a peer whose send buffer stalls is now subject to the
  send timeout, so it can no longer hold a connection slot indefinitely.
- Verified inbound connections are bounded per operator identity; `CHECKDATASIG`
  uses the signature cache; repeated authenticated Platform bans are skipped;
  `DYNBITSET` allocation is bounded against the remaining stream size; a bogus
  InstantSend lock no longer drops a batch of unrelated locks; and
  `-allowprivatenet` no longer influences masternode address validation.

## Wallet

- Invalid-mnemonic errors no longer echo the recovery phrase, an encrypted
  wallet is relocked if an HD upgrade fails after it was unlocked, and a wallet
  unlock deadline can no longer be extended without re-authenticating.

# PirateCash-specific changes

## Dash Core v23.1.9 base

PirateCash Core v23.1.9 is built from the Dash Core v23.1.9 codebase. Upstream
changes are adapted without replacing PirateCash consensus, network parameters,
staking, rewards or Corsa integration.

The following sections describe integration and migration behavior introduced
in earlier PirateCash releases and retained in v23.1.9.

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
v23.1.9 for mixing, masternode list handling and ChainLocks operation.

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
- build, test and documentation fixes inherited from Dash Core v23.1.9

## Backports from Bitcoin Core

This release includes many updates from Bitcoin Core v0.18 through v0.21, as
well as selected updates from Bitcoin Core v22 and newer versions. Changes that
do not align with Dash or PirateCash network behavior, such as SegWit and RBF, are
excluded from these backports.


# v23.1.9 Change log

PirateCash Core v23.1.9 is based on Dash Core v23.1.9.

For the upstream Dash Core patch changes included in this release, see:

- [Upstream Dash Core changes][set-of-changes]

PirateCash-specific changes are tracked in the PirateCash Core repository history:

- <https://github.com/piratecash/piratecash>


# Credits

Thanks to everyone who directly contributed to this release, submitted issues,
reviewed pull requests, helped with release candidates, maintained
infrastructure, or helped translate the project.

The upstream Dash Core v23.1.9 changes merged into this release include
contributions from:

- Konstantin Akimov
- PastaClaw
- PastaPastaPasta
- UdjinM6

Thanks also go to Dash Core and Bitcoin Core developers for the upstream work
this release builds on.


# Older releases

PirateCash was forked from Dash Core.

These upstream Dash Core releases are considered obsolete. Their release
notes can be found here:

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

[set-of-changes]: https://github.com/dashpay/dash/compare/v23.1.8...dashpay:v23.1.9
