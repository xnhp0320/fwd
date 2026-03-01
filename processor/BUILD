load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

# CRTP base class for packet processors (header-only)
cc_library(
    name = "packet_processor_base",
    hdrs = ["packet_processor_base.h"],
    deps = [
        "//config:dpdk_config",
        "@abseil-cpp//absl/status",
    ],
)

# Processor registry: name → factory mapping
cc_library(
    name = "processor_registry",
    srcs = ["processor_registry.cc"],
    hdrs = ["processor_registry.h"],
    deps = [
        ":packet_processor_base",
        "//config:dpdk_config",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

# Simple forwarding processor: drains all RX queues into a single TX queue
cc_library(
    name = "simple_forwarding_processor",
    srcs = ["simple_forwarding_processor.cc"],
    hdrs = ["simple_forwarding_processor.h"],
    deps = [
        ":packet_processor_base",
        ":processor_registry",
        "//config:dpdk_config",
        "//rxtx:batch",
        "//:dpdk_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)
