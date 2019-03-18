                  
PirateCash [PIRATE] Source Code
================================

https://www.piratecash.net


Copyright © 2009-2019	Bitcoin Core Developers

Copyright © 2014-2019	The Dash Core developers

Copyright © 2014-2019	PivX Developers

Copyright © 2012-2019	The NovaCoin developers

Copyright © 2018-2019	PirateCash Developers


## Specifications


| Specification                 | Value               |
| ----------------------------- |:--------------------|
| Block Spacing                 | 120 seconds         |
| Maturity                      | 120 Confirmations   |
| Difficulty Retargeting        | Every Block         |
| Staking Minimum Age           | 8 hours             |
| Max Supply                    | ~105 Million PIRATE |
| Required Coins for masternode | 10 000 PIRATE       |


## PoS Rewards Breakdown

| Block                 | Reward              | Masternodes | Stakers   |
|---------------------- |:------------------- |:----------- |:--------- |
| 1                     | 50    PIRATE + fees | 60%         | 40%       |
| 1048576 (0x100000)    | 25    PIRATE + fees | 60%         | 40%       |
| 2097152 (0x200000)    | 12.5  PIRATE + fees | 60%         | 40%       |
| 3145728 (0x300000)    | 6.25  PIRATE + fees | 60%         | 40%       |
| 4194304 (0x400000)    | 3.125 PIRATE + fees | 60%         | 40%       |
| ...                   | ...                 | 60%         | 40%       |
| 2162688 (0x2100000)   | ~0    PIRATE + fees | 60%         | 40%       |



## Channels

| Specification | Value             |
| ------------- |:------------------|
| Bitcointalk   | [https://bitcointalk.org/index.php?topic=5050988](https://bitcointalk.org/index.php?topic=5050988)       |
| Website       | [https://piratecash.net](http://piratecash.net) |
| Explorer      | [https://block.piratecash.net:3001](http://block.piratecash.net:3001)|
| Telegram	| [https://t.me/joinchat/FtUaLREioKVyF6EPNVtnIQ](https://t.me/joinchat/FtUaLREioKVyF6EPNVtnIQ)|
| Discord       | [https://discord.gg/cbTdqxx](https://discord.gg/cbTdqxx)|
| ...           | ... |
| Test-NET	| [http://block.piratecash.net:3002/](http://block.piratecash.net:3002/)|

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
