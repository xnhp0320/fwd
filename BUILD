load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

# Alias to the DPDK external repository (configured per-machine in MODULE.bazel)
alias(
    name = "dpdk_lib",
    actual = "@dpdk//:dpdk",
    visibility = ["//visibility:public"],
)

# Original hello_world binary
cc_binary(
    name = "main",
    srcs = ["main.cc"],
    linkopts = [
        "-lnuma",
        "-latomic",
    ],
    deps = [
        ":dpdk_lib",
        "//config:config_parser",
        "//config:config_printer",
        "//config:config_validator",
        "//config:dpdk_initializer",
        "//config:pmd_thread_manager",
        "//control:control_plane",
        "//processor:five_tuple_forwarding_processor",
        "//processor:lpm_forwarding_processor",
        "//processor:simple_forwarding_processor",
        "//rcu:rcu_manager",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/strings",
        "@boost.asio",
    ],
)

