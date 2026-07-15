# PirateCash Core version v19.1.0

Official release:

  <https://github.com/piratecash/piratecash/releases/tag/v19.1.0-pirate>

PirateCash Core v19.1.0 incorporates the applicable changes from the complete
Dash Core v19 release line, from v19.0.0 through v19.3.0, as one PirateCash
release. The upstream changes are backported and adapted to PirateCash
Proof-of-Stake, scrypt hashing, network parameters, staking and Corsa services.


# Upgrading and downgrading

Back up the wallet and configuration, shut down the previous node cleanly and
wait for it to stop before replacing the binaries.

Downgrading below the v19 database and BLS migration may require a reindex or
full resync.


# Release Notes

## Backported from the Dash Core v19 release line

### High-Performance Masternodes and BLS

- Added the high-performance masternode type for Platform services alongside
  ChainLocks and InstantSend responsibilities.
- Migrated remaining BLS public key and signature usage to the basic BLS scheme,
  with versioned serialization for quorum commitments, simplified masternode
  lists, ProTx transactions and Enhanced Hard Fork transactions.
- Added high-performance masternode registration and service-update RPCs,
  masternode type reporting and related quorum/BLS response fields.

### Wallet, network and RPC

- Removed automatic wallet creation at startup. Wallets are created explicitly
  through the GUI, `piratecash-wallet create` or `createwallet`.
- Removed BIP61 reject messages and the `-enablebip61` option.
- Updated CoinJoin messages to improve light-client mixing support.
- Added legacy-scheme ProTx registration RPCs, `cleardiscouraged`,
  `upgradewallet`, high-performance masternode RPCs and new masternode list
  modes.
- Included applicable Bitcoin Core updates from v0.18 through v0.21 and selected
  newer backports, excluding incompatible SegWit and RBF behavior.

### Dash v19.2 migration and compatibility fixes

- Reworked BLS public-key storage and migration after edge cases found during
  upstream v19 activation.
- Added per-entry versioning to simplified masternode list diffs so light
  clients can deserialize legacy and basic BLS keys and verify historical
  masternode roots.
- Improved CoinJoin verification for light clients.
- Allowed ChainLocks to remain enforced while signing of new ChainLocks is
  disabled.
- Fixed database-corruption reindex startup, long mnemonic passphrases, disabled
  wallet GUI settings, sensitive key logging and long-term memory usage.

### Dash v19.3 wallet and node improvements

- Fixed CoinJoin operation when the node starts without a loaded wallet.
- Improved rescans and long-running wallet operations for large wallets.
- Added the `wipewallettxes` RPC and `piratecash-wallet wipetxes` command.
- Added HPMN/Evo filters to `masternodelist` and `protx list`.
- Allowed `-blockversion` on non-mainnet networks.
- Fixed empty `settings.json` startup, false unknown-rule warnings and several
  block-processing and test issues; updated the BLS library to version 1.3.0.


# PirateCash-specific changes

## Historical LLMQ_60_75 synchronization

Historical mainnet `LLMQ_60_75` selection before v19 activation now uses the
v18 quorum-picking behavior. This preserves the quorums mined by older
PirateCash nodes and prevents `bad-qc-invalid`, cascading PoSe penalties and
`bad-cbtx-mnmerkleroot` failures while replaying the historical chain. After
v19 activation, nodes use the upstream v19 algorithm.

## PIP-0003 transaction policy, Stage 1

The historical and consensus path retains `MAX_LEGACY_TX_SIZE = 2,900,000`, so
existing blocks remain valid. New transactions use the Dash-aligned
`MAX_STANDARD_TX_SIZE = 100,000` policy in standardness checks, local
submission, wallet creation and the orphan pool. See
[PIP-0003](../../pips/pip-0003.md) for the complete rollout plan.

## RPC, staking and chain parameters

- Restored the upstream `gettxoutsetinfo` and `dumptxoutset` behavior, including
  cancellable UTXO scans, the full `hash_type` API and corrected snapshot and
  `coins_written` reporting.
- Applied `-stake*`, `-poshashinterval` and `-inputstakeprotect` options to
  wallet state so command-line overrides take effect.
- Updated mainnet checkpoint data, minimum chain work and assumed-valid data to
  block 1,841,000.
- Reduced release-branch CI noise and removed the obsolete four-component
  `CLIENT_VERSION_REVISION` release macro.


# v19.1.0 Change log

- [PirateCash v19.1.0 release](https://github.com/piratecash/piratecash/releases/tag/v19.1.0-pirate)
- [PirateCash changes since v19.0.1](https://github.com/piratecash/piratecash/compare/v19.0.1-pirate...v19.1.0-pirate)

Detailed upstream release notes:

- [Dash Core v19.0.0](../dash/release-notes-19.0.0.md)
- [Dash Core v19.1.0](../dash/release-notes-19.1.0.md)
- [Dash Core v19.2.0](../dash/release-notes-19.2.0.md)
- [Dash Core v19.3.0](../dash/release-notes-19.3.0.md)


# Credits

Thanks to PirateCash contributors and to the Dash Core and Bitcoin Core
developers whose upstream work was backported into this release.


# Older releases

- [PirateCash Core v19.0.1](https://github.com/piratecash/piratecash/releases/tag/v19.0.1-pirate)
