Cosanta Core
==========

This is the official reference wallet for Cosanta digital currency and comprises the backbone of the Cosanta peer-to-peer network. You can [download Cosanta Core](https://cosa.is/downloads/) or [build it yourself](#building) using the guides below.

Running
---------------------
The following are some helpful notes on how to run Cosanta Core on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/cosanta-qt` (GUI) or
- `bin/cosantad` (headless)

### Windows

Unpack the files into a directory, and then run cosanta-qt.exe.

### macOS

Drag Cosanta Core to your applications folder, and then run Cosanta Core.

### Need Help?

* See the [Cosanta documentation](https://docs.cosa.is)
for help and more information.
* Ask for help on [Cosanta Discord](http://staydashy.com)
* Ask for help on the [Cosanta Forum](https://cosa.is/forum)

Building
---------------------
The following are developer notes on how to build Cosanta Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)
- [Android Build Notes](build-android.md)

Development
---------------------
The Cosanta Core repo's [root README](/README.md) contains relevant information on the development process and automated testing.

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
* See the [Cosanta Developer Documentation](https://dashcore.readme.io/)
  for technical specifications and implementation details.
* Discuss on the [Cosanta Forum](https://cosa.is/forum), in the Development & Technical Discussion board.
* Discuss on [Cosanta Discord](http://staydashy.com)
* Discuss on [Cosanta Developers Discord](http://chat.cosantadevs.org/)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [cosanta.conf Configuration File](dash-conf.md)
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
