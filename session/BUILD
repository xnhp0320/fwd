load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "session_entry",
    hdrs = ["session_entry.h"],
)

cc_library(
    name = "session_key",
    hdrs = ["session_key.h"],
    deps = ["//rxtx:packet_metadata"],
)

cc_library(
    name = "session_table",
    srcs = ["session_table.cc"],
    hdrs = ["session_table.h"],
    deps = [
        ":session_entry",
        ":session_key",
        "//:dpdk_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "session_table_test",
    size = "small",
    srcs = ["session_table_test.cc"],
    deps = [
        ":session_table",
        ":session_entry",
        ":session_key",
        "//rxtx:packet_metadata",
        "//rxtx:test_utils",
        "//:dpdk_lib",
        "@googletest//:gtest",
    ],
    linkopts = [
        "-lnuma",
        "-latomic",
    ],
)
