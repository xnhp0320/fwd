load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "vm_location_key",
    hdrs = ["vm_location_key.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//rxtx:packet_metadata",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "tunnel_info",
    hdrs = ["tunnel_info.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//rxtx:packet_metadata",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "vm_location_table",
    hdrs = ["vm_location_table.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":vm_location_key",
        ":tunnel_info",
        "//indirect_table:indirect_table",
    ],
)
