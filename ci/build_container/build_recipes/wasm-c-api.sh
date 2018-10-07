#!/bin/bash

set -e

VERSION=v0.0.1

cp /Users/diorahman/Experiments/dio/wasm-c-api/$VERSION-darwin.tar.gz wasm-c-api-"$VERSION".tar.gz
tar xf wasm-c-api-"$VERSION".tar.gz
cd v0.0.1

cp lib/libv8_monolith.a "$THIRDPARTY_BUILD/lib"
mkdir -p "$THIRDPARTY_BUILD/include/wasm-c-api"
cp include/* "$THIRDPARTY_BUILD/include/wasm-c-api"
