load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

# Command handler for JSON command processing
cc_library(
    name = "command_handler",
    srcs = ["command_handler.cc"],
    hdrs = ["command_handler.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config:pmd_thread_manager",
        "//:dpdk_lib",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

# Unix socket server for async socket I/O
cc_library(
    name = "unix_socket_server",
    srcs = ["unix_socket_server.cc"],
    hdrs = ["unix_socket_server.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@boost.asio",
    ],
)

# Signal handler for signal integration
cc_library(
    name = "signal_handler",
    srcs = ["signal_handler.cc"],
    hdrs = ["signal_handler.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@boost.asio",
    ],
)

# Control plane orchestrator
cc_library(
    name = "control_plane",
    srcs = ["control_plane.cc"],
    hdrs = ["control_plane.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":command_handler",
        ":signal_handler",
        ":unix_socket_server",
        "//config:pmd_thread_manager",
        "//rcu:rcu_manager",
        "//:dpdk_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@boost.asio",
    ],
)

# Command handler test (optional - not yet implemented)
# cc_test(
#     name = "command_handler_test",
#     size = "small",
#     srcs = ["command_handler_test.cc"],
#     deps = [
#         ":command_handler",
#     ],
# )

# Unix socket server test (optional - not yet implemented)
# cc_test(
#     name = "unix_socket_server_test",
#     size = "small",
#     srcs = ["unix_socket_server_test.cc"],
#     deps = [
#         ":unix_socket_server",
#     ],
# )

# Signal handler test (optional - not yet implemented)
# cc_test(
#     name = "signal_handler_test",
#     size = "small",
#     srcs = ["signal_handler_test.cc"],
#     deps = [
#         ":signal_handler",
#     ],
# )

# Control plane test (optional - not yet implemented)
# cc_test(
#     name = "control_plane_test",
#     size = "small",
#     srcs = ["control_plane_test.cc"],
#     deps = [
#         ":control_plane",
#     ],
# )
