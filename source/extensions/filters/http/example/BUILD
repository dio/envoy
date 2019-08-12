licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library")

api_proto_library(
    name = "http_filter_proto",
    srcs = ["http_filter.proto"],
)

envoy_cc_library(
    name = "http_filter_lib",
    srcs = ["http_filter.cc"],
    hdrs = ["http_filter.h"],
    deps = [
        ":http_filter_proto_cc",
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["http_filter_config.cc"],
    deps = [
        ":http_filter_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:json_utility_lib",
        "//source/extensions/filters/http:well_known_names",
    ],
)
