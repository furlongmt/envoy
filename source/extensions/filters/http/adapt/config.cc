
#include <memory>
#include <string>

#include "extensions/filters/http/adapt/adapt_filter.h"
#include "extensions/filters/http/adapt/config.h"

#include "envoy/server/filter_config.h"
#include "common/config/filter_json.h"
#include "common/config/json_utility.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

Http::FilterFactoryCb AdaptFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::adapt::v2::AdaptRateLimit& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr filter_config(
      new AdaptConfig(config, context.scope(), context.runtime(), stats_prefix, context.timeSource()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Adapt>(filter_config));
  };
}

void AdaptFilterFactory::translateHttpAdaptFilter(
    const Json::Object& json_config,
    envoy::config::filter::http::adapt::v2::AdaptRateLimit& proto_config) {
  if ((json_config).hasObject("limit_kbps")) {
    (proto_config).set_limit_kbps(json_config.getInteger("limit_kbps"));
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

/*  Router::RouteSpecificFilterConfigConstSharedPtr
AdaptFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::config::filter::http::adapt::v2::AdaptRateLimit& config,
    Server::Configuration::FactoryContext&) {
  return std::make_shared<const Fault::FaultSettings>(config);
}
*/

    /**
     * Static registration for this sample filter. @see RegisterFactory.
     */
    REGISTER_FACTORY(AdaptFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
