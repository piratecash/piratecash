# NetBSD Build Guide

Updated for NetBSD [9.2](https://netbsd.org/releases/formal-9/NetBSD-9.2.html).

This guide describes how to build piratecashd, command-line utilities, and GUI on NetBSD.

**This guide has not been tested for building PirateCash Core and may fail. Please report your results; contributions welcome.**

## Preparation

### 1. Install Required Dependencies

Install the required dependencies the usual way you [install software on NetBSD](https://www.netbsd.org/docs/guide/en/chap-boot.html#chap-boot-pkgsrc).
The example commands below use `pkgin`.

```bash
pkgin install autoconf automake libtool pkg-config git gmake boost libevent gmp openssl

```

NetBSD currently ships with an older version of `gcc` than is needed to build. You should upgrade your `gcc` and then pass this new version to the configure script.

For example, `gcc12` meets the minimum compiler version listed in [dependencies.md](dependencies.md):
```
pkgin install gcc12
```

Then, when configuring, pass the following:
```bash
./configure
    ...
    CC="/usr/pkg/gcc12/bin/gcc" \
    CXX="/usr/pkg/gcc12/bin/g++" \
    ...
```

See [dependencies.md](dependencies.md) for a complete overview.

### 2. Clone PirateCash Core Repo

Clone the PirateCash Core repository to a directory. All build scripts and commands will run from this directory.

```bash
git clone https://github.com/piratecash/piratecash.git
cd piratecash
```

### 3. Install Optional Dependencies

#### Wallet Dependencies

It is not necessary to build wallet functionality to run piratecashd or the GUI.

###### Descriptor Wallet Support

`sqlite3` is required to enable support for [descriptor wallets](descriptors.md).

```bash
pkgin install sqlite3
```

###### Legacy Wallet Support

`db4` is required to enable support for legacy wallets.

```bash
pkgin install db4
```

#### GUI Dependencies

PirateCash Core includes a GUI built with the cross-platform Qt Framework. To compile the GUI, we need to install `qt5`.

```bash
pkgin install qt5
```

The GUI can encode addresses in a QR Code. To build in QR support for the GUI, install `qrencode`.

```bash
pkgin install qrencode
```

#### Test Suite Dependencies

There is an included test suite that is useful for testing code changes when developing.
To run the test suite (recommended), you will need to have Python 3 installed:

```bash
pkgin install python310
```

### Building PirateCash Core

**Note**: Use `gmake` (the non-GNU `make` will exit with an error).


### 1. Configuration

There are many ways to configure PirateCash Core. Here is an example that
explicitly disables the wallet and GUI:

```bash
./autogen.sh
./configure --disable-wallet --with-gui=no \
    CPPFLAGS="-I/usr/pkg/include" \
    MAKE=gmake
```

For a full list of configuration options, see the output of `./configure --help`

### 2. Compile

Build and run the tests:

```bash
gmake # use "-j N" here for N parallel jobs
gmake check # Run tests if Python 3 is available
```
