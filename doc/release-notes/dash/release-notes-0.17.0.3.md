PirateCash Core version 0.17.0.3
==========================

Release is now available from:

  <https://piratecash.net/en/download/>

This is a new hotfix release.

Please report bugs using the issue tracker at github:

  <https://github.com/piratecash/piratecash-core/issues>


Upgrading and downgrading
=========================

How to Upgrade
--------------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/PirateCash-Qt (on Mac) or
piratecashd/piratecash-qt (on Linux). If you upgrade after DIP0003 activation and you were
using version < 0.13 you will have to reindex (start with -reindex-chainstate
or -reindex) to make sure your wallet has all the new data synced. Upgrading
from version 0.13 should not require any additional actions.

When upgrading from a version prior to 0.14.0.3, the
first startup of PirateCash Core will run a migration process which can take a few
minutes to finish. After the migration, a downgrade to an older version is only
possible with a reindex (or reindex-chainstate).

Downgrade warning
-----------------

### Downgrade to a version < 0.14.0.3

Downgrading to a version older than 0.14.0.3 is no longer supported due to
changes in the "evodb" database format. If you need to use an older version,
you must either reindex or re-sync the whole chain.

### Downgrade of masternodes to < 0.17.0.2

Starting with the 0.16 release, masternodes verify the protocol version of other
masternodes. This results in PoSe punishment/banning for outdated masternodes,
so downgrading even prior to the activation of the introduced hard-fork changes
is not recommended.

Notable changes
===============

This release adds some missing translations and help strings. It also fixes
a couple of build issues and a rare crash on some linux systems.

0.17.0.3 Change log
===================

See detailed [set of changes]().

Credits
=======

Thanks to everyone who directly contributed to this release:

- dustinface (xdustinface)
- strophy
- UdjinM6

As well as everyone that submitted issues and reviewed pull requests.

Older releases
==============

PirateCash was forked from Dash Core.
