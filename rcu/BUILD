load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "deferred_work_item",
    hdrs = ["deferred_work_item.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "mpsc_queue",
    hdrs = ["mpsc_queue.h"],
    visibility = ["//visibility:public"],
    deps = [":deferred_work_item"],
)

cc_test(
    name = "mpsc_queue_test",
    srcs = ["mpsc_queue_test.cc"],
    deps = [
        ":mpsc_queue",
        "@googletest//:gtest_main",
        "@rapidcheck",
        "@rapidcheck//:rapidcheck_gtest",
    ],
)

cc_library(
    name = "rcu_manager",
    srcs = ["rcu_manager.cc"],
    hdrs = ["rcu_manager.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":deferred_work_item",
        ":mpsc_queue",
        "//:dpdk_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@boost.asio",
    ],
)

# cc_test(
#     name = "rcu_manager_test",
#     srcs = ["rcu_manager_test.cc"],
#     deps = [
#         ":rcu_manager",
#         "//:dpdk_lib",
#         "@googletest//:gtest_main",
#         "@rapidcheck",
#         "@rapidcheck//:rapidcheck_gtest",
#     ],
# )
