Cosanta Core version v18.2.2
=========================

Release is now available from:

  <https://cosa.is/downloads/#wallets>

This is a new hotfix version release.

This release is optional for all nodes; however, v18.2.2 or higher is required
to be able to use testnet right until v19 hard fork activation. Earlier
versions will not be able to sync past block 847000 on testnet.

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

### Downgrade to a version < v18.2.2

Downgrading to a version older than v18.2.2 is supported.

### Downgrade to a version < v18.0.1

Downgrading to a version older than v18.0.1 is not supported due to changes in
the indexes database folder. If you need to use an older version, you must
either reindex or re-sync the whole chain.

Notable changes
===============

Testnet Breaking Changes
------------------------

A new testnet only LLMQ has been added. This LLMQ is of the type LLMQ_25_67; this LLMQ is only active on testnet.
This LLMQ will not remove the LLMQ_100_67 from testnet; however that quorum (likely) will not form and will perform no role.
See the [diff](https://github.com/cosanta/cosanta-core/pull/5225/files#diff-e70a38a3e8c2a63ca0494627301a5c7042141ad301193f78338d97cb1b300ff9R451-R469) for specific parameters of the LLMQ.

This LLMQ will become active at the height of 847000. **This will be a breaking change and a hard fork for testnet**
This LLMQ is not activated with the v19 hardfork; as such testnet will experience two hardforks. One at height 847000,
and the other to be determined by the BIP9 hard fork process.

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

Other changes
-------------
#5247 is backported to improve debugging experience.

v18.2.2 Change log
==================

See detailed [set of changes](https://github.com/cosanta/cosanta-core/compare/v18.2.1...cosanta:v18.2.2).

Credits
=======

Thanks to everyone who directly contributed to this release:

- Odysseas Gabrielides
- UdjinM6

As well as everyone that submitted issues, reviewed pull requests, helped debug the release candidates, and write DIPs that were implemented in this release.

Older releases
==============

Cosanta was forked from Dash Core after the v0.15 release.
