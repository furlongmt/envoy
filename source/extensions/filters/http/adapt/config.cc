
#include <memory>
#include <string>

#include "extensions/filters/http/adapt/adapt_filter.h"
#include "extensions/filters/http/adapt/config.h"

#include "envoy/server/filter_config.h"
#include "common/config/filter_json.h"
#include "common/config/json_utility.h"
#include "envoy/registry/registry.h"

#define JSON_NON_MUTABLE_SET_INTEGER(json, message, field_name)  \
do {                                                      \
  if ((json).hasObject(#field_name)) { \
    (message).set_##field_name((json).getInteger(#field_name)); \
  } \
} while (0)

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

Http::FilterFactoryCb AdaptFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::adapt::v2::AdaptRateLimit& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr filter_config(
      new AdaptConfig(config, context.scope(), stats_prefix, context.timeSource()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Adapt>(filter_config));
  };
}

void AdaptFilterFactory::translateHttpAdaptFilter(
    const Json::Object& json_config,
    envoy::config::filter::http::adapt::v2::AdaptRateLimit& proto_config) {

  JSON_NON_MUTABLE_SET_INTEGER(json_config, proto_config, encode_limit_kbps);
  JSON_NON_MUTABLE_SET_INTEGER(json_config, proto_config, decode_limit_kbps);
  JSON_NON_MUTABLE_SET_INTEGER(json_config, proto_config, decode_deadline);
  JSON_NON_MUTABLE_SET_INTEGER(json_config, proto_config, encode_deadline);

  for (const Json::ObjectSharedPtr& drop_entry : json_config.getObjectArray("drop_requests")) {
    envoy::config::filter::http::adapt::v2::AdaptRateLimit::DropRequests* drop_request =
        proto_config.mutable_drop_requests()->Add();
    JSON_UTIL_SET_STRING(*drop_entry, *drop_request, type);
    JSON_NON_MUTABLE_SET_INTEGER(*drop_entry, *drop_request, value);
    JSON_NON_MUTABLE_SET_INTEGER(*drop_entry, *drop_request, queue_length);
  }

  for (const Json::ObjectSharedPtr& redirect_entry : json_config.getObjectArray("redirect_requests")) {
    envoy::config::filter::http::adapt::v2::AdaptRateLimit::RedirectRequests* redirect_request =
        proto_config.mutable_redirect_requests()->Add();
    JSON_UTIL_SET_STRING(*redirect_entry, *redirect_request, orig_host);
    JSON_UTIL_SET_STRING(*redirect_entry, *redirect_request, to_ip);
    JSON_NON_MUTABLE_SET_INTEGER(*redirect_entry, *redirect_request, queue_length);
  }
}

Http::FilterFactoryCb
AdaptFilterFactory::createFilterFactory(const Json::Object& json_config,
                                        const std::string& stats_prefix,
                                        Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::adapt::v2::AdaptRateLimit proto_config;
  translateHttpAdaptFilter(json_config, proto_config);
  return createFilterFactoryFromProtoTyped(proto_config, stats_prefix, context);
}

Router::RouteSpecificFilterConfigConstSharedPtr
AdaptFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::config::filter::http::adapt::v2::AdaptRateLimit& config,
    Server::Configuration::FactoryContext&) {
  return std::make_shared<const AdaptSettings>(config);
}

/**
 * Static registration for this sample filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AdaptFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
