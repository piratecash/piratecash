                  
PirateCash [PIRATE] Source Code
================================

https://www.piratecash.net


Copyright (c) 2009-2015 Bitcoin Core Developers

Copyright (c) 2014-2016 PivX Developers

Copyright (c) 2018	PirateCash Developers


## The code will be upload at 3 November 2018


## Specifications


| Specification                 | Value             |
| ----------------------------- |:------------------|
| Block Spacing                 | 60 seconds        |
| Maturity                      | 120 Confirmations |
| Difficulty Retargeting        | Every Block       |
| Staking Minimum Age           | 8 hours           |
| Reward                        | 10 PIRATE         |
| Max Supply                    | 99 Million PIRATE |
| Required Coins for masternode | 1 000 PIRATE      |


## Channels

| Specification | Value             |
| ------------- |:------------------|
| Bitcointalk   | [https://bitcointalk.org/index.php?topic=5050988](https://bitcointalk.org/index.php?topic=5050988)       |
| Website       | [https://piratecash.net](http://piratecash.net) |
| Explorer      | [https://block.piratecash.net:3001](http://block.piratecash.net:3001)


License
-------

PirateCash is released under the terms of the MIT license. See `COPYING` for more
information or see http://opensource.org/licenses/MIT.

Development process
-------------------

Developers work in their own trees, then submit pull requests when they think
their feature or bug fix is ready.

If it is a simple/trivial/non-controversial change, then one of the PirateCash
development team members simply pulls it.

If it is a *more complicated or potentially controversial* change, then the patch
submitter will be asked to start a discussion with the devs and community.

The patch will be accepted if there is broad consensus that it is a good thing.
Developers should expect to rework and resubmit patches if the code doesn't
match the project's coding conventions (see `doc/coding.txt`) or are
controversial.

The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/piratecash/piratecash/tags) are created
regularly to indicate new official, stable release versions of PirateCash.


**Compiling for debugging**

Run configure with the --enable-debug option, then make. Or run configure with
CXXFLAGS="-g -ggdb -O0" or whatever debug flags you need.

**Debug.log**

If the code is behaving strangely, take a look in the debug.log file in the data directory;
error and debugging messages are written there.

The -debug=... command-line option controls debugging; running with just -debug will turn
on all categories (and give you a very large debug.log file).

The Qt code routes qDebug() output to debug.log under category "qt": run with -debug=qt
to see it.
