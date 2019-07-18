#pragma once

#include "common/buffer/buffer_impl.h"
#include "common/common/token_bucket_impl.h"
#include "envoy/http/filter.h"

#include <mutex>
#include <queue>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

class Request {
public:
    Request(Event::Dispatcher& dispatcher, 
        Http::StreamEncoderFilterCallbacks *encoder_callbacks, uint64_t size) : 
            dispatcher_(dispatcher), encoder_callbacks_(encoder_callbacks), size_(size){}
    
    Request(Event::Dispatcher& dispatcher, 
        Http::StreamDecoderFilterCallbacks *decoder_callbacks, uint64_t size) : 
            dispatcher_(dispatcher), decoder_callbacks_(decoder_callbacks), size_(size){}

    Event::Dispatcher& dispatcher() { return dispatcher_; };
    Http::StreamEncoderFilterCallbacks* encoder_callbacks() { return encoder_callbacks_; }
    Http::StreamDecoderFilterCallbacks* decoder_callbacks() { return decoder_callbacks_; }
    uint64_t size() { return size_; }

private:
    Event::Dispatcher& dispatcher_;
    Http::StreamEncoderFilterCallbacks* encoder_callbacks_;
    Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
    uint64_t size_;
};

using RequestSharedPtr = std::shared_ptr<Request>;

class QueueManager : Logger::Loggable<Logger::Id::filter> {
public:
  static QueueManager& Instance() {
    static QueueManager instance;
    return instance;
  }

  void addEncoderToQueue(Http::StreamEncoderFilterCallbacks *callbacks, uint64_t size);
  void addDecoderToQueue(Http::StreamDecoderFilterCallbacks *callbacks, uint64_t size);

protected:
  QueueManager();
  ~QueueManager() {};

private:

  std::chrono::milliseconds check_encode_queue_for_removal();
  std::chrono::milliseconds check_decode_queue_for_removal();
  void transform_encoder_queue(std::function<void(Buffer::Instance&)> buf_func,
                               std::function<void(Http::HeaderMap&)> header_func);
  void transform_decoder_queue(std::function<void(Buffer::Instance&)> buf_func,
                               std::function<void(Http::HeaderMap&)> header_func);

  // We currently divide each second into 16 segments for the token bucket. Thus, the rate limit is
  // KiB per second, divided into 16 segments, ~63ms apart. 16 is used because it divides into 1024
  // evenly.
  const uint64_t SecondDivisor = 16;
  const uint64_t MaxTokens = 10000; // this number is completely random

  const uint64_t DecodeBytesThreshold = 1000;
  const uint64_t EncodeBytesThreshold = 1000;
  bool transform_encoded_q_{};
  bool transform_decoded_q_{};

  RealTimeSource time_source_;
  uint64_t bytes_per_time_slice_;
  std::mutex mtx_;

  uint64_t max_kbps_ = 1; // TODO: create configurable max_kbps and different kbps for encode/decode
  bool decode_saw_data_{};
  bool encode_saw_data_{};
  uint64_t bytes_in_encode_q_;
  uint64_t bytes_in_decode_q_;
  TokenBucketImpl encode_token_bucket_;
  TokenBucketImpl decode_token_bucket_;
  std::list<RequestSharedPtr> decode_q_;
  std::list<RequestSharedPtr> encode_q_; 
};

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy