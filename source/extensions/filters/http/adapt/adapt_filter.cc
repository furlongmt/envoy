#include "extensions/filters/http/adapt/adapt_filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"

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
}

Adapt::~Adapt() {
  ENVOY_LOG(critical, "Cleaning up adapt filter...");
  ASSERT(response_limiter_ == nullptr || response_limiter_->destroyed());
  ASSERT(request_limiter_ == nullptr || request_limiter_->destroyed());
}

void Adapt::onDestroy() {
  if (response_limiter_ != nullptr) {
    response_limiter_->destroy();
  }
  if(request_limiter_ != nullptr) {
    request_limiter_->destroy();
  }
}

Http::FilterHeadersStatus Adapt::decodeHeaders(Http::HeaderMap&, bool) {

  if (request_limiter_ == nullptr) {
    const uint64_t kbps_rate = config_->settings()->get_limit_kbps();
    request_limiter_ = std::make_unique<AdaptRateLimiter>(
        kbps_rate, decoder_callbacks_->decoderBufferLimit(),
        [this](Buffer::Instance& data, bool end_stream) {
          ENVOY_LOG(critical, "Request limiter injection {} bytes.", data.length());
          config_->stats().request_queue_size_.sub(data.length());
          decoder_callbacks_->injectDecodedDataToFilterChain(data, end_stream);
        },
        [this] { decoder_callbacks_->continueDecoding(); }, config_->timeSource(),
        decoder_callbacks_->dispatcher());
    ENVOY_LOG(critical, "Configured request limiter with a rate limit of {} kbps.", kbps_rate);
    ENVOY_LOG(critical, "Time source is {}", typeid(config_->timeSource()).name());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Adapt::decodeData(Buffer::Instance& data, bool end_stream) {

  ENVOY_LOG(critical, "Writing {} bytes to buffer.", data.length());
  buffer_.move(data);

  if (end_stream) {
    assert(request_limiter_ != nullptr);
    config_->stats().request_queue_size_.add(buffer_.length());
    ENVOY_LOG(critical, "Writing {} bytes to rate_limiter in decode.", buffer_.length());
    request_limiter_->writeData(buffer_);
  }
  
  return Http::FilterDataStatus::StopIterationNoBuffer;
}

// TODO
Http::FilterTrailersStatus Adapt::decodeTrailers(Http::HeaderMap&) {
  if (request_limiter_ != nullptr) {
    return request_limiter_->onTrailers();
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus Adapt::encodeHeaders(Http::HeaderMap&, bool) {
  if (response_limiter_ == nullptr) {
    const uint64_t kbps_rate = config_->settings()->get_limit_kbps();
    response_limiter_ = std::make_unique<AdaptRateLimiter>(
        kbps_rate, encoder_callbacks_->encoderBufferLimit(),
        [this](Buffer::Instance& data, bool end_stream) {
          config_->stats().response_queue_size_.sub(data.length());
          ENVOY_LOG(critical, "Response limiter injection {} bytes.", data.length());
          encoder_callbacks_->injectEncodedDataToFilterChain(data, end_stream);
        },
        [this] { encoder_callbacks_->continueEncoding(); }, config_->timeSource(),
        encoder_callbacks_->dispatcher());
    ENVOY_LOG(critical, "Configured response limiter with a rate limit of {} kbps.", kbps_rate);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Adapt::encodeData(Buffer::Instance& data, bool end_stream) {

  ENVOY_LOG(critical, "Writing {} bytes to buffer in encode.", data.length());
  buffer_.move(data);

  if (end_stream) {
    assert(response_limiter_ != nullptr);
    config_->stats().response_queue_size_.add(buffer_.length());
    ENVOY_LOG(critical, "Writing {} bytes to rate_limiter in encode.", buffer_.length());
    response_limiter_->writeData(buffer_);
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

// TODO
Http::FilterTrailersStatus Adapt::encodeTrailers(Http::HeaderMap&) {
  if (response_limiter_ != nullptr) {
    return response_limiter_->onTrailers();
  }

  return Http::FilterTrailersStatus::Continue;
}

void Adapt::printStuff(uint64_t bytes) { ENVOY_LOG(critical, "IT WORKED, bytes = {}", bytes); }

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
