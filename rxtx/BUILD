load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "packet",
    hdrs = ["packet.h"],
    deps = ["//:dpdk_lib"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "batch",
    hdrs = ["batch.h"],
    deps = ["//rxtx:packet"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "test_utils",
    hdrs = ["test_utils.h"],
    deps = ["//:dpdk_lib"],
    testonly = True,
    visibility = ["//visibility:public"],
)

cc_test(
    name = "packet_test",
    size = "small",
    srcs = ["packet_test.cc"],
    deps = [
        "//rxtx:packet",
        "//rxtx:test_utils",
        "@googletest//:gtest",
    ],
    linkopts = [
        "-lnuma",
        "-latomic",
    ],
)

cc_test(
    name = "batch_test",
    size = "small",
    srcs = ["batch_test.cc"],
    deps = [
        "//rxtx:batch",
        "//rxtx:test_utils",
        "@googletest//:gtest",
    ],
    linkopts = [
        "-lnuma",
        "-latomic",
    ],
)
