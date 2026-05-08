Cosanta Core version 18.0.2
========================

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

This release resolves some excessive memory usage via the "evo" database (evodb).

18.0.2 Change log
===================

See detailed [set of changes](https://github.com/cosanta/cosanta-core/compare/v18.0.1...cosanta:v18.0.2`).

- [`666ff7bff9`](https://github.com/cosanta/cosanta-core/commit/666ff7bff9) merge bitcoin#14193: Add missing mempool locks
- [`96f4022a6a`](https://github.com/cosanta/cosanta-core/commit/96f4022a6a) chore: archive release-nodes.md
- [`0b60096d8a`](https://github.com/cosanta/cosanta-core/commit/0b60096d8a) chore: bump version to 18.0.2
- [`e8afde2740`](https://github.com/cosanta/cosanta-core/commit/e8afde2740) fix: Flush chainstate (and evodb) cache whenever evodb mem usage is getting too high (#5007)
- [`8efd7f04c6`](https://github.com/cosanta/cosanta-core/commit/8efd7f04c6) Merge bitcoin/bitcoin#25739: Update leveldb subtree (#5005)
- [`c92cbce6a5`](https://github.com/cosanta/cosanta-core/commit/c92cbce6a5) trivial: Fix trailing whitespaces in release notes (#4989)

Credits
========

Thanks to everyone who directly contributed to this release:

- kittywhiskers
- PastaPastaPasta
- UdjinM6

As well as everyone that submitted issues and reviewed pull requests.

Older releases
==============

Cosanta was forked from Dash Core after the v0.15 release.
