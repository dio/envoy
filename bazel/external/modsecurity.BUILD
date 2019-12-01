licenses(["notice"])  # Apache 2

load(":genrule_cmd.bzl", "genrule_cmd")

cc_library(
    name = "modsecurity",
    srcs = [
        "libmodsecurity.a",
    ],
    hdrs = glob(["headers/modsecurity/**/*.h"]),
    includes = ["headers"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "build",
    srcs = glob(["**"]),
    outs = [
        "libmodsecurity.a",
    ],
    cmd = genrule_cmd("@envoy//bazel/external:modsecurity.genrule_cmd"),
)
