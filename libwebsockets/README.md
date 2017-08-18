Linux build command:
```
CC=~/gcc-4.6.4/bin/gcc cmake -DCMAKE_C_FLAGS=-m32 -DLWS_WITH_SHARED=OFF -DLWS_WITH_SSL=OFF -DLWS_WITH_ZLIB=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_EXTENSIONS=ON -DLWS_WITHOUT_DAEMONIZE=ON -DLWS_IPV6=ON -DLWS_WITH_PLUGINS=OFF -DLWS_WITH_RANGES=ON -DLWS_WITH_ZIP_FOPS=OFF -DLWS_MAX_SMP=1 ..
```
Anonymize `LWS_BUILD_HASH` in `lws_config.h`.
Build with `make -j8`

Windows:
```
cmake -DLWS_WITH_SHARED=OFF -DLWS_WITH_SSL=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_MAX_SMP=1 ..
```
Anonymize `LWS_BUILD_HASH` in `lws_config.h`.
Replace all instances of `/MD` with `/MT` in CMakeCache.txt, run just `cmake ..`, then build solution.

