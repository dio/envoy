load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# Trace driver for OpenCensus: https://opencensus.io/

envoy_package()

envoy_cc_library(
    name = "zipkin_exporter_lib",
    srcs = ["zipkin_exporter.cc"],
    hdrs = ["zipkin_exporter.h"],
    copts = ["-Wno-unused-parameter"],
    external_deps = [
        "opencensus_trace",
    ],
    deps = [
        "//source/common/http:message_lib",
        "//source/common/common:thread_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)
