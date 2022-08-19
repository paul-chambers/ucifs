# ucifs

A FUSE filesystem to read &amp; write virtual files in the
'Universal Configuration Format' (UCI) format used by OpenWRT.

Uses `libelektra` as the backend to store the actual configuration values.
To install `libelektra`, please follow the instructions at:
https://github.com/Netgear/libelektra/blob/master/doc/INSTALL.md

`libuci` is a dependency, and headers/libraries must be installed to build.
Note that `libuci` depends in turn on `libubox`.

