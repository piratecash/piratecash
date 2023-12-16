PirateCash [PIRATE] Source Code
================================

# Mandatory Update

http://p.cash/hardfork

We have released a major update PirateCash Core v18.

To use it, you need to perform the following steps.

## Update the client version to 11.7.0 (protocol version 60029)

At block 1265001 (approximately January 8, 2024), the block reward will be reduced to 150 satoshis. This is done so that miners with outdated clients cannot generate new blocks. We have also sent an alert to all users with a protocol version below 60029 about the need to update the client to avoid FOMO among those who have not updated.

For services using PirateCash (exchanges, exchange services, staking pools, and others), we recommend temporarily suspending deposits and withdrawals from January 7 to 8, 2024 (before block 1265001).

At block 1265799 (approximately January 9, 2024), the old client will create the last block and then stop generating and accepting new blocks. Additionally, information will be recorded in debug.log about the need to update the client to the latest version.

## Download the new 0.18.0.0 version of the client

The new version of PirateCash 0.18.0.0 will available approx January 2-5 2024 and right now 0.18.0.0-rc2 is available.

Until block 1265800, you can run both clients simultaneously because the directories with working files are different: .piratecash for the old and .piratecore for the new client. You can also copy the wallet.dat file to the new client directory, export and import private keys, or transfer funds from the old wallet to the new one (you need to generate an address on the new client). Note: the new client before block 1265800 downloads blocks and displays incoming transactions, but sending will only be possible after the hard fork is activated.

At block 1265800 (approximately January 9, 2024), the new client will start creating new blocks.

After block 1268999 (approximately January 13, 2024), block rewards will be restored. It is recommended for services that accept and send Pirate to restore wallet functionality!.

Please note that the RPC command signrawtransaction has been replaced with signrawtransactionwithwallet (our code is fully compatible with RPC commands from DashCore 0.18.2).

## An alternative option is available for users.

Transfer your Pirate to our service [@piratecash_bot](http://t.me/piratecash_bot), where you can mine them in the bot or exchange them for tokens.

It is important to note that temporarily you will not be able to deposit or withdraw PirateCash from the @piratecash_bot service during maintenance in the main blockchain. However, you can freely use tokens without any restrictions.

Starting from block 1265001, payouts for staking and referral rewards will be temporarily paused and will resume after block 1268999 (January 13, 2024).


## About Souces

https://www.piratecash.net


Copyright © 2009-2023	The Bitcoin Core developers

Copyright © 2014-2023	The Dash Core developers

Copyright © 2014-2023	The PivX developers

Copyright © 2012-2023	The NovaCoin developers

Copyright © 2018-2023	The PirateCash developers


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

| Block                 | Reward            | Masternodes | Stakers    |
|---------------------- |:----------------- |:----------- |:---------- |
| 0                     | 50         PIRATE | 60%         | 40%        |
| 917000  (0x0DFE08)    | 50         PIRATE | 0.1%        | 99.9%      |
| 1048577 (0x100001)    | 25         PIRATE | 0.1%        | 99.9%      |
| 2097153 (0x200001)    | 12.5       PIRATE | 0.1%        | 99.9%      |
| 3145729 (0x300001)    | 6.25       PIRATE | 0.1%        | 99.9%      |
| 4194305 (0x400001)    | 3.125      PIRATE | 0.1%        | 99.9%      |
| 5242881 (0x500001)    | 1.625      PIRATE | 0.1%        | 99.9%      |
| 6291457 (0x600001)    | 0.78125    PIRATE | 0.1%        | 99.9%      |
| 7340033 (0x700001)    | 0.390625   PIRATE | 0.1%        | 99.9%      |
| 8388609 (0x800001)    | 0.1953125  PIRATE | 0.1%        | 99.9%      |
| 9437185 (0x900001)    | 0.09765625 PIRATE | 0.1%        | 99.9%      |
| 10485761 (0xA00001)   | 0.04882812 PIRATE | 0.1%        | 99.9%      |
| 11534337 (0xB00001)   | 0.02441406 PIRATE | 0.1%        | 99.9%      |
| 12582913 (0xC00001)   | 0.01220703 PIRATE | 0.1%        | 99.9%      |
| 13631489 (0xD00001)   | 0.00610351 PIRATE | 0.1%        | 99.9%      |
| 14680065 (0xE00001)   | 0.00305175 PIRATE | 0.1%        | 99.9%      |
| 15728641 (0xF00001)   | 0.00152587 PIRATE | 0.1%        | 99.9%      |
| 16777217 (0x1000001)  | 0.00076293 PIRATE | 0.1%        | 99.9%      |
| 17825793 (0x1100001)  | 0.00038146 PIRATE | 0.1%        | 99.9%      |
| 18874369 (0x1200001)  | 0.00019073 PIRATE | 0.1%        | 99.9%      |
| 19922945 (0x1300001)  | 0.00009536 PIRATE | 0.1%        | 99.9%      |
| 20971521 (0x1400001)  | 0.00004768 PIRATE | 0.1%        | 99.9%      |
| 22020097 (0x1500001)  | 0.00002384 PIRATE | 0.1%        | 99.9%      |
| 23068673 (0x1600001)  | 0.00001192 PIRATE | 0.1%        | 99.9%      |
| 24117249 (0x1700001)  | 0.00000596 PIRATE | 0.1%        | 99.9%      |
| 25165825 (0x1800001)  | 0.00000298 PIRATE | 0.1%        | 99.9%      |
| 26214401 (0x1900001)  | 0.00000149 PIRATE | 0.1%        | 99.9%      |
| 27262977 (0x1A00001)  | 0.00000074 PIRATE | 0.1%        | 99.9%      |
| 28311553 (0x1B00001)  | 0.00000037 PIRATE | 0.1%        | 99.9%      |
| 29360129 (0x1C00001)  | 0.00000018 PIRATE | 0.1%        | 99.9%      |
| 30408705 (0x1D00001)  | 0.00000009 PIRATE | 0.1%        | 99.9%      |
| 31457281 (0x1E00001)  | 0.00000004 PIRATE | 0.1%        | 99.9%      |
| 32505857 (0x1F00001)  | 0.00000002 PIRATE | 0.1%        | 99.9%      |
| 33554433 (0x2000001)  | 0.00000001 PIRATE | 0.1%        | 99.9%      |
| 34603009 (0x2100001)  | 0 + fees   PIRATE | 0.1%        | 99.9%      |



## Channels

| Specification | Value             |
| ------------- |:------------------|
| Bitcointalk   | [https://bitcointalk.org/index.php?topic=5050988](https://bitcointalk.org/index.php?topic=5050988)       |
| Website       | [https://piratecash.net](http://piratecash.net) |
|:TOR:          | [nzcaaryanbre6mdwbxkdonf24b4z6isk2q4qxahno42f4gso3ddjwpad.onion](nzcaaryanbre6mdwbxkdonf24b4z6isk2q4qxahno42f4gso3ddjwpad.onion) |
|^^             | [pirateogjtp5xbzfdmep2iavvc7qwku2reg754eamr7bxnospyqkqoad.onion](pirateogjtp5xbzfdmep2iavvc7qwku2reg754eamr7bxnospyqkqoad.onion) |
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
