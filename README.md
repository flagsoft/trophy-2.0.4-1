# trophy-2.0.4-1


<img width="1602" height="1202" alt="img01" src="https://github.com/user-attachments/assets/57fbd053-2726-454d-abb1-f40a07b0ed4f" />



NOTE: This works only with macOS right now. Because it uses a compatibility layer for ClanLib 1.0 calls to ClanLib 4.1.0.

Trophy 2D car racing game (with Clanlib 4.1.0)

Known to work on macOS Sequoia(15.8.1) with Intel chip.

This is Trophy 2.0.4 ported to ClanLib 4.1.0. The game code is almost unchanged.
`src/cl1compat.h` and `src/cl1compat.cpp` provide the ClanLib 1.0 calls that
Trophy uses (surfaces, sprites, bitmap fonts, resources.xml, sound, keyboard,
mouse, window) on top of ClanLib 4.1.


## Build

### Download and install clanlib version 4.1.0.

https://github.com/sphair/ClanLib

### Build clanlib with

```
bash ./autogen.sh
./configure
```

### Download trophy 2.0.4-1 (macOS port)

(This repository, ZIP or via git.)

### Build with

```
bash ./DO-build.sh
```

Which does:

```
cd ./src
cmake -S . -B build \
	  -DCLANLIB_SOURCE_DIR=/Volumes/DATA-5TB/trophy/clanlib/4.1.0/ClanLib-4.1.0 \
	  -DCLANLIB_BUILD_DIR=/Volumes/DATA-5TB/trophy/clanlib/4.1.0/ClanLib-4.1.0
cmake --build build
```


### run it
```
./src/build/trophy
```


`CLANLIB_BUILD_DIR` is searched recursively for libclanApp, libclanGL,
libclanDisplay, libclanSound and libclanCore (.a or .dylib). To see where they
are: `find /Volumes/DATA-5TB/trophy/clanlib -name 'libclan*'`. If your build
dir has several copies (Debug/Release), pass the full list instead:
`-DCLANLIB_LIBRARIES="/path/libclanApp.a;/path/libclanGL.a;..."`.
If you built ClanLib with the Xcode generator, add `-DCMAKE_BUILD_TYPE=Debug`
(or build with `cmake --build build --config Debug`) to match.

The game finds `resources.xml` through the path baked in at build time
(`<trophy-2.0.4>/resources`), so it can be started from any directory.

## What was changed in the game code

- `#include <ClanLib/...>` replaced with `#include "cl1compat.h"`
- `std::auto_ptr` -> `std::unique_ptr`, `std::bind2nd`/`binary_function`
  removed (gone in C++17)
- `CAImageManipulation::changeHSV` reads and writes plain RGBA bytes
- `CASoundEffect::stop` read one element past the end of its array; fixed
- Network sources (`canetserver`, `canetclient`) are not built; nothing uses them
