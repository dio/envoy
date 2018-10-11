#!/bin/bash

set -e

VERSION=bazel-make

curl https://github.com/dio/wasm-c-api/archive/"$VERSION".tar.gz -sLo wasm-c-api-"$VERSION".tar.gz
tar xf wasm-c-api-"$VERSION".tar.gz
cd wasm-c-api-"$VERSION"

make v8-checkout
make -j8 v8
make all
make wasm

cp v8/v8/out.gn/x64.release/obj/libv8_monolith.a "$THIRDPARTY_BUILD/lib"
cp out/wasm.a $THIRDPARTY_BUILD/lib/wasm.a
mkdir -p "$THIRDPARTY_BUILD/include/wasm-c-api"
cp include/* "$THIRDPARTY_BUILD/include/wasm-c-api"
