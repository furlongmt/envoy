#include "extensions/filters/http/adapt/adapt_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"

#define DECODE
#define ENCODE

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

AdaptSettings::AdaptSettings(const envoy::config::filter::http::adapt::v2::AdaptRateLimit& config) {
  limit_kbps = config.limit_kbps();
}

AdaptConfig::AdaptConfig(const envoy::config::filter::http::adapt::v2::AdaptRateLimit& config,
                         Stats::Scope& scope, 
                         const std::string& stats_prefix, TimeSource& time_source)
    : settings_(config), stats_(generateStats(stats_prefix, scope)), 
      time_source_(time_source) {}

InstanceStats AdaptConfig::generateStats(const std::string& name, Stats::Scope& scope) {
  std::string final_prefix = fmt::format("{}adapt.", name);
  //std::string final_prefix = name;
  std::cout << "Stats prefix is " << final_prefix << std::endl;
  return {ALL_ADAPT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                          POOL_GAUGE_PREFIX(scope, final_prefix))};
}

Adapt::Adapt(ConfigSharedPtr config) : config_(config) {
  ENVOY_LOG(critical, "Beginning adapt object creation.");
  encode_buffer_len_ = 0;
  decode_buffer_len_ = 0;
}

Adapt::~Adapt() {
  ENVOY_LOG(critical, "Cleaning up adapt filter...");
}

void Adapt::onDestroy() {
}

Http::FilterHeadersStatus Adapt::decodeHeaders(Http::HeaderMap&, bool) {
#ifdef DECODE 
  return Http::FilterHeadersStatus::StopIteration;
#else
  return Http::FilterHeadersStatus::Continue;
#endif
}

Http::FilterDataStatus Adapt::decodeData(Buffer::Instance& data, bool) {
#ifdef DECODE
  ENVOY_LOG(critical, "Writing {} bytes to buffer.", data.length());
  decode_buffer_len_ += data.length();
  return Http::FilterDataStatus::StopIterationAndBuffer;
#else
  ENVOY_LOG(critical, "Continuing in decode data...", data.length());
  return Http::FilterDataStatus::Continue;
#endif
}

// TODO
Http::FilterTrailersStatus Adapt::decodeTrailers(Http::HeaderMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void Adapt::decodeComplete() {
#ifdef DECODE
  ENVOY_LOG(critical, "Decoding complete, inserting {} bytes into queue", decode_buffer_len_); 
  QueueManager::Instance().addDecoderToQueue(decoder_callbacks_, decode_buffer_len_);
#endif
}

Http::FilterHeadersStatus Adapt::encodeHeaders(Http::HeaderMap& headers, bool) {
#ifdef ENCODE
  ENVOY_LOG(critical, "Stop iterating when encoding headers {}", headers);
  return Http::FilterHeadersStatus::StopIteration;
#else
  return Http::FilterHeadersStatus::Continue;
#endif
}

Http::FilterDataStatus Adapt::encodeData(Buffer::Instance& data, bool) {
#ifdef ENCODE
  ENVOY_LOG(critical, "Writing {} bytes to buffer in encode.", data.length());
  encode_buffer_len_ += data.length();
  return Http::FilterDataStatus::StopIterationAndBuffer;
#else
  ENVOY_LOG(critical, "Continuing in encode data...", data.length());
  return Http::FilterDataStatus::Continue;
#endif
}

// TODO
Http::FilterTrailersStatus Adapt::encodeTrailers(Http::HeaderMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void Adapt::encodeComplete() {
#ifdef ENCODE
  ENVOY_LOG(critical, "Encoding complete, inserting {} bytes into queue", encode_buffer_len_); 
  QueueManager::Instance().addEncoderToQueue(encoder_callbacks_, encode_buffer_len_);
#endif
}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
