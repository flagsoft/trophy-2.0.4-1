
# https://github.com/sphair/ClanLib

# Build clanlib with

#```
#bash ./autogen.sh
#./configure
#```


cmake -S . -B build \
      -DCLANLIB_SOURCE_DIR=/Volumes/DATA-5TB/trophy/clanlib/4.1.0/ClanLib-4.1.0 \
      -DCLANLIB_BUILD_DIR=/Volumes/DATA-5TB/trophy/clanlib/4.1.0/ClanLib-4.1.0

cmake --build build

#./build/trophy
