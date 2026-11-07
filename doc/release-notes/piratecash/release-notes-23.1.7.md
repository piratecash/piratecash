# PirateCash Core version v23.1.7

Official release:

  <https://github.com/piratecash/piratecash/releases/tag/v23.1.7-pirate>

PirateCash Core v23.1.7 updates the codebase from the Dash Core v22 line to
Dash Core v23.1.7. The applicable upstream changes from v23.0.0 through
v23.1.7 are backported into PirateCash and summarized below.


# Upgrading and downgrading

Back up the wallet and configuration, shut down the previous node cleanly and
wait for it to stop before replacing the binaries with PirateCash Core v23.1.7.

The v23 codebase introduces an EvoDB migration and a new compact block filter
index format. Existing block filter indexes are automatically rebuilt.
Downgrading to a version based on Dash Core older than v23.0.0 is not supported
and requires a reindex.


# Release Notes

## Backported from Dash Core v23.0.0 through v23.1.7

The changes in this section were developed in Dash Core. Detailed upstream
release notes are linked below for the complete change descriptions.

### EvoDB and block filters

- Introduced a new internal masternode-state format supporting extended
  addresses, with automatic migration to the new format.
- Added `evodb verify` and `evodb repair` RPCs, automatic verification and
  repair of EvoDB diffs at startup, and `-forceevodbrepair` for re-verification.
- Fixed legacy-diff BLS scheme selection and skipped unnecessary EvoDB repair
  during reindexing.
- Updated compact block filters to include provider transaction fields and
  Asset Lock credit outputs, with index versioning to detect incompatible data.

### InstantSend, quorums and network performance

- Prioritized quorum-related network messages through a dedicated processing
  queue and proactively relayed recovered signatures.
- Added parallel signing-share processing, block-height caching and shared
  mutexes to reduce contention during signing and lock verification.
- Removed redundant signature checks and lock inventory announcements, while
  retaining InstantSend inventory relay to non-masternode peers that request
  recovered signatures.
- Fixed ChainLock processing races, avoided unbounded collection of block
  transaction IDs during initial sync, and serialized chain-tip signing.
- Improved block validation and header-sync performance and reduced lock
  contention when penalizing peers for invalid transactions.

### Masternode RPCs and extended addresses

- Added extended masternode address support to provider transaction RPCs and
  reporting, including `coreP2PAddrs`, `platformP2PAddrs` and
  `platformHTTPSAddrs`. Its use remains subject to activation and version rules.
- Deprecated legacy address-reporting fields in favor of the `addresses`
  representation and added an optional `submit` argument to provider updates.
- Corrected BLS scheme selection in provider revocation and service updates.
- Added the request `id` to `getislocks` results and introduced `coinjoin status`.
- Restricted DKG status reporting to masternodes and watch-only quorum nodes.
- Removed the previously deprecated `instantsendtoaddress`, `getpoolinfo`,
  `masternode current` and `masternode winner` RPCs.

### Wallet and general RPCs

- Promoted descriptor wallets from experimental status and added experimental
  support for HWI-compatible external signers.
- Improved HD-chain loading and wallet encryption, and allowed null characters
  in wallet and mnemonic passphrases.
- Added `getblock` verbosity 3 with input `prevout` information, the `newkeypool`
  RPC, and wallet `lastprocessedblock` reporting.
- Added a `coinjoin` marker and `next_index` to descriptor reporting.
- Included coinbase transactions in the received-by-address and received-by-label
  RPCs, with an option to include immature coinbase transactions.
- Made CoinJoin denomination creation respect `avoid_reuse` and shared
  masternode-selection tracking across loaded wallets.
- Added the `abandoned` field consistently to transaction reporting and prevented
  wallet unload while a rescan is running.

### Wallet GUI

- Redesigned the masternode list with status icons, type and ban filters,
  context menus and a detailed description dialog.
- Added runtime visibility controls for the Governance and Masternode tabs and
  persisted masternode filter preferences.
- Added dust attack protection, duplicate-recipient warnings, mnemonic backup
  verification, recovery-phrase display and wallet restoration from backups.
- Moved GUI configuration into `settings.json` so applicable settings are shared
  with the daemon.
- Added governance voting, proposal information and budget visualization,
  dedicated information and network widgets, and Data Transaction reporting.
- Fixed external-signer Send button visibility, font scaling, list sorting,
  proposal precision and theme-related crashes.

### Transaction policy and security hardening

- Treated Asset Locks exceeding the supported input or size limits, or using
  the version 2 payload, as nonstandard while retaining the consensus distinction.
- Prevented simultaneous mempool registrations or service updates with the same
  Platform node ID and avoided repeated EHF signal validation during connection.
- Bounded pushed DKG messages, accepted them only from verified masternodes and
  validated their structure before retaining them.
- Fixed malformed DKG signatures and missing verification vectors triggering
  assertion failures or null-pointer access.
- Bounded InstantSend lock input sets and pending queues, and rejected oversized
  governance vote-sync bloom filters before expensive processing.
- Fixed a networking backpressure condition that could keep the socket-handler
  thread spinning at full CPU usage.
- Rejected identity elements during BLS deserialization and key generation.

### Networking, dependencies and build

- Added UNIX-domain socket support for proxy and selected ZMQ endpoints.
- Updated encrypted P2P transport negotiation and compact message identifiers.
- Enabled compact block filter service and indexing by default for masternodes,
  and strengthened startup validation of network ports and statistics options.
- Updated REST header endpoints to accept an optional result-count query.
- Updated Qt to 5.15.18, including upstream security fixes, and improved Debian
  13 and GCC 16 build compatibility.


# v23.1.7 Change log

- [PirateCash v23.1.7 release](https://github.com/piratecash/piratecash/releases/tag/v23.1.7-pirate)
- [PirateCash changes since v22.1.4](https://github.com/piratecash/piratecash/compare/v22.1.4-pirate...v23.1.7-pirate)

Detailed upstream release notes:

- [Dash Core v23.0.0](../dash/release-notes-23.0.0.md)
- [Dash Core v23.0.2](../dash/release-notes-23.0.2.md)
- [Dash Core v23.1.0](../dash/release-notes-23.1.0.md)
- [Dash Core v23.1.2](../dash/release-notes-23.1.2.md)
- [Dash Core v23.1.3](../dash/release-notes-23.1.3.md)
- [Dash Core v23.1.4](../dash/release-notes-23.1.4.md)
- [Dash Core v23.1.5](../dash/release-notes-23.1.5.md)
- [Dash Core v23.1.7](../dash/release-notes-23.1.7.md)


# Credits

Thanks to PirateCash contributors and to the Dash Core and Bitcoin Core
developers whose upstream work was backported into this release.


# Older releases

- [PirateCash Core v22.1.4](release-notes-22.1.4.md)
