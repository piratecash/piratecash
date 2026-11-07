# PirateCash Core version v19.1.1

Official release:

  <https://github.com/piratecash/piratecash/releases/tag/v19.1.1-pirate>

PirateCash Core v19.1.1 is a backward-compatible bug-fix release for the v19
codebase. It does not introduce another Dash release range; it fixes
PirateCash-specific PoS header serialization on top of v19.1.0.


# Upgrading

Back up the wallet and configuration, shut down the previous node cleanly and
wait for it to stop before replacing the binaries. No reindex or on-disk format
change is required for this update.


# Bug fixes

## Preserve the PoS signature in `CBlock::GetBlockHeader()`

`CBlock::GetBlockHeader()` copied `posStakeHash` and `posStakeN` but omitted
`posBlockSig`. Code paths that rebuilt and serialized a `CBlockHeader` from a
`CBlock` therefore produced headers with an empty PoS signature and could fail
with `bad-pos-sig, missing PoS signature`.

The fix preserves all three PoS fields, matching
`CBlockIndex::GetBlockHeader()`.

Affected paths included:

- `HEADERS2` compressed-header relay, where peers rejected the reconstructed
  header and discouraged the sender. Headers-first synchronization could fall
  back to full block or compact block delivery.
- `CMerkleBlock` construction, where SPV clients could receive PoS headers with
  empty signatures.

The wire format, consensus rules, block hashes and on-disk format are unchanged.
Nodes can upgrade independently. Older nodes continue to emit broken
`HEADERS2` data, while upgraded nodes send correctly signed headers.


# v19.1.1 Change log

- [PirateCash v19.1.1 release](https://github.com/piratecash/piratecash/releases/tag/v19.1.1-pirate)
- [Changes since v19.1.0](https://github.com/piratecash/piratecash/compare/v19.1.0-pirate...v19.1.1-pirate)


# Older releases

- [PirateCash Core v19.1.0](release-notes-19.1.0.md)
