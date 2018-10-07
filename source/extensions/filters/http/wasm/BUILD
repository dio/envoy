licenses(["notice"])  # Apache 2

# WASM

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "wasm_filter_lib",
    srcs = ["wasm_filter.cc"],
    hdrs = ["wasm_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//source/extensions/filters/common/wasm:wasm_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/config/filter/http/wasm/v2alpha:wasm_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/wasm:wasm_filter_lib",
        "@envoy_api//envoy/config/filter/http/wasm/v2alpha:wasm_cc",
    ],
)
