
licenses(["notice"])  # Apache 2


load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "adapt_rate_limiter_lib",
    srcs = ["rate_limiter.cc"],
    hdrs = ["rate_limiter.h"],
    deps = [
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_library(
    name = "adapt_filter_lib",
    srcs = ["adapt_filter.cc"],
    hdrs = ["adapt_filter.h"],
    deps = [
        "@envoy_api//envoy/config/filter/http/adapt/v2:adapt_cc",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        #"//source/extensions/filters/http/fault:fault_filter_lib",
        "//source/extensions/filters/http/adapt:adapt_rate_limiter_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/adapt:adapt_filter_lib",
        "//source/common/config:filter_json_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http:well_known_names",
    ],
)