# PirateCash Core 19.0.0 Release Announcement

We are happy to announce the release of PirateCash Core 19.0.0. This release
includes binaries that you can download below.

## About this release

PirateCash Core 19.0.0 is the first major release of the PirateCash Core 19.x.x
series. This release is based on Dash Core 19.3.0 and includes the Dash Core
19.0.0 through 19.3.0 feature set, improvements and bug fixes adapted for the
PirateCash network. We consider this a stable release.

Detailed release notes are available in
[doc/release-notes.md](release-notes.md).

When upgrading from a version older than 19.0.0, a migration process will occur.
Although expected to complete quite quickly, this migration process can take up
to thirty minutes to complete on some systems.

This release is mandatory for all nodes. Masternode operators should upgrade
Sentinel to 1.7.3 or newer if Sentinel is used as part of their deployment.

This release also activates Stage 1 of
[PIP-0001](pips/pip-0001.md), making local Corsa messenger integration a
mandatory startup requirement for PirateCash masternodes. A masternode must be
configured with a local authenticated Corsa RPC endpoint before it can start.

## Downloads

Binaries are available from:

<https://p.cash/en/download/>

## Verification

It is important to verify the binaries you download. Use the signed
`SHA256SUMS.asc` file published with the release and compare the SHA256 hash of
your downloaded binary before installing.

- Windows
- macOS
- Linux

## Credits

Thanks go out to all PirateCash Core contributors, infrastructure maintainers,
everyone who submitted issues, reviewed pull requests, helped translate the
project, or tested release candidates.

Thanks also go to Dash Core and Bitcoin Core developers for the upstream work
this release builds on.
