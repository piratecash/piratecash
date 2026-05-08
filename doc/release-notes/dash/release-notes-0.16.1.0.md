Cosanta Core version 0.16.1.0
==========================

Release is now available from:

  <https://cosa.is/downloads/>

This is a new minor version release, bringing various bugfixes and improvements.

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

Network changes
---------------
InstantSend locks were not relayed correctly when another node was requesting updates via the `mempool`
p2p message. Some other internal optimizations were made to the way object requests are handled.

GUI changes
-----------
Fixes for the following GUI related issues
- The wallet crashed if no valid GUI theme was found in settings.
This happened for users upgrading from versions prior to v0.14.
- There were randomly occurring font size scaling issues.
- Opening or closing the settings while the application was in full-screen/maximized
window mode lead to fragmented GUI layouts.
- There was an unexpected checkmark in the "Encrypt wallet" menu item on Linux and Windows
- Starting Cosanta Core compiled without wallet support or with `-disablewallet` command line
parameter lead to an application crash.

RPC changes
-----------
- `getwalletinfo` shows wallet rescan duration and progress now

0.16.1.0 Change log
===================

See detailed [set of changes](https://github.com/cosanta/cosanta-core/compare/v0.16.0.1...cosanta:v0.16.1.0).

- [`b1c930bc86`](https://github.com/cosanta/cosanta-core/commit/b1c930bc86) [v0.16.x] bump version in configure.ac (#3788)
- [`3d94e714a9`](https://github.com/cosanta/cosanta-core/commit/3d94e714a9) contrib|src: Update hard coded seeds (#3791)
- [`c02a994489`](https://github.com/cosanta/cosanta-core/commit/c02a994489) bump nMinimumChainWork, defaultAssumeValid, checkpointData for mainnet and testnet (#3789)
- [`1d41fbd760`](https://github.com/cosanta/cosanta-core/commit/1d41fbd760) Update man pages (#3798)
- [`b3bbc00dbc`](https://github.com/cosanta/cosanta-core/commit/b3bbc00dbc) Merge #15730: rpc: Show scanning details in getwalletinfo (#3785)
- [`3ad4651db1`](https://github.com/cosanta/cosanta-core/commit/3ad4651db1) Call EraseObjectRequest as soon as an object is read from the stream (#3783)
- [`d8f8f174c0`](https://github.com/cosanta/cosanta-core/commit/d8f8f174c0) Avoid accessing pendingContributionVerifications from VerifyPendingContributions while ReceiveMessage is still doing its job (#3782)
- [`0814e6145d`](https://github.com/cosanta/cosanta-core/commit/0814e6145d) qt: Handle fonts of deleted widgets properly, streamline the flow in `GUIUtil::updateFonts` (#3772)
- [`e149740120`](https://github.com/cosanta/cosanta-core/commit/e149740120) qt: avoid auto-updating window width when it's in full screen or when it's maximized (#3771)
- [`78358a2a6d`](https://github.com/cosanta/cosanta-core/commit/78358a2a6d) qt: Do not show a check-mark for "Encrypt wallet" menu item (#3770)
- [`7b72e98092`](https://github.com/cosanta/cosanta-core/commit/7b72e98092) Fix IS-locks sync via `mempool` p2p command (#3766)
- [`9ceee5df20`](https://github.com/cosanta/cosanta-core/commit/9ceee5df20) qt: Fix --disable-wallet build and --disablewallet mode (#3762)
- [`62985c771b`](https://github.com/cosanta/cosanta-core/commit/62985c771b) depends: Update Qt download url. (#3756)
- [`717a41a572`](https://github.com/cosanta/cosanta-core/commit/717a41a572) qt: Make sure there is a valid theme set in the options (#3755)
- [`01a3435158`](https://github.com/cosanta/cosanta-core/commit/01a3435158) update public part of windows code signing certificate (#3749)

Credits
========

Thanks to everyone who directly contributed to this release:

- dustinface (xdustinface)
- Oleg Girko (OlegGirko)
- PastaPastaPasta
- UdjinM6

As well as everyone that submitted issues and reviewed pull requests.

Older releases
==============

Cosanta was forked from Dash Core after the v0.15 release.
