load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

# Configuration data structures (header-only)
cc_library(
    name = "dpdk_config",
    hdrs = ["dpdk_config.h"],
    visibility = ["//visibility:public"],
)

# JSON configuration parser
cc_library(
    name = "config_parser",
    srcs = ["config_parser.cc"],
    hdrs = ["config_parser.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

# Configuration validator
cc_library(
    name = "config_validator",
    srcs = ["config_validator.cc"],
    hdrs = ["config_validator.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

# Configuration printer
cc_library(
    name = "config_printer",
    srcs = ["config_printer.cc"],
    hdrs = ["config_printer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        "@nlohmann_json//:json",
    ],
)

# DPDK port
cc_library(
    name = "dpdk_port",
    srcs = ["dpdk_port.cc"],
    hdrs = ["dpdk_port.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        "//:dpdk_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

# Port manager
cc_library(
    name = "port_manager",
    srcs = ["port_manager.cc"],
    hdrs = ["port_manager.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        ":dpdk_port",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

# PMD thread manager
cc_library(
    name = "pmd_thread",
    srcs = ["pmd_thread.cc"],
    hdrs = ["pmd_thread.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        "//:dpdk_lib",
        "//processor:processor_registry",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

# PMD thread manager (new manager class)
cc_library(
    name = "pmd_thread_manager",
    srcs = ["pmd_thread_manager.cc"],
    hdrs = ["pmd_thread_manager.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        ":pmd_thread",
        "//:dpdk_lib",
        "//processor:processor_registry",
        "//processor:simple_forwarding_processor",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

# DPDK initializer
cc_library(
    name = "dpdk_initializer",
    srcs = ["dpdk_initializer.cc"],
    hdrs = ["dpdk_initializer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        ":pmd_thread_manager",
        ":port_manager",
        "//:dpdk_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

# Configuration parser test
cc_test(
    name = "config_parser_test",
    size = "small",
    srcs = ["config_parser_test.cc"],
    deps = [
        ":config_parser",
        ":dpdk_config",
    ],
)

# Configuration validator test
cc_test(
    name = "config_validator_test",
    size = "small",
    srcs = ["config_validator_test.cc"],
    deps = [
        ":config_validator",
        ":dpdk_config",
    ],
)

# Configuration printer test
cc_test(
    name = "config_printer_test",
    size = "small",
    srcs = ["config_printer_test.cc"],
    deps = [
        ":config_parser",
        ":config_printer",
        ":dpdk_config",
    ],
)

# PMD thread test (optional - not yet implemented)
# cc_test(
#     name = "pmd_thread_test",
#     size = "small",
#     srcs = ["pmd_thread_test.cc"],
#     deps = [
#         ":dpdk_config",
#         ":pmd_thread",
#     ],
# )

# PMD thread manager test (optional - not yet implemented)
# cc_test(
#     name = "pmd_thread_manager_test",
#     size = "small",
#     srcs = ["pmd_thread_manager_test.cc"],
#     deps = [
#         ":dpdk_config",
#         ":pmd_thread",
#         ":pmd_thread_manager",
#     ],
# )
