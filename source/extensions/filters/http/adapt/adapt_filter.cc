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
  QueueManager::Instance().SetDecodeMaxKbps(config.decode_limit_kbps());
  QueueManager::Instance().SetEncodeMaxKbps(config.encode_limit_kbps());
  for (const envoy::config::filter::http::adapt::v2::AdaptRateLimit_DropRequests& drop_request : config.drop_requests()) {
    QueueManager::Instance().AddDropAdaptation(drop_request.type(), drop_request.value(),
                                               drop_request.queue_length());
  }
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
      // TODO: change deadline to config
      deadline_ = 100;
      ENVOY_LOG(trace, "New adapt filter created.");
}

Adapt::~Adapt() { ENVOY_LOG(trace, "Cleaning up adapt filter."); }

// TODO: this may be a bit hacky...
// When the filter is destroyed, we know that the request has left the queue
void Adapt::onDestroy() {
#ifdef DECODE
  if (decode_buffer_len_ > 0) config_->stats().request_queue_size_.dec();
  config_->stats().bytes_in_request_queue_.sub(decode_buffer_len_);
  // Check to see if we made our deadline
  std::chrono::duration<double, std::milli> decode_time_span = decode_entered_tp_ - std::chrono::system_clock::now();
  if (decode_time_span.count() < deadline_) {
    // TODO: this is wrong in the case where we drop messages
    config_->stats().request_bytes_made_dl_.add(decode_buffer_len_);
  }
#endif
#ifdef ENCODE
  if (encode_buffer_len_ > 0) config_->stats().response_queue_size_.dec();
  config_->stats().bytes_in_response_queue_.sub(encode_buffer_len_);
  // Check to see if we made our deadline
  std::chrono::duration<double, std::milli> encode_time_span = encode_entered_tp_ - std::chrono::system_clock::now();
  if (encode_time_span.count() < deadline_) {
    // TODO: this is wrong in the case where we drop messages
    config_->stats().response_bytes_made_dl_.add(encoder_callbacks_->streamInfo().bytesSent());
  }
#endif
  ENVOY_LOG(trace, "Adapt filter onDestroy()");
}

// TODO: we probably want to buffer headers and payload...
#ifdef DECODE
Http::FilterHeadersStatus Adapt::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  decode_headers_only_ = end_stream;
  decode_headers_ = &headers;
  decode_buffer_len_ += headers.size();
  ENVOY_LOG(trace, "Stop iterating when decoding headers {}, end_stream={}", headers,
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
  config_->stats().request_queue_size_.inc();
  config_->stats().bytes_in_request_queue_.add(decode_buffer_len_);
  decode_entered_tp_ = std::chrono::system_clock::now();
  QueueManager::Instance().AddDecoderToQueue(decoder_callbacks_, decode_buffer_len_,
                                             decode_headers_only_, *decode_headers_);
#endif
}

#ifdef ENCODE
Http::FilterHeadersStatus Adapt::encodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  encode_headers_only_ = end_stream;
  encode_headers_ = &headers;
  encode_buffer_len_ += headers.size();
  ENVOY_LOG(trace, "Stop iterating when encoding headers {}", headers);
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
  config_->stats().response_queue_size_.inc();
  config_->stats().bytes_in_response_queue_.add(encode_buffer_len_);
  encode_entered_tp_ = std::chrono::system_clock::now();
  ENVOY_LOG(critical, "Encoding complete, inserting {} bytes into queue", encode_buffer_len_);
  QueueManager::Instance().AddEncoderToQueue(encoder_callbacks_, encode_buffer_len_,
                                             encode_headers_only_, *encode_headers_);
#endif
}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
