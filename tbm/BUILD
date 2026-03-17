load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

cc_import(
    name = "tbmlib_import",
    static_library = "tbmlib.a",
)

cc_library(
    name = "tbmlib",
    hdrs = ["tbmlib.h"],
    deps = [":tbmlib_import"],
    visibility = ["//visibility:public"],
)
