load("@rules_go//go:def.bzl", "go_library", "go_test")

go_library(
    name = "formatter",
    srcs = ["formatter.go"],
    importpath = "fwdcli/formatter",
    visibility = ["//fwdcli:__subpackages__"],
)

go_test(
    name = "formatter_test",
    srcs = ["formatter_test.go"],
    embed = [":formatter"],
)
