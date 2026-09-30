### Verify Binaries

#### Usage:

This script downloads `SHA256SUMS.asc` from the [PirateCash GitHub releases](https://github.com/piratecash/piratecash/releases) and an independent release mirror, and requires the two files to match.

Set `PIRATECASH_RELEASE_MIRROR` to the mirror's URL prefix before running the script. The script appends `<version>/SHA256SUMS.asc` to this prefix, so include any required trailing slash or `v`. No mirror URL is assumed; verification stops if the variable is unset.

It first checks if the signature passes, and then downloads the files specified in the file, and checks if the hashes of these files match those that are specified in the signature file.

The script returns 0 if everything passes the checks. It returns 1 if either the signature check or the hash check doesn't pass. If an error occurs the return value is at least 2.


```sh
./verify.py 24.0.0
./verify.py 24.0.0-rc.1
```

If you only want to download the binaries of certain platform, add the corresponding suffix, e.g.:

```sh
./verify.py 24.0.0-osx
./verify.py 24.0.0-linux
./verify.py 24.0.0-rc.1-win64
```

If you do not want to keep the downloaded binaries, specify anything as the second parameter.

```sh
./verify.py 24.0.0 delete
```
