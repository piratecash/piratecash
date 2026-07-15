# PirateCash Core version v20.1.1

Official release:

  <https://github.com/piratecash/piratecash/releases/tag/v20.1.1-pirate>

PirateCash Core v20.1.1 is a mandatory network upgrade based on the Dash Core
v20 series. PirateCash ships this series as one release, so the applicable Dash
Core changes from v20.0.0 through v20.1.1 are backported into PirateCash and
summarized below.


# Upgrading and downgrading

Back up the wallet and configuration, shut down the previous node cleanly and
wait for it to stop before replacing the binaries. All node operators, miners,
exchanges and masternode operators should upgrade.

Downgrading to an older major version may require a reindex or full resync.


# Release Notes

## Backported from Dash Core v20.0.0 through v20.1.1

The changes in this section were developed in Dash Core and backported into
PirateCash Core. They are adapted without replacing PirateCash Proof-of-Stake,
scrypt hashing, network parameters, staking or Corsa behavior.

### Evo and Platform transactions

- Added Asset Lock transactions for locking funds into the Platform credit pool
  and support for Asset Unlock transactions.
- Added Coinbase transaction v3 and the Platform/Evo fields
  `bestCLSignature`, `bestCLHeightDiff` and `creditPoolBalance`.
- Added quorum ChainLock signatures to `protx diff` and related simplified
  masternode list processing.
- Updated HPMN-facing RPC terminology toward Evo names and added Evo filters to
  `protx list` and `masternodelist`.
- Added Enhanced Hard Fork activation support used by applicable network and
  test deployments.

### Quorums, networking and protocol

- Added a ChainLock-based random beacon for LLMQ member selection.
- Integrated the upstream Sentinel model into Core.
- Added I2P support and removed obsolete Tor v2, BIP61 reject messages,
  `LEGACYTXLOCKREQUEST` and `NODE_GETUTXO` behavior.
- Removed non-deterministic legacy InstantSend and its legacy `islock` message
  and inventory handling.
- Updated transaction rebroadcast and mempool unbroadcast tracking.
- Added repeated ZeroMQ notification options.

### RPC, wallet and GUI

- Added or updated `addpeeraddress`, `getindexinfo`, `protx listdiff`,
  `submitchainlock`, `getassetunlockstatuses`, `quorum dkginfo`,
  `getrawtransactionmulti`, `gettxchainlocks` and verbose transaction decoding.
- Added activation and chain-tip time data to `getblockchaininfo`, connection
  and activity fields to `getpeerinfo`, and the unbroadcast transaction count to
  `getmempoolinfo`.
- `testmempoolaccept` reports fee and virtual size and supports multiple
  transactions. Wallet funding RPCs handle manually selected locked coins more
  consistently.
- HD wallets are enabled by default in the Dash v20.1 model.
- Added granular CoinJoin options, Discreet Mode and a Qt action to close all
  wallets.
- Fixed wallet notifications for block-conflicted transactions and transaction
  creation without change when the keypool is empty.

### Stability, build and release

- Fixed an RPC work-queue deadlock that could leave nodes unresponsive with a
  `Work depth queue exceeded` message.
- Fixed DKG PoSe scoring, old quorum cleanup memory use, selected v19-to-v20
  upgrade crashes, `getspecialtxes` lock reporting and regtest Asset Unlock
  signing-quorum selection.
- Added Guix deterministic builds, replacing the upstream Gitian flow.
- Updated Windows signing, macOS debug symbols, FreeBSD compilation, CI and
  headless Docker packaging.
- Included applicable Bitcoin Core backports from v0.20 through v25.


# PirateCash integration

The Dash v20 backports were integrated while preserving PirateCash consensus,
Proof-of-Stake, scrypt hashing, network parameters, staking, rewards, wallet
behavior and Corsa services.


# v20.1.1 Change log

- [PirateCash v20.1.1 release](https://github.com/piratecash/piratecash/releases/tag/v20.1.1-pirate)
- [PirateCash changes since v19.1.2](https://github.com/piratecash/piratecash/compare/v19.1.2-pirate...v20.1.1-pirate)

Detailed upstream release notes:

- [Dash Core v20.0.0](../dash/release-notes-20.0.0.md)
- [Dash Core v20.0.1](../dash/release-notes-20.0.1.md)
- [Dash Core v20.0.2](../dash/release-notes-20.0.2.md)
- [Dash Core v20.0.3](../dash/release-notes-20.0.3.md)
- [Dash Core v20.0.4](../dash/release-notes-20.0.4.md)
- [Dash Core v20.1.0](../dash/release-notes-20.1.0.md)
- [Dash Core v20.1.1](../dash/release-notes-20.1.1.md)


# Credits

Thanks to PirateCash contributors and to the Dash Core and Bitcoin Core
developers whose upstream work was backported into this release.


# Older releases

- [PirateCash Core v19.1.2](release-notes-19.1.2.md)
