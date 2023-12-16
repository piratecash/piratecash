<<<<<<< HEAD
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
=======
PirateCash Core staging tree 18.0
===========================

https://p.cash/ https://pirate.cash/ https://piratecash.net/
>>>>>>> v18


What is PirateCash?
-------------

PirateCash is an experimental digital ecosystem for provide services B2B, private
payments to anyone, anywhere in the world. PirateCash uses peer-to-peer technology
to operate with no central authority: managing transactions and issuing money
are carried out collectively by the network. PirateCash Core is the name of the open
source software which enables the use of this currency.

For more information, as well as an immediately useable, binary version of
the PirateCash Core software, see https://p.cash/.

## Specifications


| Specification                 | Value               |
| ----------------------------- |:--------------------|
| Block Spacing                 | 120 seconds         |
| Maturity                      | 120 Confirmations   |
| Difficulty Retargeting        | Every Block         |
| Staking Minimum Age           | 8 hours             |
| Max Supply                    | ~105 Million PIRATE |
| Required Coins for masternode | 10 000 PIRATE       |


## Pow / PoS Rewards Breakdown

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

For more information, as well as an immediately usable, binary version of
the PirateCash Core software, see https://p.cash/en/download/.

License
-------

PirateCash Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is meant to be stable. Development is normally done in separate branches.
[Tags](https://github.com/piratecash/piratecash/tags) are created to indicate new official,
stable release versions of PirateCash Core.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with: `make check`. Further details on running
and extending unit tests can be found in [/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python, that are run automatically on the build server.
These tests can be run (if the [test dependencies](/test) are installed) with: `test/functional/test_runner.py`

The Travis CI system makes sure that every pull request is built for Windows, Linux, and macOS, and that unit/sanity tests are run automatically.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.

Translations
------------

Changes to translations as well as new translations can be submitted to
[PirateCash Core's Transifex page](https://www.transifex.com/projects/p/piratecash/).

Translations are periodically pulled from Transifex and merged into the git repository. See the
[translation process](doc/translation_process.md) for details on how this works.

**Important**: We do not accept translation changes as GitHub pull requests because the next
pull from Transifex would automatically overwrite them again.

