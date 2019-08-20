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
  for (const envoy::config::filter::http::adapt::v2::AdaptRateLimit_DropRequests& drop_request :
       config.drop_requests()) {
    QueueManager::Instance().AddDropAdaptation(drop_request.type(), drop_request.value(),
                                               drop_request.queue_length());
  }
  for (const envoy::config::filter::http::adapt::v2::AdaptRateLimit_RedirectRequests&
           redirect_request : config.redirect_requests()) {
    QueueManager::Instance().AddRedirectAdaptation(
        redirect_request.orig_host(), redirect_request.to_ip(), redirect_request.queue_length());
  }
  decode_deadline = config.decode_deadline();
  encode_deadline = config.encode_deadline();
  ENVOY_LOG(critical, "Decode deadline: {}, Encode deadline: {}", decode_deadline, encode_deadline);
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
    : config_(config) {
      ENVOY_LOG(trace, "New adapt filter created.");
}

Adapt::~Adapt() { ENVOY_LOG(trace, "Cleaning up adapt filter."); }

// TODO: this may be a bit hacky...
// When the filter is destroyed, we know that the request has left the queue
void Adapt::onDestroy() {

#ifdef DECODE
  if (request_ != nullptr) {
    ASSERT(QueueManager::Instance().MessageInDecoderQueue(request_) == false);
    if (request_->size_ > 0) {
      config_->stats().request_queue_size_.dec();
    }
    config_->stats().bytes_in_request_queue_.sub(request_->size_);
    // Check to see if we made our deadline
    std::chrono::duration<double, std::milli> decode_time_span =
        request_->exited_tp_ - request_->entered_tp_;
    ENVOY_LOG(critical, "Request was in queue for {}ms", decode_time_span.count());
    if (!request_->dropped_ &&
        decode_time_span.count() <= config_->settings()->get_decode_deadline()) {
      config_->stats().request_bytes_made_dl_.add(request_->size_);
    }
    if (!request_->dropped_) { // If the request wasn't dropped, than include this message in our
                               // bytes sent
      config_->stats().request_total_bytes_sent_.add(request_->size_);
    }
    if (request_->dropped_) {
      config_->stats().requests_dropped_.inc();
    }
  }
#endif
#ifdef ENCODE
  if (response_ != nullptr) {
    ASSERT(QueueManager::Instance().MessageInEncoderQueue(response_) == false);
    if (response_->size_ > 0) {
      config_->stats().response_queue_size_.dec();
    }
    config_->stats().bytes_in_response_queue_.sub(response_->size_);
    // Check to see if we made our deadline
    std::chrono::duration<double, std::milli> encode_time_span =
        response_->exited_tp_ - response_->entered_tp_;
    ENVOY_LOG(critical, "Response was in queue for {}ms, deadline is {}ms",
              encode_time_span.count(), config_->settings()->get_encode_deadline());
    if (!response_->dropped_ &&
        encode_time_span.count() <= config_->settings()->get_encode_deadline()) {
      config_->stats().response_bytes_made_dl_.add(response_->size_);
    }
    if (!response_->dropped_) { // If the request wasn't dropped, than include this message in our
                                // bytes sent
      config_->stats().response_total_bytes_sent_.add(response_->size_);
    }
    if (response_->dropped_) {
      config_->stats().responses_dropped_.inc();
    }
  }
#endif
  ENVOY_LOG(critical, "Adapt filter onDestroy()");
} // namespace AdaptFilter

#ifdef DECODE
Http::FilterHeadersStatus Adapt::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  request_ = std::make_shared<Message>(decoder_callbacks_, headers);
  request_->headers_only_ = end_stream;
  request_->size_ += headers.size();

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
  request_->size_ += data.length();
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
  ENVOY_LOG(trace, "Decoding complete, inserting {} bytes into queue", request_->size_);
  config_->stats().request_queue_size_.inc();
  config_->stats().bytes_in_request_queue_.add(request_->size_);
  config_->stats().request_input_bytes_.add(request_->size_);
  request_->entered_tp_ = std::chrono::system_clock::now();
  QueueManager::Instance().AddDecoderToQueue(request_);
#endif
}

#ifdef ENCODE
Http::FilterHeadersStatus Adapt::encodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  response_ = std::make_shared<Message>(encoder_callbacks_, headers);
  response_->headers_only_ = end_stream;
  response_->size_ += headers.size();
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
  ENVOY_LOG(trace, "Writing {} bytes to buffer in encode.", data.length());
  response_->size_ += data.length();
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
  config_->stats().bytes_in_response_queue_.add(response_->size_);
  config_->stats().response_input_bytes_.add(response_->size_);
  response_->entered_tp_ = std::chrono::system_clock::now();
  ENVOY_LOG(critical, "Encoding complete, inserting {} bytes into queue", response_->size_);
  QueueManager::Instance().AddEncoderToQueue(response_);
#endif
}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
