# PirateCash Core version v23.1.8

Official release:

  <https://github.com/piratecash/piratecash/releases/tag/v23.1.8-pirate>

PirateCash Core v23.1.8 is a mandatory security and bug-fix release based on
Dash Core v23.1.8. It includes the applicable upstream peer-to-peer, RPC and
build fixes together with PirateCash-specific Proof-of-Stake validation and
staking-wallet improvements.


# Upgrading and downgrading

Back up the wallet and configuration, shut down the previous node cleanly and
wait for it to stop before replacing the binaries. Upgrading from v23.1.7 does
not normally require a reindex.

Downgrading to a version older than v23.0.0 is not supported due to database
changes and requires a reindex or full resync.


# Release Notes

## Dash Core v23.1.8 fixes

The applicable Dash Core v23.1.8 changes have been adapted without replacing
PirateCash consensus, network parameters, Proof-of-Stake, scrypt hashing,
staking, rewards or Corsa integration.

The upstream portion fixes three remotely reachable crashes in
provider-transaction mempool cleanup, unknown-LLMQ signature-share handling and
quorum commitments that reference a parentless block. It also adds further
bounds and resource limits to LLMQ, DKG, quorum-data, governance, CoinJoin,
bloom-filter, spork and compact-block message processing.

Additional upstream fixes cover Platform port reporting in `protx listdiff`,
the masternode-list PoSe score column, pixel-sized Qt fonts, build compatibility
and CI.

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

Previously, a staking output could settle just below twice
`-stakesplitthreshold`, after which small staking reward outputs could
accumulate without being combined. The wallet can now sweep small inputs into
a kernel that is already above the split threshold, and the normal split pass
turns the result back into threshold-sized outputs.

The new `-stakecombinemax=<n>` option sets the largest input, in PIRATE, that may
be swept in this situation. It defaults to `100`; `0` disables this additional
sweep, and values at or above `-stakesplitthreshold` are limited to one PIRATE
below that threshold. The effective value is reported by `getstakingstatus` as
`stakecombinemax`.

Auto-combine candidates must be confirmed, mature and still unspent. Selection
now respects `-reservebalance` and `-inputstakeprotect`, protecting the reserved
wallet balance and masternode collateral. Coinstake construction also observes
the block's remaining size and signature-operation budgets. When staking is
enabled, a configured `-blockmaxsize` below 2000 bytes produces a warning and
the minimum space required for a PoS block is reserved.


# v23.1.8 Change log

- [PirateCash v23.1.8 release](https://github.com/piratecash/piratecash/releases/tag/v23.1.8-pirate)
- [PirateCash changes since v23.1.7](https://github.com/piratecash/piratecash/compare/v23.1.7-pirate...v23.1.8-pirate)
- [Dash Core changes since v23.1.7](https://github.com/dashpay/dash/compare/v23.1.7...v23.1.8)


# Credits

Thanks to PirateCash contributors and to the Dash Core and Bitcoin Core
developers whose upstream work was backported into this release.


# Older releases

- [PirateCash Core v23.1.7](release-notes-23.1.7.md)
