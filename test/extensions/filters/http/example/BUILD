licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "sample_test",
    srcs = ["http_filter_integration_test.cc"],
    extension_name = "sample",
    deps = [
        "//source/extensions/filters/http/example:config",
        "//test/integration:http_integration_lib",
    ],
)
