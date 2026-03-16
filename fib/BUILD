load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "fib_loader",
    srcs = ["fib_loader.cc"],
    hdrs = ["fib_loader.h"],
    visibility = ["//visibility:public"],
    deps = ["@abseil-cpp//absl/status"],
)

cc_test(
    name = "fib_loader_test",
    size = "small",
    srcs = ["fib_loader_test.cc"],
    deps = [":fib_loader"],
)
