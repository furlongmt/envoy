#pragma once

#include "envoy/config/filter/http/adapt/v2/adapt.pb.h"
#include "envoy/config/filter/http/adapt/v2/adapt.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

class AdaptFilterFactory 
    : public Common::FactoryBase<envoy::config::filter::http::adapt::v2::AdaptRateLimit> {
public:
   AdaptFilterFactory() : FactoryBase(HttpFilterNames::get().Adapt) {
   }

   Http::FilterFactoryCb createFilterFactory(const Json::Object& json_config,
                  const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

 private:
   Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
       const envoy::config::filter::http::adapt::v2::AdaptRateLimit& proto_config,
       const std::string& stats_prefix,
       Server::Configuration::FactoryContext& context) override;
  
  void translateHttpAdaptFilter(const Json::Object& json_config, 
                                envoy::config::filter::http::adapt::v2::AdaptRateLimit& proto_config);
  
   Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
       const envoy::config::filter::http::adapt::v2::AdaptRateLimit& proto_config,
       Server::Configuration::FactoryContext& context) override;
   
};

} // namespace AdaptFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy