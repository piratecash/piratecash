### Usage

To build dependencies for the current arch+OS:

    make

To build for another arch/OS:

    make HOST=host-platform-triplet

For example:

    make HOST=x86_64-pc-linux-gnu -j4

Common `host-platform-triplets` for cross compilation are:

- `x86_64-pc-linux-gnu` for Linux 64 bit
- `arm-linux-gnueabihf` for Linux ARM 32 bit
- `aarch64-linux-gnu` for Linux ARM 64 bit

No other options are needed, the paths are automatically configured.

Install the required dependencies: Ubuntu & Debian
--------------------------------------------------

- see [build-unix.md](../doc/build-unix.md)

### Other documentation

- [description.md](description.md): General description of the depends system
- [packages.md](packages.md): Steps for adding packages

