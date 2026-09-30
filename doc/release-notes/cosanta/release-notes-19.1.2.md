# PirateCash Core version v19.1.2

Official release:

  <https://github.com/piratecash/piratecash/releases/tag/v19.1.2-pirate>

PirateCash Core v19.1.2 is a PirateCash-specific PoS and `HEADERS2`
synchronization bug-fix release on top of v19.1.1. It does not introduce an
additional Dash release range.


# Upgrading

Back up the wallet and configuration, shut down the previous node cleanly and
wait for it to stop before replacing the binaries.

This update is especially important for fresh nodes, nodes using `-reindex` or
`-reindex-chainstate`, nodes with a removed block index and nodes that must
retrieve historical headers below the V1-to-V2 PoS fork height.


# Bug fixes

## Restore the PoS marker after compressed-header decoding

`nFlags` is disk-only and is not transmitted in compressed headers. Historical
V1 PoS blocks also carry neither `POS_BIT` nor `POSV2_BITS` in `nVersion`. After
decoding such a header, the receiver therefore had `nFlags=0` and
`IsProofOfStake()` returned false.

`AcceptBlockHeader` then incorrectly performed a Proof-of-Work check and
rejected the PoS header with `high-hash, proof of work failed`, breaking
headers-first synchronization across the V1 PoS range and causing peers to be
discouraged.

`CompressibleBlockHeader::Uncompress()` now restores the PoS marker from the
compressed header bit field, symmetrically with the sender. A regression test
round-trips a V1 PoS header through compression, serialization, decoding and
uncompression and verifies that the marker survives.

Affected historical ranges are testnet below height 289,000 and mainnet below
height 1,265,800. Already synchronized nodes receiving V2 PoS headers at the tip
are not affected because those headers carry `POSV2_BITS` in `nVersion`.


# v19.1.2 Change log

- [PirateCash v19.1.2 release](https://github.com/piratecash/piratecash/releases/tag/v19.1.2-pirate)
- [Changes since v19.1.1](https://github.com/piratecash/piratecash/compare/v19.1.1-pirate...v19.1.2-pirate)


# Older releases

- [PirateCash Core v19.1.1](release-notes-19.1.1.md)
