#pragma once


#include "envoy/config/filter/http/adapt/v2/adapt.pb.validate.h"
#include "envoy/config/filter/http/adapt/v2/adapt.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

//#include "extensions/filters/http/fault/fault_filter.h" // StreamRateLimiter
#include "extensions/filters/http/adapt/queue_manager.h"

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
#define ALL_ADAPT_STATS(COUNTER, GAUGE)            \
  GAUGE(response_queue_size)                       \
  GAUGE(request_queue_size)                        \
  COUNTER(request_bytes_made_dl)                   \
  COUNTER(response_bytes_made_dl)                  \
  GAUGE(bytes_in_request_queue)                    \
  GAUGE(bytes_in_response_queue)                   \            
  COUNTER(request_total_bytes_sent)                \
  COUNTER(response_total_bytes_sent)

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
  uint64_t get_encode_deadline() const { return encode_deadline; }
  uint64_t get_decode_deadline() const { return decode_deadline; }
private:
  uint32_t limit_kbps;
  uint64_t encode_deadline;
  uint64_t decode_deadline;
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
 * Implementation of an adaptation filter (both upstream and downstream traffic).
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
    decoder_callbacks_->setDecoderBufferLimit(0); // Setting buffer limit to 0 means there's no limit
  }
  void decodeComplete() override;

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
    encoder_callbacks_->setEncoderBufferLimit(0); // Setting buffer limit to 0 means there's no limit
  }
  void encodeComplete() override;

private:

  ConfigSharedPtr config_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  /**
   * *_buffer_len_ represents the total size of the request, both headers and data.
   */
  uint64_t encode_buffer_len_;
  uint64_t decode_buffer_len_; 
  /**
   * *_headers_only_ represents whether the request has any payload or is just headers.
   */
  bool encode_headers_only_;
  bool decode_headers_only_;
  /**
   * *_headers_ are the headers of the respective requests
   */
  Http::HeaderMap* encode_headers_;
  Http::HeaderMap* decode_headers_;
  /**
   * *_entered_tp_ are the timestamps of when the message entered the queue
   */
  std::chrono::system_clock::time_point decode_entered_tp_;
  std::chrono::system_clock::time_point encode_entered_tp_;

  /**
   * *_dropped_ represents whether our request was dropped from the queue
   */ 
  bool decode_dropped_{};
  bool encode_dropped_{};
};


} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
