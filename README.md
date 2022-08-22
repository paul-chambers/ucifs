# ucifs

A FUSE filesystem to read &amp; write virtual files in the
'Universal Configuration Format' (UCI) format used by OpenWRT.

Uses `libelektra` as the backend to store the actual configuration values.
I recommend building `libelektra` from source, rather than following the
instructions at:
https://github.com/Netgear/libelektra/blob/master/doc/INSTALL.md

The repositories it refers to are out-of-date, both from the project
sources and the distro versions supported.

`libuci` is also a dependency, and its headers/libraries must be installed to build.
Note that `libuci` depends in turn on `libubox`.

