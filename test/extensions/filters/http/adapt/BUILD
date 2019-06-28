
licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "adapt_integration_test",
    srcs = ["adapt_integration_test.cc"],
    extension_name = "envoy.filters.http.adapt",
    deps = [
        "//source/extensions/filters/http/adapt:adapt_filter_lib",
        "//test/integration:http_protocol_integration_lib",
    ],
    args = ["-l critical"],
)

