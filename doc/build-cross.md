Cross-compiliation of PirateCash
===============================

PirateCash can be cross-compiled on Linux to all other supported host systems. This is done by changing
the `HOST` parameter when building the dependencies and then specifying another directory when building PirateCash.

The following instructions are only tested on Debian Stretch and Ubuntu Bionic.

Windows 64bit Cross-compilation
-------------------------------
The steps below can be performed on Debian and Ubuntu (including in a VM) or WSL. The depends system
will also work on other Linux distributions, however the commands for
installing the toolchain will be different.

First, install the general dependencies:

    sudo apt update
    sudo apt upgrade
    sudo apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git python3 cmake

A host toolchain (`build-essential`) is necessary because some dependency
packages need to build host utilities that are used in the build process.

Acquire the source in the usual way:

    git clone https://github.com/piratecash/piratecash
    cd piratecash

### Building for 64-bit Windows

The first step is to install the mingw-w64 cross-compilation tool chain:

    sudo apt install g++-mingw-w64-x86-64

Ubuntu Bionic 18.04 <sup>[1](#footnote1)</sup>:

    sudo update-alternatives --config x86_64-w64-mingw32-g++ # Set the default mingw32 g++ compiler option to posix.
    sudo update-alternatives --config x86_64-w64-mingw32-gcc # Set the default mingw32 gcc compiler option to posix.

Build using:

    PATH=$(echo "$PATH" | sed -e 's/:\/mnt.*//g') # strip out problematic Windows %PATH% imported var
    cd depends
    make HOST=x86_64-w64-mingw32
    cd ../src
    make -f makefile.linux-mingw
    cd ..
    $PWD/depends/x86_64-w64-mingw32/native/bin/qmake -spec win32-g++ STATIC=1 RELEASE=1 USE_QRCODE=1 -o Makefile piratecash.pro
    make

### Depends system

For further documentation on the depends system see [README.md](../depends/README.md) in the depends directory.

Footnotes
---------

<a name="footnote1">1</a>: Starting from Ubuntu Xenial 16.04, both the 32 and 64 bit Mingw-w64 packages install two different
compiler options to allow a choice between either posix or win32 threads. The default option is win32 threads which is the more
efficient since it will result in binary code that links directly with the Windows kernel32.lib. Unfortunately, the headers
required to support win32 threads conflict with some of the classes in the C++11 standard library, in particular std::mutex.
It's not possible to build the PirateCash code using the win32 version of the Mingw-w64 cross compilers (at least not without
modifying headers in the PirateCash source code).
