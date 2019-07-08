#pragma once


#include "envoy/config/filter/http/adapt/v2/adapt.pb.validate.h"
#include "envoy/config/filter/http/adapt/v2/adapt.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "extensions/filters/http/fault/fault_filter.h" // StreamRateLimiter

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include <queue>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

/*
 * All adapt stats. @see stats_macros.h
 */
// TODO: Accumulate may not be the right option, need to look into hot restart
#define ALL_ADAPT_STATS(COUNTER, GAUGE)               \
  GAUGE(response_queue_size)                       \
  GAUGE(request_queue_size)

/*
 * Struct defintion for all adapt stats. @see stats_macros.h
 */
struct InstanceStats {
  ALL_ADAPT_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/*
 * Configuration for adaptation filter.
 */
class AdaptSettings : public Router::RouteSpecificFilterConfig {
public:
  AdaptSettings(const envoy::config::filter::http::adapt::v2::AdaptRateLimit& limit);
  uint32_t get_limit_kbps() const { return limit_kbps; }
private:
  uint32_t limit_kbps;
};

class AdaptConfig {
public:
  AdaptConfig(const envoy::config::filter::http::adapt::v2::AdaptRateLimit& config,
    Stats::Scope& scope, const std::string& stats_prefix, TimeSource& time_source);
  const InstanceStats& stats() { return stats_; }
  const AdaptSettings* settings() { return &settings_; }
  TimeSource& timeSource() { return time_source_; }

private:
  static InstanceStats generateStats(const std::string& name, Stats::Scope &scope);

  const AdaptSettings settings_;
  const InstanceStats stats_;
  TimeSource& time_source_;
};

using ConfigSharedPtr = std::shared_ptr<AdaptConfig>;

/**
 * Implementation of a adaptation filter (both upstream and downstream traffic).
 */
class Adapt : public Http::StreamFilter,
              public Logger::Loggable<Logger::Id::filter> {
public:
  Adapt(ConfigSharedPtr config);
  ~Adapt();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool ) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }
  //void encodeComplete() override;

  void printStuff(uint64_t bytes);

private:
  ConfigSharedPtr config_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  std::unique_ptr<Fault::StreamRateLimiter> response_limiter_;
  std::unique_ptr<Fault::StreamRateLimiter> request_limiter_; 
};

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
