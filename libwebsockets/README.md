Linux build command: `cmake -DLWS_WITH_SHARED=OFF -DLWS_WITH_SSL=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_MAX_SMP=1 -DCMAKE_C_FLAGS=-m32 ..`

Windows: `cmake -DLWS_WITH_SHARED=OFF -DLWS_WITH_SSL=OFF -DLWS_WITHOUT_TESTAPPS=ON -DLWS_MAX_SMP=1 ..`
Replace all instances of `/MD` with `/MT` in CMakeCache.txt, run just `cmake ..`, then build solution.