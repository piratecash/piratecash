# PirateCash Core version v21.1.1

Official release:

  <https://github.com/piratecash/piratecash/releases/tag/v21.1.1-pirate>

PirateCash Core v21.1.1 is a mandatory network upgrade based on the Dash Core
v21 series. PirateCash ships this series as one release, so the applicable Dash
Core changes from v21.0.0 through v21.1.1 are backported into PirateCash and
summarized below.


# Upgrading and downgrading

Back up the wallet and configuration, shut down the previous node cleanly and
wait for it to stop before replacing the binaries. All node operators, miners,
exchanges and masternode operators should upgrade.

Masternode operators must run Corsa v1.0.62 or newer.

Downgrading to an older major version may require a reindex or full resync.


# Release Notes

## Backported from Dash Core v21.0.0 through v21.1.1

The changes in this section were developed in Dash Core and backported into
PirateCash Core. They are adapted without replacing PirateCash Proof-of-Stake,
scrypt hashing, network parameters, staking or Corsa behavior.

### Wallet and RPC

- Added experimental descriptor wallet support while retaining legacy wallets.
- Added the `send` and `listdescriptors` RPCs and descriptor-aware options to
  `createwallet`.
- Added Avoid Partial Spends improvements and the `-maxapsfee` option.
- Added `-networkactive` and `setnetworkactive` support.
- Added the `quorum signplatform` RPC flow.
- Updated `getnetworkinfo` connection fields and removed deprecated
  `getaddressinfo` fields and old `-banscore` handling.
- Improved Platform Transfer and Asset Unlock decoding in `gettransaction`,
  `getblock`, `getblockstats`, masternode payment RPCs and `creditOutputs` JSON.

### Masternodes, quorums and protocol

- Added applicable masternode, Evo and Enhanced Hard Fork signing updates.
- Fixed evonode payment prediction and transaction retrieval before mining in
  affected client flows.
- Hardened upstream mainnet spork handling.
- Updated quorum, ChainLock and Asset Unlock processing through the v21.0.x and
  v21.1.x patch releases.

### Wallet GUI, network and build

- Included Dash v21 coin selection, APS and transaction creation improvements.
- Fixed persistence of the CoinJoin denomination GUI option and Platform
  Transfer display.
- Included peer, addrman, compact block, I2P, RPC queue, mempool and networking
  improvements, together with updated peer-misbehavior handling and removal of
  obsolete behavior.
- Added Docker SBOM and provenance support from the Dash release flow.
- Updated Guix builds, CI, packaging, seed tools and compatibility with newer
  Ubuntu and Clang environments.


# PirateCash-specific changes

- Re-applied PirateCash PoS block and transaction formats on top of the v21
  codebase, including PoS proof checks and index metadata persistence.
- Replaced upstream X11 hashing paths with PirateCash scrypt hashing.
- Wired PirateCash staking options and Corsa services into the v21 node.
- Configured PirateCash network synchronization behavior and the mainnet v19
  activation height.
- Set the default Platform ports for Evo nodes.
- Aligned PirateCash RPC and policy behavior with the v21 codebase.
- Updated seeds, developer tools, packaging, documentation, Qt branding and
  translations.
- Aligned functional tests and CI/build infrastructure with the v21 release
  branch and reduced block creation log noise.


# v21.1.1 Change log

- [PirateCash v21.1.1 release](https://github.com/piratecash/piratecash/releases/tag/v21.1.1-pirate)
- [PirateCash changes since v20.1.1](https://github.com/piratecash/piratecash/compare/v20.1.1-pirate...v21.1.1-pirate)

Detailed upstream release notes:

- [Dash Core v21.0.0](../dash/release-notes-21.0.0.md)
- [Dash Core v21.0.2](../dash/release-notes-21.0.2.md)
- [Dash Core v21.1.0](../dash/release-notes-21.1.0.md)
- [Dash Core v21.1.1](../dash/release-notes-21.1.1.md)


# Credits

Thanks to PirateCash contributors and to the Dash Core and Bitcoin Core
developers whose upstream work was backported into this release.


# Older releases

- [PirateCash Core v20.1.1](release-notes-20.1.1.md)
