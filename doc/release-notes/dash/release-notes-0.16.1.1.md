Cosanta Core version 0.16.1.1
==========================

Release is now available from:

  <https://cosa.is/downloads/>

This is a new hotfix release.

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

When upgrading from a version prior to 0.14.0.3, the
first startup of Cosanta Core will run a migration process which can take a few
minutes to finish. After the migration, a downgrade to an older version is only
possible with a reindex (or reindex-chainstate).

Downgrade warning
-----------------

### Downgrade to a version < 0.14.0.3

Downgrading to a version older than 0.14.0.3 is no longer supported due to
changes in the "evodb" database format. If you need to use an older version,
you must either reindex or re-sync the whole chain.

### Downgrade of masternodes to < 0.16

Starting with this release, masternodes will verify the protocol version of other
masternodes. This will result in PoSe punishment/banning for outdated masternodes,
so downgrading is not recommended.

Notable changes
===============

There was an unexpected behaviour of the "Encrypt wallet" menu item for unencrypted wallets
which was showing users the "Decrypt wallet" dialog instead. This was a GUI only issue,
internal encryption logic and RPC behaviour were not affected.

0.16.1.1 Change log
===================

See detailed [set of changes](https://github.com/cosanta/cosanta-core/compare/v0.16.1.0...cosanta:v0.16.1.1).

- [`ccef3b4836`](https://github.com/cosanta/cosanta-core/commit/ccef3b48363d8bff4b919d9119355182e3902ef3) qt: Fix wallet encryption dialog (#3816)

Python Support
--------------

Support for Python 2 has been discontinued for all test files and tools.

Credits
========

Thanks to everyone who directly contributed to this release:

- UdjinM6

As well as everyone that submitted issues and reviewed pull requests.

Older releases
==============

Cosanta was forked from Dash Core after the v0.15 release.
