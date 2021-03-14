UNIX BUILD NOTES
====================
Some notes on how to PirateCash in Unix.

Base build dependencies
-----------------------
Building the dependencies and PirateCash requires some essential build tools and libraries to be installed before.

Run the following commands to install required packages:

##### Debian/Ubuntu:
```bash
$ sudo apt-get install curl build-essential libtool autotools-dev automake pkg-config python3 bsdmainutils cmake
```

##### Fedora:
```bash
$ sudo dnf install gcc-c++ libtool make autoconf automake python3 cmake libstdc++-static patch
```

##### Arch Linux:
```bash
$ pacman -S base-devel python3 cmake
```

##### Alpine Linux:
```sh
$ sudo apk --update --no-cache add autoconf automake cmake curl g++ gcc libexecinfo-dev libexecinfo-static libtool make perl pkgconfig python3 patch linux-headers
```

##### FreeBSD/OpenBSD:
```bash
pkg_add gmake cmake libtool
pkg_add autoconf # (select highest version, e.g. 2.69)
pkg_add automake # (select highest version, e.g. 1.15)
pkg_add python # (select highest version, e.g. 3.5)
```

Building
--------

Follow the instructions in [build-generic](build-generic.md)

Building on FreeBSD
--------------------

(TODO, this is untested, please report if it works and if changes to this documentation are needed)

Building on FreeBSD is basically the same as on Linux based systems, with the difference that you have to use `gmake`
instead of `make`.

*Note on debugging*: The version of `gdb` installed by default is [ancient and considered harmful](https://wiki.freebsd.org/GdbRetirement).
It is not suitable for debugging a multi-threaded C++ program, not even for getting backtraces. Please install the package `gdb` and
use the versioned gdb command e.g. `gdb7111`.

Building on OpenBSD
-------------------

(TODO, this is untested, please report if it works and if changes to this documentation are needed)

**Important**: From OpenBSD 6.2 onwards a C++11-supporting clang compiler is
part of the base image, and while building it is necessary to make sure that this
compiler is used and not ancient g++ 4.2.1. This is done by appending
`CC=cc CXX=c++` to configuration commands. Mixing different compilers
within the same executable will result in linker errors.

```bash
$ cd depends
$ make CC=cc CXX=c++
$ cd ..
$ export AUTOCONF_VERSION=2.69 # replace this with the autoconf version that you installed
$ export AUTOMAKE_VERSION=1.15 # replace this with the automake version that you installed
$ $PWD/depends/<host>/native/bin/qmake -spec linux-g++ STATIC=1 RELEASE=1 -o Makefile piratecash.pro
$ gmake # use -jX here for parallelism
```

OpenBSD Resource limits
-------------------
If the build runs into out-of-memory errors, the instructions in this section
might help.

The standard ulimit restrictions in OpenBSD are very strict:

    data(kbytes)         1572864

This, unfortunately, in some cases not enough to compile some `.cpp` files in the project,
(see issue [#6658](https://github.com/bitcoin/bitcoin/issues/6658)).
If your user is in the `staff` group the limit can be raised with:

    ulimit -d 3000000

The change will only affect the current shell and processes spawned by it. To
make the change system-wide, change `datasize-cur` and `datasize-max` in
`/etc/login.conf`, and reboot.
