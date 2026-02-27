load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "dpdk_lib",
    copts = ["-include rte_config.h"],
    includes = ["/usr/local/include"],
    linkopts = [
        "-L/usr/local/lib/aarch64-linux-gnu/",
        "-Wl,--as-needed -L/usr/local/lib/aarch64-linux-gnu -lrte_node -lrte_graph -lrte_pipeline -lrte_table -lrte_pdump -lrte_port -lrte_fib -lrte_pdcp -lrte_ipsec -lrte_vhost -lrte_stack -lrte_security -lrte_sched -lrte_reorder -lrte_rib -lrte_mldev -lrte_regexdev -lrte_rawdev -lrte_power -lrte_pcapng -lrte_member -lrte_lpm -lrte_latencystats -lrte_jobstats -lrte_ip_frag -lrte_gso -lrte_gro -lrte_gpudev -lrte_dispatcher -lrte_eventdev -lrte_efd -lrte_dmadev -lrte_distributor -lrte_cryptodev -lrte_compressdev -lrte_cfgfile -lrte_bpf -lrte_bitratestats -lrte_bbdev -lrte_acl -lrte_timer -lrte_hash -lrte_metrics -lrte_cmdline -lrte_pci -lrte_ethdev -lrte_meter -lrte_net -lrte_mbuf -lrte_mempool -lrte_rcu -lrte_ring -lrte_eal -lrte_telemetry -lrte_argparse -lrte_kvargs -lrte_log",
    ],
    visibility = ["//visibility:public"],
)

# Configuration data structures (header-only)
cc_library(
    name = "dpdk_config",
    hdrs = ["config/dpdk_config.h"],
    visibility = ["//visibility:public"],
)

# JSON configuration parser
cc_library(
    name = "config_parser",
    srcs = ["config/config_parser.cc"],
    hdrs = ["config/config_parser.h"],
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
    srcs = ["config/config_validator.cc"],
    hdrs = ["config/config_validator.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

# Configuration validator test
cc_test(
    name = "config_validator_test",
    srcs = ["config/config_validator_test.cc"],
    deps = [
        ":config_validator",
        ":dpdk_config",
    ],
)

# Configuration printer test
cc_test(
    name = "config_printer_test",
    srcs = ["config/config_printer_test.cc"],
    deps = [
        ":config_parser",
        ":config_printer",
        ":dpdk_config",
    ],
)

# Configuration printer
cc_library(
    name = "config_printer",
    srcs = ["config/config_printer.cc"],
    hdrs = ["config/config_printer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        "@nlohmann_json//:json",
    ],
)

# DPDK port
cc_library(
    name = "dpdk_port",
    srcs = ["config/dpdk_port.cc"],
    hdrs = ["config/dpdk_port.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        ":dpdk_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

# Port manager
cc_library(
    name = "port_manager",
    srcs = ["config/port_manager.cc"],
    hdrs = ["config/port_manager.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        ":dpdk_port",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

# DPDK initializer
cc_library(
    name = "dpdk_initializer",
    srcs = ["config/dpdk_initializer.cc"],
    hdrs = ["config/dpdk_initializer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dpdk_config",
        ":dpdk_lib",
        ":port_manager",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
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
        ":config_parser",
        ":config_printer",
        ":config_validator",
        ":dpdk_initializer",
        ":dpdk_lib",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/strings",
        "@boost.asio",
    ],
)
