# PirateCash Core v24.0.2

PirateCash Core v24.0.2 is based on Dash Core v24.0.3. It includes the
upstream fixes for governance, indexes, wallets, CoinJoin, masternodes, RPC
and the GUI, together with PirateCash's Proof-of-Stake consensus, network
parameters, rewards and Corsa integration.

The checkpoint-based header synchronization and PoS v3 upgrade introduced
in PirateCash Core v24.0.1 remain included. The fixes from Dash Core v24.0.1
and v24.0.2 are also retained; see the
[PirateCash Core v24.0.1 release notes](release-notes/piratecash/release-notes-24.0.1.md).

## Upgrading

Shut down the running node or wallet before replacing the binaries.
Masternode operators still need a local authenticated Corsa service with
protocol version 31 or later. See [PIP-0001](pips/pip-0001.md).

Users coming from v23.1.x or older should also read the index migration and
configuration instructions in the
[PirateCash Core v24.0.0 release notes](release-notes/piratecash/release-notes-24.0.0.md).
Versions older than v19.0.0 also require a chain migration on first startup.

### PoS v3

PoS v3 activation heights are unchanged from PirateCash Core v24.0.1:

| Network | Activation height |
| --- | ---: |
| Mainnet | 1,989,475 |
| Testnet | 1,281,787 |
| Devnet | 78,000 |

Upgrade before your network reaches its activation height. Versions without
PoS v3 support cannot follow PoS v3 blocks. Staking still requires a legacy
wallet. Keep the node's clock synchronized: PoS v3 uses 10-second staking
slots and a timestamp tolerance of nine seconds.

## Fixes from Dash Core v24.0.3

### Governance

- Create Proposal dialog (Qt): the payment window is now based on the chosen
  superblocks instead of the time the proposal was created. Previously, a
  payment could come at a different superblock than the one chosen, or be
  missed. The dialog also no longer offers superblocks whose voting deadline
  has passed. (#8154)
- Hardened the cache of governance votes that arrive before their object.
  (#8122)

### Reindex and indexes

- The node now flushes all of its databases once a reindex completes, so an
  unclean stop right after a reindex no longer needs another reindex. A node
  that already fails to start for this reason needs one more reindex. (#8103)
- Fixed index startup after an unclean stop with `-addressindex`,
  `-spentindex` or `-timestampindex`. (#8047)

### Wallet

- Mnemonic validation now checks every BIP39 checksum bit, so mnemonics with
  an invalid checksum are refused instead of creating or restoring a different
  wallet. Existing wallet files are not affected, but such a mnemonic can no
  longer be used to restore one. (#8111)
- Fixed a race in block-filter rescans (`-blockfilterindex`) that could miss
  transactions when descriptors changed during the rescan. (#8139)

### CoinJoin

- Masternodes check participants' signatures against the final mixing
  transaction. (#7815)
- A participant with several unsigned inputs is now counted once when choosing
  whose collateral to charge. (#8116)
- Mixing clients reject queue announcements with an invalid denomination, as
  masternodes already do. (#7955)

### Masternodes, quorums and sporks

- Nodes now keep the final quorum commitment with the most signers. (#8085)
- A signature share dropped because the pending queue was full is requested
  again. (#7994)
- Spork updates signed within the same second are now accepted by other
  nodes. (#8127)
- Re-signing an InstantSend or ChainLock request restarts the signing
  session's timeout. (#8203)
- Quorum lists used for ChainLocks and InstantSend are now cached only for the
  active chain, so a node no longer needs a restart to accept ChainLocks after
  some reorgs. (#8102)

### GUI

- Address book, Coin Control, options dialog, PSBT dialog, CoinJoin send tab,
  shared masternode dialogs, wallet migration, proposal list, traffic graph and
  overview page fixes. (#8123, #8124)

### RPC

- Quorum RPCs reject quorum types outside 0..255 instead of truncating them.
  (#7954)
- `masternode payments`, `masternode winners`, `getspecialtxes`,
  `verifyislock`, `getaddressdeltas`, `evodb verify`/`repair` and
  `getblockheaders`: argument handling, range checks and locking fixes.
  (#8118, #8125)
- `getdeploymentinfo` and REST `/deploymentinfo` now return an error for
  blocks that are not fully validated. (#8098)

### Contrib

- Fixed the OpenRC init script, `contrib/zmq/zmq_sub.py` on Python 3.14 and
  IPv6 handling in `contrib/qos/tc.sh`. (#8109)

## References

- [PirateCash changes since v24.0.1](https://github.com/piratecash/piratecash/compare/v24.0.1-pirate...v24.0.2-pirate)
- [PirateCash Core v24.0.1 release notes](release-notes/piratecash/release-notes-24.0.1.md)
- [Dash Core v24.0.3 release notes](release-notes/dash/release-notes-24.0.3.md)
- [Dash changes from v24.0.2 to v24.0.3](https://github.com/dashpay/dash/compare/v24.0.2...v24.0.3)
- [Download PirateCash Core](https://p.cash/en/download/)
- [Report an issue](https://github.com/piratecash/piratecash/issues)
