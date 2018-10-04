#!/bin/bash

set -e

VERSION=master

curl https://github.com/WebAssembly/wabt/archive/"$VERSION".tar.gz -sLo wabt-"$VERSION".tar.gz
tar xf wabt-"$VERSION".tar.gz
cd wabt-"$VERSION"

mkdir build
cd build

# libevent defaults CMAKE_BUILD_TYPE to Release
build_type=Release
if [[ "${OS}" == "Windows_NT" ]]; then
  # On Windows, every object file in the final executable needs to be compiled to use the
  # same version of the C Runtime Library. If Envoy is built with '-c dbg', then it will
  # use the Debug C Runtime Library. Setting CMAKE_BUILD_TYPE to Debug will cause libevent
  # to use the debug version as well
  # TODO: when '-c fastbuild' and '-c opt' work for Windows builds, set this appropriately
  build_type=Debug
fi

cmake -G "Ninja" \
  -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
  -DBUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE="$build_type" \
  ..
ninja
ninja install

cp libwabt.a "$THIRDPARTY_BUILD/lib"

mkdir -p "$THIRDPARTY_BUILD/include/wabt/interp"
ls -la ../src/ > /tmp/list.txt
ls -la ../ > /tmp/list.txt
cp config.h ../src/*.h ../src/*.def "$THIRDPARTY_BUILD/include/wabt"
cp ../src/interp/*.h "$THIRDPARTY_BUILD/include/wabt/interp"

# TODO(dio): I'm too lazy for re-prefixing all the include paths.
mkdir -p "$THIRDPARTY_BUILD/include/wabt/src/interp"
cp config.h ../src/*.h ../src/*.def "$THIRDPARTY_BUILD/include/wabt/src"
cp ../src/interp/*.h "$THIRDPARTY_BUILD/include/wabt/src/interp"
