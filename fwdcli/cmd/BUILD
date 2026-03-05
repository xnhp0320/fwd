load("@rules_go//go:def.bzl", "go_library")

go_library(
    name = "cmd",
    srcs = [
        "commands.go",
        "monitor.go",
        "root.go",
        "shutdown.go",
        "stats.go",
        "status.go",
        "threads.go",
    ],
    importpath = "fwdcli/cmd",
    visibility = ["//fwdcli:__subpackages__"],
    deps = [
        "//fwdcli/client",
        "//fwdcli/formatter",
        "@com_github_spf13_cobra//:cobra",
    ],
)
