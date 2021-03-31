Mac OS X Build Instructions and Notes
The assembly was tested on macOS 10.15.7
====================================
The commands in this guide should be executed in a Terminal application.
The built-in one is located in `/Applications/Utilities/Terminal.app`.

Preparation
-----------
Install the OS X command line tools and Xcode :

[Download - Command Line Tools for Xcode 11.3.1](https://download.developer.apple.com/Developer_Tools/Command_Line_Tools_for_Xcode_11.3.1/Command_Line_Tools_for_Xcode_11.3.1.dmg).
[Xcode_11.3.1.xip](https://download.developer.apple.com/Developer_Tools/Xcode_11.3.1/Xcode_11.3.1.xip).

Download and Install.

Then install [Homebrew](https://brew.sh).

Base build dependencies
-----------------------

```bash
brew install automake libtool pkg-config
```

If you want to build the disk image with `make deploy` (.dmg / optional), you need RSVG
```bash
brew install librsvg
```

Building
--------

Follow the instructions in [build-generic](build-generic.md)

