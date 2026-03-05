load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "packet_metadata",
    hdrs = ["packet_metadata.h"],
    deps = ["//:dpdk_lib"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "packet_metadata_impl",
    srcs = ["packet_metadata.cc"],
    deps = [
        "//rxtx:packet",
        "//rxtx:packet_metadata",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "packet",
    hdrs = ["packet.h"],
    deps = [
        "//:dpdk_lib",
        "//rxtx:packet_metadata",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "batch",
    hdrs = ["batch.h"],
    deps = ["//rxtx:packet"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "test_utils",
    hdrs = ["test_utils.h"],
    deps = ["//:dpdk_lib"],
    testonly = True,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "test_packet_builder",
    hdrs = ["test_packet_builder.h"],
    deps = [
        "//rxtx:test_utils",
        "//:dpdk_lib",
    ],
    testonly = True,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "hex_packet",
    hdrs = ["hex_packet.h"],
    deps = [
        "//rxtx:packet",
        "//rxtx:test_utils",
        "//:dpdk_lib",
    ],
    testonly = True,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "test_packet_data",
    hdrs = ["test_packet_data.h"],
    testonly = True,
    visibility = ["//visibility:public"],
)

cc_test(
    name = "packet_test",
    size = "small",
    srcs = ["packet_test.cc"],
    deps = [
        "//rxtx:packet",
        "//rxtx:packet_metadata",
        "//rxtx:test_utils",
        "@googletest//:gtest",
    ],
    linkopts = [
        "-lnuma",
        "-latomic",
    ],
)

cc_test(
    name = "batch_test",
    size = "small",
    srcs = ["batch_test.cc"],
    deps = [
        "//rxtx:batch",
        "//rxtx:test_utils",
        "@googletest//:gtest",
    ],
    linkopts = [
        "-lnuma",
        "-latomic",
    ],
)

cc_test(
    name = "packet_metadata_test",
    size = "small",
    srcs = ["packet_metadata_test.cc"],
    deps = [
        "//rxtx:packet",
        "//rxtx:packet_metadata",
        "//rxtx:packet_metadata_impl",
        "//rxtx:test_utils",
        "//rxtx:test_packet_builder",
        "//rxtx:hex_packet",
        "//rxtx:test_packet_data",
        "@googletest//:gtest",
        "@rapidcheck",
        "@rapidcheck//:rapidcheck_gtest",
    ],
    linkopts = [
        "-lnuma",
        "-latomic",
    ],
)

cc_library(
    name = "allocator",
    hdrs = ["allocator.h"],
    deps = [
        "//rxtx:packet",
        "//:dpdk_lib",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "list_slab",
    hdrs = ["list_slab.h"],
    deps = [
        "//rxtx:allocator",
        "@boost.intrusive",
    ],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "list_slab_test",
    size = "small",
    srcs = ["list_slab_test.cc"],
    deps = [
        "//rxtx:list_slab",
        "//rxtx:allocator",
        "@boost.intrusive",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "lookup_entry",
    hdrs = ["lookup_entry.h"],
    deps = [
        "//rxtx:packet_metadata",
        "//rxtx:packet",
        "@boost.intrusive",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/strings",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "fast_lookup_table",
    hdrs = ["fast_lookup_table.h"],
    deps = [
        "//rxtx:list_slab",
        "//rxtx:lookup_entry",
        "//rxtx:allocator",
        "//rxtx:packet_metadata",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/hash",
    ],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "fast_lookup_table_test",
    size = "small",
    srcs = ["fast_lookup_table_test.cc"],
    deps = [
        "//rxtx:fast_lookup_table",
        "//rxtx:list_slab",
        "//rxtx:lookup_entry",
        "//rxtx:allocator",
        "//rxtx:packet_metadata",
        "//rxtx:packet",
        "@googletest//:gtest",
    ],
    linkopts = [
        "-lnuma",
        "-latomic",
    ],
)
