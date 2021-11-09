                  
PirateCash [PIRATE] Source Code
================================

https://www.piratecash.net


Copyright © 2009-2021	Bitcoin Core Developers

Copyright © 2014-2021	The Dash Core developers

Copyright © 2014-2021	PivX Developers

Copyright © 2012-2021	The NovaCoin developers

Copyright © 2018-2021	PirateCash Developers


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

| Block                 | Reward            | Masternodes | Stakers   |
|---------------------- |:----------------- |:----------- |:--------- |
| 0                     | 50         PIRATE | 60%         | 40%       |
| 1048577 (0x100001)    | 25         PIRATE | 60%         | 40%       |
| 2097153 (0x200001)    | 12.5       PIRATE | 60%         | 40%       |
| 3145729 (0x300001)    | 6.25       PIRATE | 60%         | 40%       |
| 4194305 (0x400001)    | 3.125      PIRATE | 60%         | 40%       |
| 5242881 (0x500001)    | 1.625      PIRATE | 60%         | 40%       |
| 6291457 (0x600001)    | 0.78125    PIRATE | 60%         | 40%       |
| 7340033 (0x700001)    | 0.390625   PIRATE | 60%         | 40%       |
| 8388609 (0x800001)    | 0.1953125  PIRATE | 60%         | 40%       |
| 9437185 (0x900001)    | 0.09765625 PIRATE | 60%         | 40%       |
| 10485761 (0xA00001)   | 0.04882812 PIRATE | 60%         | 40%       |
| 11534337 (0xB00001)   | 0.02441406 PIRATE | 60%         | 40%       |
| 12582913 (0xC00001)   | 0.01220703 PIRATE | 60%         | 40%       |
| 13631489 (0xD00001)   | 0.00610351 PIRATE | 60%         | 40%       |
| 14680065 (0xE00001)   | 0.00305175 PIRATE | 60%         | 40%       |
| 15728641 (0xF00001)   | 0.00152587 PIRATE | 60%         | 40%       |
| 16777217 (0x1000001)  | 0.00076293 PIRATE | 60%         | 40%       |
| 17825793 (0x1100001)  | 0.00038146 PIRATE | 60%         | 40%       |
| 18874369 (0x1200001)  | 0.00019073 PIRATE | 60%         | 40%       |
| 19922945 (0x1300001)  | 0.00009536 PIRATE | 60%         | 40%       |
| 20971521 (0x1400001)  | 0.00004768 PIRATE | 60%         | 40%       |
| 22020097 (0x1500001)  | 0.00002384 PIRATE | 60%         | 40%       |
| 23068673 (0x1600001)  | 0.00001192 PIRATE | 60%         | 40%       |
| 24117249 (0x1700001)  | 0.00000596 PIRATE | 60%         | 40%       |
| 25165825 (0x1800001)  | 0.00000298 PIRATE | 60%         | 40%       |
| 26214401 (0x1900001)  | 0.00000149 PIRATE | 60%         | 40%       |
| 27262977 (0x1A00001)  | 0.00000074 PIRATE | 60%         | 40%       |
| 28311553 (0x1B00001)  | 0.00000037 PIRATE | 60%         | 40%       |
| 29360129 (0x1C00001)  | 0.00000018 PIRATE | 60%         | 40%       |
| 30408705 (0x1D00001)  | 0.00000009 PIRATE | 60%         | 40%       |
| 31457281 (0x1E00001)  | 0.00000004 PIRATE | 60%         | 40%       |
| 32505857 (0x1F00001)  | 0.00000002 PIRATE | 60%         | 40%       |
| 33554433 (0x2000001)  | 0.00000001 PIRATE | 60%         | 40%       |
| 34603009 (0x2100001)  | 0 + fees   PIRATE | 60%         | 40%       |



## Channels

| Specification | Value             |
| ------------- |:------------------|
| Bitcointalk   | [https://bitcointalk.org/index.php?topic=5050988](https://bitcointalk.org/index.php?topic=5050988)       |
| Website       | [https://piratecash.net](http://piratecash.net) |
| TOR           | [nzcaaryanbre6mdwbxkdonf24b4z6isk2q4qxahno42f4gso3ddjwpad.onion](nzcaaryanbre6mdwbxkdonf24b4z6isk2q4qxahno42f4gso3ddjwpad.onion) |
| Explorer      | [https://piratecash.info](https://piratecash.info)|
| Telegram	| [https://t.me/joinchat/FtUaLREioKVyF6EPNVtnIQ](https://t.me/joinchat/FtUaLREioKVyF6EPNVtnIQ)|
| Discord       | [https://discord.gg/cbTdqxx](https://discord.gg/cbTdqxx)|
| ...           | ... |
| Test-NET	| [https://testnet.piratecash.info/](https://testnet.piratecash.info)|

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
