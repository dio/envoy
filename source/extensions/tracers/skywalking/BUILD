load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Trace driver for SkyWalking (https://skywalking.apache.org)

envoy_extension_package()

envoy_cc_library(
    name = "skywalking_tracer_lib",
    srcs = [
        "propagation.cc",
        "skywalking_tracer_impl.cc",
        "span.cc",
        "tracer.cc",
    ],
    hdrs = [
        "propagation.h",
        "skywalking_tracer_impl.h",
        "span.h",
        "tracer.h",
    ],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/grpc:context_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/upstream:cluster_update_tracker_lib",
        "//source/extensions/tracers/common/ot:opentracing_driver_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":skywalking_tracer_lib",
        "//source/extensions/tracers/common:factory_base_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)
