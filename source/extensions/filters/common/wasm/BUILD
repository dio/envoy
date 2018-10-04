licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "wasm_lib",
    srcs = ["wasm.cc"],
    hdrs = ["wasm.h"],
    external_deps = [
        "wabt",
    ],
)
