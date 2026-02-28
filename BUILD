load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

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
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/strings",
        "@boost.asio",
    ],
)

# Verification binary for dpdk.json integration test
cc_binary(
    name = "verify_dpdk_json",
    srcs = ["verify_dpdk_json.cc"],
    data = ["dpdk.json"],
    deps = [
        "//config:config_parser",
        "//config:config_printer",
        "//config:config_validator",
        "//config:dpdk_config",
    ],
)
