workspace(name = "envoy")

load("//bazel:api_repositories.bzl", "envoy_api_dependencies")

envoy_api_dependencies()

load("//bazel:repositories.bzl", "GO_VERSION", "envoy_dependencies")
load("//bazel:cc_configure.bzl", "cc_configure")

envoy_dependencies()

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

cc_configure()

load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")

go_rules_dependencies()

go_register_toolchains(go_version = GO_VERSION)

# matt f added boost ( put back in if we want to use lock-free queues)
#load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
#git_repository(
#    name = "com_github_nelhage_rules_boost",
#    commit = "6d6fd834281cb8f8e758dd9ad76df86304bf1869",
#    remote = "https://github.com/nelhage/rules_boost",
#)

#load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
#boost_deps()
