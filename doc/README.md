PirateCash Core
==========

This is the official reference wallet for PirateCash digital currency and comprises the backbone of the PirateCash peer-to-peer network. You can [download PirateCash Core](https://www.dash.org/downloads/) or [build it yourself](#building) using the guides below.

Running
---------------------
The following are some helpful notes on how to run Dash on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/piratecash-qt` (GUI) or
- `bin/piratecashd` (headless)

### Windows

Unpack the files into a directory, and then run piratecash-qt.exe.

### macOS

Drag PirateCash-Qt to your applications folder, and then run PirateCash-Qt.

### Need Help?

* See the [Dash documentation](https://docs.dash.org)
for help and more information.
* See the [Dash Developer Documentation](https://dash-docs.github.io/) 
for technical specifications and implementation details.
* Ask for help on [Dash Nation Discord](http://dashchat.org)
* Ask for help on the [Dash Forum](https://dash.org/forum)

Building
---------------------
The following are developer notes on how to build PirateCash Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The PirateCash Core repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- Source Code Documentation ***TODO***
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Travis CI](travis-ci.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* Discuss on the [Dash Forum](https://dash.org/forum), in the Development & Technical Discussion board.
* Discuss on [Dash Nation Discord](http://dashchat.org)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
