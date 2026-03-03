load("@rules_go//go:def.bzl", "go_binary", "go_library")

go_library(
    name = "fwdcli_lib",
    srcs = ["main.go"],
    importpath = "fwdcli",
    deps = ["//fwdcli/cmd"],
)

go_binary(
    name = "fwdcli",
    embed = [":fwdcli_lib"],
    x_defs = {
        "fwdcli/cmd.version": "{STABLE_VERSION}",
    },
    visibility = ["//visibility:public"],
)
