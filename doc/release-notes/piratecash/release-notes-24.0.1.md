# PirateCash Core v24.0.1

PirateCash Core v24.0.1 is based on Dash Core v24.0.2 and includes the fixes
from Dash Core v24.0.1 and v24.0.2. It adds checkpoint-based header
synchronization and the PirateCash PoS v3 network upgrade.

## Upgrading

Shut down the running node or wallet before replacing the binaries. Upgrade
before the PoS v3 activation height for your network. Users coming from
v23.1.x or older should also read the index migration and configuration
instructions in the [PirateCash Core v24.0.0 release notes](release-notes-24.0.0.md).
Versions older than v19.0.0 also require a chain migration on first startup.

Masternode operators still need a local authenticated Corsa service with
protocol version 31 or later. See [PIP-0001](../../pips/pip-0001.md).

## PirateCash changes

### Header synchronization

- Below the last checkpoint, nodes download headers in segments and add each
  segment to the block index only after its final hash matches a checkpoint.
  Mainnet and testnet checkpoints follow a 50,000-block grid, with an
  additional checkpoint at mainnet block 1,974,444 and testnet block 1,280,000.
- Above the last checkpoint, nodes defer headers whose stake cannot yet be
  checked and retry headers received too early. A temporary failure does not
  penalize the peer.
- Full and pruned nodes can check stake on competing branches without a
  transaction index, using the active UTXO set and available undo data.
- The wallet waits until coinbase outputs satisfy the stake maturity rule
  before using them. During initial sync, PirateCash-Qt estimates the
  remaining blocks using header progress.

### PoS v3

PoS v3 activates by block height. Upgrade before your network reaches the
height below. Earlier versions cannot follow PoS v3 blocks.

| Network | Activation height |
| --- | ---: |
| Mainnet | 1,989,475 |
| Testnet | 1,281,787 |
| Devnet | 78,000 |

PoS v3 uses 10-second staking slots and a 256-bit stake modifier. Staking
wallets search eligible inputs for the earliest slot. Keep your node's clock
synchronized, since the timestamp tolerance after activation is nine seconds.
Staking still requires a legacy wallet.

Regtest can activate PoS v3 with `-testactivationheight=posv3@<height>`.
The regtest `-posparams` option now takes two fields,
`<last-pow-height>:<minimum-stake-age-seconds>`. Historical blocks retain their
existing validation rules.

## Fixes from Dash Core v24.0.1 and v24.0.2

### Masternodes and quorums

- DKG keeps verification vectors in memory during the commit phase and
  processes local DKG messages before queued peer messages. Both changes help
  large quorums finish in time. (#8135, #8130)
- Mineable quorum commitments stay in the pool when a block containing them is
  rejected for an unrelated reason. (#8104)
- ChainLocks shuts down before the rest of the node. Signature-share batches
  are handled consistently when a referenced quorum is unavailable. (#8117)

### Governance

- Masternodes wait for a trigger matching their own candidate before voting
  against competing superblock triggers, and votes from an unfunded cycle no
  longer interfere with the next one. (#8131)
- On test networks, restarting during a superblock maturity window preserves
  the stored triggers. Governance votes also remain associated with the
  correct masternode through a chain reorganization. (#8097, #8100)
- Governance sync replies go only to the requesting peer. Votes received before
  their governance objects can be served once the objects arrive. (#8112)

### Node, wallet and GUI

- Fixed memory growth during normal network activity and high CPU usage when a
  peer's send buffer is full. (#8115, #8094)
- The REST block JSON endpoint handles provider transactions whose payloads
  cannot be decoded. (#8099)
- CoinJoin schedules automatic mixing separately per wallet and handles a
  reduction in mixing rounds during a session. (#8113)
- A released shared masternode contribution can be reserved again. The macOS
  menu bar is cleared during shutdown to prevent crashes from stale menu
  actions. (#8052, #8073)
- The `transactions.inputValue` statistic reports the inputs spent. Fixes
  also address statistics collection before a chain tip exists and concurrent
  message counters. Nodes without `-statshost` skip per-block and
  per-transaction statistics. (#8119)
- `piratecash-cli` and `piratecash-tx` accept `-printcrashinfo`. The build
  uses a working MiniUPnPc source URL, and the Qt tests handle distribution
  builds that use the GLib event loop. (#8120, #8088, #8080)

## References

- [PirateCash changes since v24.0.0](https://github.com/piratecash/piratecash/compare/v24.0.0-pirate...v24.0.1-pirate)
- [Dash Core v24.0.1 release notes](../dash/release-notes-24.0.1.md)
- [Dash Core v24.0.2 release notes](../dash/release-notes-24.0.2.md)
- [Download PirateCash Core](https://p.cash/en/download/)
- [Report an issue](https://github.com/piratecash/piratecash/issues)
