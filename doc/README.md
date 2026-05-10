PirateCash Core
==========

This is the official reference wallet for PirateCash digital currency and comprises the backbone of the PirateCash peer-to-peer network. You can [download PirateCash Core](https://cosa.is/downloads/) or [build it yourself](#building) using the guides below.

Running
---------------------
The following are some helpful notes on how to run PirateCash Core on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/piratecash-qt` (GUI) or
- `bin/piratecashd` (headless)

### Windows

Unpack the files into a directory, and then run piratecash-qt.exe.

### macOS

Drag PirateCash Core to your applications folder, and then run PirateCash Core.

### Need Help?

* See the [PirateCash documentation](https://docs.cosa.is)
for help and more information.
* Ask for help on [PirateCash Discord](http://staydashy.com)
* Ask for help on the [PirateCash Forum](https://cosa.is/forum)

Building
---------------------
The following are developer notes on how to build PirateCash Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)
- [Android Build Notes](build-android.md)

Development
---------------------
The PirateCash Core repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- Source Code Documentation ***TODO***
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* See the [PirateCash Developer Documentation](https://dashcore.readme.io/)
  for technical specifications and implementation details.
* Discuss on the [PirateCash Forum](https://cosa.is/forum), in the Development & Technical Discussion board.
* Discuss on [PirateCash Discord](http://staydashy.com)
* Discuss on [PirateCash Developers Discord](http://chat.piratecashdevs.org/)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [piratecash.conf Configuration File](dash-conf.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [I2P Support](i2p.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [Managing Wallets](managing-wallets.md)
- [PSBT support](psbt.md)
- [Reduce Memory](reduce-memory.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
