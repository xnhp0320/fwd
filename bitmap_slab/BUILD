load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "bitmap_slab",
    hdrs = ["bitmap_slab.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "bitmap_slab_test",
    size = "small",
    srcs = ["bitmap_slab_test.cc"],
    deps = [
        ":bitmap_slab",
        "@googletest//:gtest",
    ],
)
