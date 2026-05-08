Cosanta Core version v18.2.0
=========================

Release is now available from:

  <https://cosa.is/downloads/#wallets>

This is a new minor version release, bringing new features, various bugfixes
and other improvements.

This release is optional for all nodes.

Please report bugs using the issue tracker at github:

  <https://github.com/cosanta/cosanta-core/issues>


Upgrading and downgrading
=========================

How to Upgrade
--------------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Cosanta-Qt (on Mac) or
cosantad/cosanta-qt (on Linux). If you upgrade after DIP0003 activation and you were
using version < 0.13 you will have to reindex (start with -reindex-chainstate
or -reindex) to make sure your wallet has all the new data synced. Upgrading
from version 0.13 should not require any additional actions.

When upgrading from a version prior to 18.0.1, the
first startup of Cosanta Core will run a migration process which can take anywhere
from a few minutes to thirty minutes to finish. After the migration, a
downgrade to an older version is only possible with a reindex
(or reindex-chainstate).

Downgrade warning
-----------------

### Downgrade to a version < v18.2.0

Downgrading to a version older than v18.2.0 is supported.

### Downgrade to a version < v18.0.1

Downgrading to a version older than v18.0.1 is not supported due to changes in
the indexes database folder. If you need to use an older version, you must
either reindex or re-sync the whole chain.

### Downgrade of masternodes to < v18.0.1

Starting with the 0.16 release, masternodes verify the protocol version of other
masternodes. This results in PoSe punishment/banning for outdated masternodes,
so downgrading even prior to the activation of the introduced hard-fork changes
is not recommended.

Versioning
----------

Cosanta Core imperfectly follows semantic versioning. Breaking changes should be
expected in a major release. The number and severity of breaking changes in minor
releases are minimized, however we do not guarantee there are no breaking changes.
Bitcoin backports often introduce breaking changes, and are a likely source of
breaking changes in minor releases. Patch releases should never contain breaking changes.

This release **does** include breaking changes. Please read below to see if they will affect you.

Notable changes
===============


Remote Procedure Call (RPC) Changes
-----------------------------------

### The new RPCs are:

- `analyzepsbt` examines a PSBT and provides information about what
  the PSBT contains and the next steps that need to be taken in order
  to complete the transaction. For each input of a PSBT, `analyzepsbt`
  provides information about what information is missing for that
  input, including whether a UTXO needs to be provided, what pubkeys
  still need to be provided, which scripts need to be provided, and
  what signatures are still needed. Every input will also list which
  role is needed to complete that input, and `analyzepsbt` will also
  list the next role in general needed to complete the PSBT.
  `analyzepsbt` will also provide the estimated fee rate and estimated
  virtual size of the completed transaction if it has enough
  information to do so.
- `quorum listextended` is the cousin of `quorum list` with a more enriched reply. By using the `height` parameter, the RPC will list active quorums at a specified height (or at the tip if `height` is not specified).
  This RPC returns the following data per quorum grouped per llmqTypes:
    - For each `quorumHash`:
        - `creationHeight`: Block height where its DKG started
        - `quorumIndex`: Returned only for rotated llmqTypes
        - `minedBlockHash`: Hash of the block containing the mined final commitment
        - `numValidMembers`: The total of valid members.
        - `healthRatio`: The ratio of healthy members to quorum size. Range [0.0 - 1.0].
- `getbalances` returns an object with all balances (`mine`,
  `untrusted_pending` and `immature`). Please refer to the RPC help of
  `getbalances` for details. The new RPC is intended to replace
  `getunconfirmedbalance` and the balance fields in `getwalletinfo`, as well as
  `getbalance`. The old calls may be removed in a future version.

### The removed RPCs are:
None

### Changes in existing RPCs introduced through bitcoin backports:
- `walletprocesspsbt` and `walletcreatefundedpsbt` now include BIP 32 derivation paths by default for public keys if we know them. This can be disabled by setting `bip32derivs` to `false`.

### Cosanta-specific changes in existing RPCs:
None

Please check `help <command>` for more detailed information on specific RPCs.

Command-line options
--------------------


Please check `Help -> Command-line options` in Qt wallet or `cosantad --help` for
more information.

Backports from Bitcoin Core
---------------------------

This release introduces many hundreds updates from Bitcoin v0.18/v0.19/v0.20/v0.21/v22. Bitcoin changes that do not align with Cosanta’s product needs, such as SegWit and RBF, are excluded from our backporting. For additional detail on what’s included in Bitcoin, please refer to their release notes.

v18.2.0 Change log
==================

See detailed [set of changes](https://github.com/cosanta/cosanta-core/compare/v18.1.0...cosanta:v18.2.0).

Credits
=======

Thanks to everyone who directly contributed to this release:

- Kittywhiskers Van Gogh
- Konstantin Akimov
- Odysseas Gabrielides
- PastaPastaPasta
- strophy
- thephez
- UdjinM6

As well as everyone that submitted issues, reviewed pull requests, helped debug the release candidates, and write DIPs that were implemented in this release.

Older releases
==============

Cosanta was forked from Dash Core after the v0.15 release.
