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
  QueueManager::Instance().setDecodeMaxKbps(config.decode_limit_kbps());
  QueueManager::Instance().setEncodeMaxKbps(config.encode_limit_kbps());
}

AdaptConfig::AdaptConfig(const envoy::config::filter::http::adapt::v2::AdaptRateLimit& config,
                         Stats::Scope& scope, const std::string& stats_prefix,
                         TimeSource& time_source)
    : settings_(config), stats_(generateStats(stats_prefix, scope)), time_source_(time_source) {}

InstanceStats AdaptConfig::generateStats(const std::string& name, Stats::Scope& scope) {
  std::string final_prefix = fmt::format("{}adapt.", name);
  return {ALL_ADAPT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                          POOL_GAUGE_PREFIX(scope, final_prefix))};
}

Adapt::Adapt(ConfigSharedPtr config)
    : config_(config), encode_buffer_len_(0), decode_buffer_len_(0) {
  // ENVOY_LOG(critical, "Beginning adapt object creation.");
}

Adapt::~Adapt() {
  // ENVOY_LOG(critical, "Cleaning up adapt filter...");
}

// TODO: this may be a bit hacky...
// When the filter is destroyed, we know that the request has left the queue
void Adapt::onDestroy() {
#ifdef DECODE
  config_->stats().request_queue_size_.sub(decode_buffer_len_);
#endif
#ifdef ENCODE
  config_->stats().response_queue_size_.sub(encode_buffer_len_);
#endif
  ENVOY_LOG(trace, "Adapt filter onDestroy()");
}

// TODO: we probably want to buffer headers and payload...
#ifdef DECODE
Http::FilterHeadersStatus Adapt::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  decode_headers_only_ = end_stream;
  decode_headers_ = &headers;
  decode_buffer_len_ += headers.size();
  ENVOY_LOG(critical, "Stop iterating when decoding headers {}, end_stream={}", headers,
        end_stream);
  return Http::FilterHeadersStatus::StopIteration;
}
#else
Http::FilterHeadersStatus Adapt::decodeHeaders(Http::HeaderMap&, bool) {
  return Http::FilterHeadersStatus::Continue;
}
#endif

#ifdef DECODE
Http::FilterDataStatus Adapt::decodeData(Buffer::Instance& data, bool) {
  ENVOY_LOG(trace, "Writing {} bytes to buffer.", data.length());
  decode_buffer_len_ += data.length();
  return Http::FilterDataStatus::StopIterationAndBuffer;
}
#else
Http::FilterDataStatus Adapt::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}
#endif

// TODO
Http::FilterTrailersStatus Adapt::decodeTrailers(Http::HeaderMap&) {
  ENVOY_LOG(critical, "TODO: we saw decoding trailers...");
  return Http::FilterTrailersStatus::StopIteration;
}

void Adapt::decodeComplete() {
#ifdef DECODE
  ENVOY_LOG(trace, "Decoding complete, inserting {} bytes into queue", decode_buffer_len_);
  config_->stats().request_queue_size_.add(decode_buffer_len_);
  QueueManager::Instance().addDecoderToQueue(decoder_callbacks_, decode_buffer_len_,
                                             decode_headers_only_, *decode_headers_);
#endif
}

#ifdef ENCODE
Http::FilterHeadersStatus Adapt::encodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  encode_headers_only_ = end_stream;
  encode_headers_ = &headers;
  encode_buffer_len_ += headers.size();
  ENVOY_LOG(critical, "Stop iterating when encoding headers {}", headers);
  return Http::FilterHeadersStatus::StopIteration;
}
#else
Http::FilterHeadersStatus Adapt::encodeHeaders(Http::HeaderMap&, bool) {
  return Http::FilterHeadersStatus::Continue;
}
#endif

#ifdef ENCODE
Http::FilterDataStatus Adapt::encodeData(Buffer::Instance& data, bool) {
  ENVOY_LOG(critical, "Writing {} bytes to buffer in encode.", data.length());
  encode_buffer_len_ += data.length();
  return Http::FilterDataStatus::StopIterationAndBuffer;
}
#else
Http::FilterDataStatus Adapt::encodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}
#endif

// TODO
Http::FilterTrailersStatus Adapt::encodeTrailers(Http::HeaderMap&) {
  ENVOY_LOG(critical, "TODO: we saw encoding trailers...");
  return Http::FilterTrailersStatus::StopIteration;
}

void Adapt::encodeComplete() {
#ifdef ENCODE
  config_->stats().response_queue_size_.add(encode_buffer_len_);
  ENVOY_LOG(critical, "Encoding complete, inserting {} bytes into queue", encode_buffer_len_);
  QueueManager::Instance().addEncoderToQueue(encoder_callbacks_, encode_buffer_len_,
                                             encode_headers_only_, *encode_headers_);
#endif
}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
