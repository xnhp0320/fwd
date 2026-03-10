load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

# Per-thread packet/byte statistics counters (header-only)
cc_library(
    name = "packet_stats",
    hdrs = ["packet_stats.h"],
)

cc_library(
    name = "pmd_job",
    hdrs = ["pmd_job.h"],
    deps = [
        "@boost.intrusive",
    ],
)

# Extensible per-thread context for processor launchers (header-only)
cc_library(
    name = "processor_context",
    hdrs = ["processor_context.h"],
    deps = [
        ":packet_stats",
        "//rxtx:lookup_entry",
    ],
)

# CRTP base class for packet processors (header-only)
cc_library(
    name = "packet_processor_base",
    hdrs = ["packet_processor_base.h"],
    deps = [
        ":processor_context",
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
        ":pmd_job",
        ":processor_context",
        "//config:dpdk_config",
        "//control:command_api",
        "//:dpdk_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

# Simple forwarding processor: drains all RX queues into a single TX queue
# alwayslink ensures the static REGISTER_PROCESSOR initializer is not
# stripped by the linker when this library is a transitive dependency.
cc_library(
    name = "simple_forwarding_processor",
    srcs = ["simple_forwarding_processor.cc"],
    hdrs = ["simple_forwarding_processor.h"],
    alwayslink = True,
    deps = [
        ":packet_processor_base",
        ":packet_stats",
        ":processor_registry",
        "//config:dpdk_config",
        "//rxtx:batch",
        "//:dpdk_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

# Five-tuple forwarding processor: parses five-tuple, performs flow-table
# lookup/insert via FastLookupTable, and forwards packets to TX queue 0.
# alwayslink ensures the static REGISTER_PROCESSOR initializer is not
# stripped by the linker when this library is a transitive dependency.
cc_library(
    name = "five_tuple_forwarding_processor",
    srcs = ["five_tuple_forwarding_processor.cc"],
    hdrs = ["five_tuple_forwarding_processor.h"],
    alwayslink = True,
    deps = [
        ":packet_processor_base",
        ":packet_stats",
        ":pmd_job",
        ":processor_context",
        ":processor_registry",
        "//config:dpdk_config",
        "//control:command_api",
        "//rxtx:batch",
        "//rxtx:fast_lookup_table",
        "//rxtx:packet",
        "//rxtx:packet_metadata",
        "//rxtx:packet_metadata_impl",
        "//session:session_entry",
        "//session:session_key",
        "//session:session_table",
        "//:dpdk_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

# Five-tuple forwarding processor unit tests
cc_test(
    name = "pmd_job_test",
    size = "small",
    srcs = ["pmd_job_test.cc"],
    deps = [
        ":pmd_job",
        "@googletest//:gtest_main",
    ],
)

# Five-tuple forwarding processor unit tests
cc_test(
    name = "five_tuple_forwarding_processor_test",
    size = "small",
    srcs = ["five_tuple_forwarding_processor_test.cc"],
    deps = [
        ":five_tuple_forwarding_processor",
        ":processor_registry",
        "//config:dpdk_config",
        "//rxtx:packet_metadata_impl",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
    ],
)
