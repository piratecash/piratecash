# PirateCash Core version v23.1.9

Official release:

  <https://github.com/piratecash/piratecash/releases/tag/v23.1.9-pirate>

This release backports Dash Core v23.1.9. The changes below describe that
upstream release.

This is a new patch version release. It fixes a consensus-affecting
transaction-encoding bug, several remotely reachable crashes and
denial-of-service issues in the masternode, DKG, signing and governance paths,
and backports a number of hardening and robustness fixes from later releases.

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

## Consensus

- Transactions whose version field is negative are now rejected once DIP3 is
  active. The version and type share one 32-bit field; a negative version was
  sign-extended when a transaction was re-serialized, so a block containing such
  a transaction could be accepted by a node that received the original bytes and
  rejected by every node that received the re-serialized form, leading to a
  persistent chain split. The new rule rejects these transactions uniformly. No
  valid or historical transaction is affected and transaction ids are unchanged.

## Crash fixes

These issues do not affect consensus rules or put funds at risk, but each one
can take a node offline.

- Fixed an abort during the DKG when a quorum member sent a contribution whose
  decrypted secret-key share was invalid: the share reached an aggregation
  assertion and terminated the process on every honest member of the quorum.
  Invalid shares are now rejected before aggregation.
- Fixed block-template failures caused by provider transactions: a
  `ProUpServTx` with an invalid type, a `ProRegTx` reusing collateral already
  referenced by an in-mempool update, and a pending owner key colliding with a
  Platform node id are now rejected at mempool acceptance.
- Bounded the masternode-list caches by size so a peer cannot drive unbounded
  memory growth between blocks.
- Bounded `DomainPort` deserialization so a small provider transaction can no
  longer make the node allocate and zero very large buffers while holding
  `cs_main`.
- Decoding an unknown-version provider transaction through the verbose decode
  RPCs no longer dereferences a missing network-info object.
- `masternode payments` no longer aborts at the genesis block, and the
  masternode-list RPCs no longer cache a list for a block outside the active
  chain.
- Evonodes no longer abort at credit-pool snapshot boundaries when the pool is
  first constructed outside a block connection (for example by
  `getassetunlockstatuses`).
- Flat-file (block and undo) positions are now stored and seeked as 64-bit
  values. Block-undo data can be several times the size of its block, so a busy
  chain could push a data file past the point a 32-bit offset can address; the
  write and seek paths no longer narrow the position to 32 bits, which avoided a
  silent wrap (and, on Windows, a failure past 2 GiB).

## InstantSend

- Provider (masternode) transactions are no longer given InstantSend locks.
  Locking them served no purpose and interacted badly with lock eviction on
  mempool removal. Software that waited for an InstantSend lock on a provider
  transaction should wait for a ChainLock or for confirmations instead.

## Security and denial-of-service hardening

These issues do not affect consensus and do not put funds at risk.

- DKG contribution verification: a single verification-vector coefficient
  shared between two members no longer fails the whole verification batch.
  Previously that marked unrelated honest members' contributions invalid and
  caused them to be complained about; contributions are now verified
  individually, so only the offending member is affected.
- Signature-share batch verification is now scoped per sender, so one peer can
  no longer get an honest peer penalised or have a forged share stored in place
  of an honest one. Malformed detached quorum share batches are rejected, and
  signing intake is gated against the active-quorum set before any quorum is
  retrieved.
- Governance: an orphan vote now requires a valid masternode signature before
  it is cached, the number of orphan parent objects re-requested per tick is
  bounded, governance vote signature checks are memoised to bound the cost of a
  sync request, a vote is no longer recorded before it is validated, and a
  failed BLS vote-signature check is logged under the governance category
  instead of unconditionally, so a peer cannot fill `debug.log` with it.
- Socket exception monitoring is now consistent between the `poll()` and
  `select()` backends, and a peer whose send buffer stalls is now subject to the
  send timeout, so it can no longer hold a connection slot indefinitely.
- Verified inbound connections are bounded per operator identity; `CHECKDATASIG`
  uses the signature cache; repeated authenticated Platform bans are skipped;
  `DYNBITSET` allocation is bounded against the remaining stream size; a bogus
  InstantSend lock no longer drops a batch of unrelated locks; and
  `-allowprivatenet` no longer influences masternode address validation.

## Wallet

- Invalid-mnemonic errors no longer echo the recovery phrase, an encrypted
  wallet is relocked if an HD upgrade fails after it was unlocked, and a wallet
  unlock deadline can no longer be extended without re-authenticating.

# v23.1.9 Change log

Detailed upstream notes: [Dash Core v23.1.9](../dash/release-notes-23.1.9.md).

See the upstream [set of changes][set-of-changes].

# Credits

Thanks to everyone who directly contributed to this release:

- Konstantin Akimov
- PastaClaw
- PastaPastaPasta
- UdjinM6

As well as everyone that submitted issues, reviewed pull requests and helped
debug the release candidates.

Thanks to the Dash Core and Bitcoin Core developers for the upstream work
backported into PirateCash Core.

# Older releases

- [PirateCash Core v23.1.8](release-notes-23.1.8.md)

[set-of-changes]: https://github.com/dashpay/dash/compare/v23.1.8...dashpay:v23.1.9
