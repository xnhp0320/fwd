load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "fib_loader",
    srcs = ["fib_loader.cc"],
    hdrs = ["fib_loader.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//tbm:tbmlib",
        "//:dpdk_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "fib_loader_test",
    size = "small",
    srcs = ["fib_loader_test.cc"],
    data = ["ipv4_test_fib.txt"],
    deps = [
        ":fib_loader",
        "//tbm:tbmlib",
    ],
)
