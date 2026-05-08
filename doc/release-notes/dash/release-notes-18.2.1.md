Cosanta Core version v18.2.1
=========================

Release is now available from:

  <https://cosa.is/downloads/#wallets>

This is a new hotfix version release, bringing various bugfixes.

Please note that v18.2.0 was revoked due to a bug; this version fixes that bug.

This release is optional for all nodes; however, v18.2.1 is required to be
able to use both mainnet and testnet. Currently, v18.2.0 is not working on mainnet,
and v18.1.1 is not working on testnet; v18.2.1 will work on both networks.

Please report bugs using the issue tracker at GitHub:

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

### Downgrade to a version < v18.2.1

Downgrading to a version older than v18.2.1 is supported.

### Downgrade to a version < v18.0.1

Downgrading to a version older than v18.0.1 is not supported due to changes in
the indexes database folder. If you need to use an older version, you must
either reindex or re-sync the whole chain.

### Downgrade of masternodes to < 18.2.1

It is highly recommended not to downgrade masternodes below 18.2.1, as 18.2.1 (and 18.1.1)
fix important bugs which may result in your masternode being PoSe banned.

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

Notable changes
===============
See #5145 and #5142; these 2 PR fix important bugs in previous versions. Specifically,
#5145 fixes an issue where qfcommit messages can be replayed from the past, then are
validated and propagated to other nodes. This patch prevents old qfcommits
from being relayed. #5142 is a fix which enables this version to function both on testnet
and mainnet.


Remote Procedure Call (RPC) Changes
-----------------------------------

### The new RPCs are:
None

### The removed RPCs are:
None

### Changes in existing RPCs introduced through bitcoin backports:
None

### Cosanta-specific changes in existing RPCs:
None

Please check `help <command>` for more detailed information on specific RPCs.

Command-line options
--------------------
None

Please check `Help -> Command-line options` in Qt wallet or `cosantad --help` for
more information.

Backports from Bitcoin Core
---------------------------
None

v18.2.1 Change log
==================

See detailed [set of changes](https://github.com/cosanta/cosanta-core/compare/v18.2.0...cosanta:v18.2.1).

Credits
=======

Thanks to everyone who directly contributed to this release:

- Kittywhiskers Van Gogh
- Konstantin Akimov
- Odysseas Gabrielides
- PastaPastaPasta
- UdjinM6

As well as everyone that submitted issues, reviewed pull requests, helped debug the release candidates, and write DIPs that were implemented in this release.

Older releases
==============

Cosanta was forked from Dash Core after the v0.15 release.
