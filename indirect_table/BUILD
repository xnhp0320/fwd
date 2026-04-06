load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "value_slot",
    hdrs = ["value_slot.h"],
    visibility = ["//visibility:public"],
    deps = ["//rcu:intrusive_rcu_list"],
)

cc_library(
    name = "key_entry",
    hdrs = ["key_entry.h"],
    visibility = ["//visibility:public"],
    deps = ["//rcu:intrusive_rcu_list"],
)

cc_library(
    name = "functors",
    hdrs = ["functors.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":key_entry",
        ":value_slot",
    ],
)

cc_library(
    name = "slot_array",
    hdrs = ["slot_array.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":functors",
        ":value_slot",
        "//rxtx:rcu_hash_table",
        "//:dpdk_lib",
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "indirect_table",
    hdrs = ["indirect_table.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":functors",
        ":key_entry",
        ":slot_array",
        "//rcu:rcu_manager",
        "//rcu:rcu_retire",
        "//rxtx:rcu_hash_table",
        "//:dpdk_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "indirect_table_test",
    size = "small",
    srcs = ["indirect_table_test.cc"],
    deps = [
        ":indirect_table",
        "//rcu:rcu_manager",
        "//rcu:rcu_retire",
        "//rxtx:test_utils",
        "//:dpdk_lib",
        "@boost.asio",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "slot_array_test",
    size = "small",
    srcs = ["slot_array_test.cc"],
    deps = [
        ":slot_array",
        "//rxtx:test_utils",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "indirect_table_pbt",
    size = "medium",
    srcs = ["indirect_table_pbt.cc"],
    deps = [
        ":indirect_table",
        "//rcu:rcu_manager",
        "//rcu:rcu_retire",
        "//rxtx:test_utils",
        "//:dpdk_lib",
        "@boost.asio",
        "@googletest//:gtest_main",
        "@rapidcheck",
        "@rapidcheck//:rapidcheck_gtest",
    ],
)
