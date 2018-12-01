# RADDI
https://www.raddi.net  
https://www.reddit.com/r/raddi

A project aiming to develop and introduce radically decentralized discussion platform. Employs reddit-esque concepts of user identities, communities, discussion trees, voting and moderator influence on content quality, built on modern cryptography. Decentralized, uncensorable.

## Status
Preview of node software, with limited-functonality command line application, is available for enthusiasts:
Downloadable from [raddi-builds-windows](https://github.com/raddinet/raddi-builds-windows) repository.

## Dependencies
Pre-built binaries of required libraries are available from [raddi-redist-windows](https://github.com/raddinet/raddi-redist-windows) repository.
Those are needed only for *release* builds. For *portable* builds everything necessary is already compiled in.

* https://github.com/jedisct1/libsodium
* https://github.com/tromp/cuckoo - adapted to [/lib/cuckoocycle.h](https://github.com/raddinet/raddi/blob/master/lib/cuckoocycle.h) ([.tcc](https://github.com/raddinet/raddi/blob/master/lib/cuckoocycle.tcc))
* https://tukaani.org/xz
* https://sqlite.org/ - client application
