Cosanta Core version 18.1.1
========================

Release is now available from:

  <https://cosa.is/downloads/#wallets>

This is a new hotfix release. All nodes should upgrade.

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

### Downgrade to a version < v18.0.1

Downgrading to a version older than v18.0.1 is not supported due to changes in
the indexes database folder. If you need to use an older version, you must
either reindex or re-sync the whole chain.

### Downgrade of masternodes to < v18.0.1

Starting with the 0.16 release, masternodes verify the protocol version of other
masternodes. This results in PoSe punishment/banning for outdated masternodes,
so downgrading even prior to the activation of the introduced hard-fork changes
is not recommended.

Notable changes
===============

This release fixes an issue where outdated qfcommit messages replayed to the network were validated and propagated to
other nodes rather than dropped. This patch updates the behavior so that old qfcommits will no longer be relayed.

18.1.1 Change log
===================

See detailed [set of changes](https://github.com/cosanta/cosanta-core/compare/v18.1.0...cosanta:v18.1.1`).

- [`82d7b6e94a154c4b16bd634ef1a5eb168e22030c`](https://github.com/cosanta/cosanta-core/commit/82d7b6e94a154c4b16bd634ef1a5eb168e22030c) fix: avoid re-propogating old qfcommit messages (#5145)
- [`6c5a310c14e5d97794fbaa84e5ca993c0961ae09`](https://github.com/cosanta/cosanta-core/commit/6c5a310c14e5d97794fbaa84e5ca993c0961ae09) chore: bump version
Credits
=======

Thanks to everyone who directly contributed to this release:

- PastaPastaPasta
- UdjinM6

As well as everyone that submitted issues and reviewed pull requests.

Older releases
==============

Cosanta was forked from Dash Core after the v0.15 release.
