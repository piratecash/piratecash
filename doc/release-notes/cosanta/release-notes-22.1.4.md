# PirateCash Core version v22.1.4

Official release:

  <https://github.com/piratecash/piratecash/releases/tag/v22.1.4-pirate>

PirateCash Core v22.1.4 updates the codebase from the Dash Core v21 line to
Dash Core v22.1.3. PirateCash ships this upstream series as one release, so the
applicable Dash Core changes from v22.0.0 through v22.1.3 are backported into
PirateCash and summarized below.

This is a mandatory mainnet upgrade. Node operators should upgrade before the
MN_RR activation at block 1,910,840.


# Upgrading and downgrading

Back up the wallet and configuration, shut down the previous node cleanly and
wait for it to stop before replacing the binaries with PirateCash Core v22.1.4.
A reindex is not normally required.

Downgrading to a version based on Dash Core older than v22.0.0 is not
recommended after the new consensus rules activate and may require a reindex or
full resync.


# Release Notes

## Backported from Dash Core v22.0.0 through v22.1.3

The changes in this section were developed in Dash Core and backported into
PirateCash Core. They are adapted to PirateCash network parameters,
Proof-of-Stake, scrypt hashing, staking, rewards and Corsa integration.

### Encrypted P2P transport

- Added BIP324 version 2 encrypted P2P transport. It was experimental in Dash
  Core v22.0.0 and became the default in v22.1.0.
- Connections automatically fall back to the legacy v1 transport when the
  remote peer does not support v2.
- Outbound connection diversity was improved across IPv4, IPv6, Tor and other
  available networks. Onion-capable nodes maintain protected outbound onion
  connections.
- Connections to unsafe or commonly abused service ports are blocked.

### Asset Unlock and withdrawals

- Updated Asset Unlock transaction validation and Platform transfer handling.
- Added support for all active withdrawal quorums and the most recent inactive
  quorum.
- Increased the withdrawal limit per credit-pool period.
- Added the `withdrawals` consensus deployment. PirateCash uses its own
  activation parameters for this deployment.

### Masternodes, quorums and InstantSend

- Improved deterministic masternode list performance, including faster
  `protx diff` processing.
- Optimized `quorum rotationinfo` and `GETQUORUMROTATIONINFO` processing.
- Fixed incorrect `baseBlockHash` handling in quorum rotation responses and
  corrected the `cycleHash` carried by InstantSend lock messages.
- Improved masternode payment destination validation. Invalid destinations now
  produce a validation error instead of a crash.

### RPC, wallet and GUI

- Added `getislocks` for retrieving InstantSend lock data.
- `getbestchainlock` now includes hexadecimal ChainLock data.
- `lockunspent` supports persistent UTXO locks, and UTXOs locked through the GUI
  remain locked after restarting the wallet.
- Governance voting RPCs support descriptor wallets.
- Added the `coinjoinsalt` wallet-management RPC.
- Improved transaction credit-output reporting. Deprecated top-level mempool
  fee fields were replaced by the `fees` object.
- Improved GUI responsiveness for large wallets and fixed several wallet,
  CoinJoin and simplified masternode list edge cases.

### Networking, performance and build

- Increased the compressed-header request limit and reduced DSQ bandwidth by
  switching to inventory-based relay.
- Fixed v2-to-v1 downgrade handling for masternode connections, reduced
  unnecessary connections and masternode load, and strengthened eclipse and
  network-partition protections.
- Optimized versionbits and block validation, including processing during chain
  reorganizations.
- Improved functional tests, CI and FreeBSD builds.
- The minimum supported glibc version is 2.31. Upstream prebuilt binaries no
  longer support Ubuntu 18.04 or RHEL 8, and macOS releases use notarized ZIP
  archives.


# PirateCash-specific changes

- Preserved PirateCash Proof-of-Stake, scrypt, Corsa, wallet and network behavior
  on top of the Dash Core v22.1.3 codebase.
- Configured mainnet MN_RR activation at block 1,910,840.
- Configured the mainnet `withdrawals` deployment to begin on August 14, 2026 at
  00:00 UTC.
- Fixed historical MN_RR bit 10 handling after burial, preventing
  `bad-mnhf-duplicate` errors during chain replay.
- Improved postponed PoS header handling and fixed a possible staking deadlock.
- Improved wallet access to masternode synchronization and peer state.


# v22.1.4 Change log

- [PirateCash v22.1.4 release](https://github.com/piratecash/piratecash/releases/tag/v22.1.4-pirate)
- [PirateCash changes since v21.1.1](https://github.com/piratecash/piratecash/compare/v21.1.1-pirate...v22.1.4-pirate)

Detailed upstream release notes:

- [Dash Core v22.0.0](../dash/release-notes-22.0.0.md)
- [Dash Core v22.1.0](../dash/release-notes-22.1.0.md)
- [Dash Core v22.1.1](../dash/release-notes-22.1.1.md)
- [Dash Core v22.1.2](../dash/release-notes-22.1.2.md)
- [Dash Core v22.1.3](../dash/release-notes-22.1.3.md)


# Credits

Thanks to PirateCash contributors and to the Dash Core and Bitcoin Core
developers whose upstream work was backported into this release.


# Older releases

- [PirateCash Core v21.1.1](release-notes-21.1.1.md)
