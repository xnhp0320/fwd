load("@rules_go//go:def.bzl", "go_library", "go_test")

go_library(
    name = "client",
    srcs = ["client.go"],
    importpath = "fwdcli/client",
    visibility = ["//fwdcli:__subpackages__"],
)

go_test(
    name = "client_test",
    srcs = ["client_test.go"],
    embed = [":client"],
)
