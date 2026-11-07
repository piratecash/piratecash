# PirateCash Core version v23.1.12

Official release:

  <https://github.com/piratecash/piratecash/releases/tag/v23.1.12-pirate>

This release backports Dash Core v23.1.10. The changes below describe that
upstream release.

This is a new patch version release. It fixes a governance issue that could
stop a superblock from paying any proposals, and hardens DKG and BLS signature
verification against misbehaving quorum members and peers.

Upgrading is **strongly recommended** for all nodes, and required for
masternodes.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/piratecash/piratecash/issues>

# Upgrading and downgrading

## How to Upgrade

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/PirateCash-Qt (on Mac) or
piratecashd/piratecash-qt (on Linux).

## Downgrade warning

### Downgrade to a version < v23.0.0

Downgrading to a version older than v23.0.0 is not supported, and will
require a reindex.

# Release Notes

## Governance

- Masternodes no longer vote NO-FUNDING on a superblock trigger before that
  superblock's maturity window opens, when they have no candidate of their own
  to compare it against.
  These early votes could keep a trigger with the correct payments from
  reaching the funding threshold, so the superblock paid no proposals.
  Triggers that don't match a masternode's candidate still receive a NO vote
  once the window opens.
- Masternodes no longer vote on triggers until they are fully synced. A
  masternode restarting inside the maturity window could otherwise vote NO on
  the correct trigger before it was able to build its own candidate.

Masternodes running older versions still cast the early NO votes, so this
protection becomes effective as masternodes upgrade. Masternode operators
should upgrade before the next superblock.

## Security and denial-of-service hardening

These issues do not affect consensus and do not put funds at risk.

- BLS batch verification (signature shares, recovered signatures, InstantSend
  locks and DKG messages) now weights each signature with a secret random
  factor before aggregating. Previously two invalid signatures whose errors
  cancelled out could pass as a valid batch. A peer could use this to disrupt
  InstantSend or ChainLock signing, get honest peers penalised, or get honest
  DKG members marked bad.
- Batched verification of DKG contributions is weighted the same way, so
  colluding members can no longer give another member invalid secret-key
  shares that pass verification, which could leave that member unable to sign
  for the quorum. This did not expose the quorum key.
- A DKG member copying another member's entire contribution could prevent the
  whole quorum from forming. Members whose verification vectors share a
  coefficient are now excluded from the commitment instead of the round
  failing. This excludes both the copying member and the member it copied,
  and both receive the usual PoSe penalty for failed DKG participation.
- Colluding DKG members could stop chosen honest members from building their
  secret-key share, which could prevent a quorum from forming without any
  member being blamed. The same condition made a node penalise an honest peer
  serving quorum data during share recovery. Equal secret-key shares from
  different members are now accepted; the resulting key share is still
  verified against the quorum's verification vector.

Like the governance fix, the DKG fixes protect a quorum once enough of its
members run this version. Weighted batch verification does more work per
message, so masternodes may see higher CPU use during DKG and signing.

# v23.1.12 Change log

Detailed upstream notes: [Dash Core v23.1.10](../dash/release-notes-23.1.10.md).

See the upstream [set of changes][set-of-changes].

# Credits

Thanks to everyone who directly contributed to this release:

- PastaPastaPasta
- UdjinM6

As well as everyone that submitted issues, reviewed pull requests and helped
debug the release candidates.

Thanks to the Dash Core and Bitcoin Core developers for the upstream work
backported into PirateCash Core.

# Older releases

- [PirateCash Core v23.1.9](release-notes-23.1.9.md)

[set-of-changes]: https://github.com/dashpay/dash/compare/v23.1.9...dashpay:v23.1.10
