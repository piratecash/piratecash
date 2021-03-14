GENERIC BUILD NOTES
====================
Some notes on how to build PirateCash based on the [depends](../depends/README.md) build system.

Note on old build instructions
------------------------------
In the past, the build documentation contained instructions on how to build PirateCash with system-wide installed dependencies
like BerkeleyDB 4.8, boost and Qt. Building this way is considered deprecated and only building with the `depends` prefix
is supported today.

Required build tools and environment
------------------------------------
Building the dependencies and PirateCash requires some essential build tools to be installed before. Please see
[build-unix](build-unix.md) for details.

Building dependencies
---------------------
PirateCash inherited the `depends` folder from Bitcoin, which contains all dependencies required to build PirateCash. These
dependencies must be built before PirateCash can actually be built. To do so, perform the following:

```bash
$ cd depends
$ make -j4 # Choose a good -j value, depending on the number of CPU cores available
$ cd ..
```

This will download and build all dependencies required to build PirateCash. Caching of build results will ensure that only
the packages are rebuilt which have changed since the last depends build.

It is required to re-run the above commands from time to time when dependencies have been updated or added. If this is
not done, build failures might occur when building PirateCash.

Please read the [depends](../depends/README.md) documentation for more details on supported hosts and configuration
options. If no host is specified (as in the above example) when calling `make`, the depends system will default to your
local host system. 

Building PirateCash
---------------------

```bash
$ # Build CLI
$ cd src/
$ make -f makefile.unix
$ strip piratecashd # optional
$ # Build QT
$ cd ..
$ $PWD/depends/<host>/native/bin/qmake -spec linux-g++ STATIC=1 RELEASE=1 -o Makefile piratecash.pro
$ make
```

Please replace `<host>` with your local system's `host-platform-triplet`. The following triplets are usually valid:
- `i686-pc-linux-gnu` for Linux32
- `x86_64-pc-linux-gnu` for Linux64
- `arm-linux-gnueabihf` for Linux ARM 32 bit
- `aarch64-linux-gnu` for Linux ARM 64 bit

If you want to cross-compile for another platform, choose the appropriate `<host>` and make sure to build the
dependencies with the same host before.

