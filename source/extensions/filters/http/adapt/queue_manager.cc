
#include "extensions/filters/http/adapt/queue_manager.h"

#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

QueueManager::QueueManager()
    : encode_token_bucket_(MaxTokens, time_source_, SecondDivisor),
      decode_token_bucket_(MaxTokens, time_source_, SecondDivisor) {
  bytes_per_time_slice_ = ((max_kbps_ * 1024) / SecondDivisor); // 1 is max_kbps here

  // Start decoder timer
  std::thread([this]() {
    while (true) {
      std::chrono::milliseconds ms = check_decode_queue_for_removal();
      std::this_thread::sleep_for(ms);
    }
  }).detach();

  // Start encoder timer
  std::thread([this]() {
    while (true) {
      std::chrono::milliseconds ms = check_encode_queue_for_removal();
      std::this_thread::sleep_for(ms);
    }
  }).detach();
}

std::chrono::milliseconds QueueManager::check_decode_queue_for_removal() {
  std::lock_guard<std::mutex> lck(mtx_);

  // TODO: set threshold and only transform queue once
  if (bytes_in_decode_q_ > DecodeBytesThreshold) {
    const absl::string_view s("\n transformation in request");
    std::function<void(Http::HeaderMap&)> header_f = [s](Http::HeaderMap& headers) {
      int str_len = s.length();
      int prev_content_length =
          std::atoi(headers.get(Http::LowerCaseString("content-length"))->value().c_str());
      headers.get(Http::LowerCaseString("content-length"))->value(prev_content_length + str_len);
    };
    std::function<void(Buffer::Instance&)> buf_f = [s](Buffer::Instance& buf) { buf.add(s); };
    transform_decoder_queue(buf_f, header_f);
  } 

  // TODO: what should this time be?
  if (decode_q_.empty()) {
    decode_saw_data_ = false; // TODO: does it make sense to reset here?
    return std::chrono::milliseconds(100);
  }

  ENVOY_LOG(critical, "limiter: timer wakeup: buffered bytes in decode_q={}", bytes_in_decode_q_);

  if (!decode_saw_data_) {
    decode_token_bucket_.reset(1);
    decode_saw_data_ = true;
  }

  Buffer::OwnedImpl data_to_write;
  uint64_t tokens_needed = 0;
  uint64_t request_size = decode_q_.front()->size();

  ENVOY_LOG(critical, "limiter: request size = {}", request_size);

  tokens_needed = (request_size + bytes_per_time_slice_ - 1) / bytes_per_time_slice_;
  const uint64_t tokens_obtained = decode_token_bucket_.consume(tokens_needed, false);

  if (tokens_obtained != 0 || request_size == 0) { // this is to handle header only requests
    ENVOY_LOG(critical, "limiter: tokens_needed={} tokens_obtained={}", tokens_needed,
              tokens_obtained);

    Http::StreamDecoderFilterCallbacks* cb = decode_q_.front()->decoder_callbacks();
    ASSERT(cb != nullptr);
    decode_q_.front()->dispatcher().post(
        [cb] { cb->continueDecoding(); }); // allow the filter to continue passing data

    ENVOY_LOG(critical, "limiter: popping filter from queue");
    decode_q_.pop_front();

    bytes_in_decode_q_ -= request_size;
  } else {
    ENVOY_LOG(critical, "limiter: not enough tokens obtained, tokens needed {}", tokens_needed);
  }

  return decode_token_bucket_.allTokensAvailable(tokens_needed);
}

std::chrono::milliseconds QueueManager::check_encode_queue_for_removal() {
  std::lock_guard<std::mutex> lck(mtx_);

  if (bytes_in_encode_q_ > EncodeBytesThreshold) {
    const absl::string_view s("\n transformation in response");
    std::function<void(Http::HeaderMap&)> header_f = [s](Http::HeaderMap& headers) {
      int str_len = s.length();
      int prev_content_length =
          std::atoi(headers.get(Http::LowerCaseString("content-length"))->value().c_str());
      headers.get(Http::LowerCaseString("content-length"))->value(prev_content_length + str_len);
    };
    std::function<void(Buffer::Instance&)> buf_f = [s](Buffer::Instance& buf) { buf.add(s); };
    transform_encoder_queue(buf_f, header_f);
  } 

  // TODO: what should this time returned be?
  if (encode_q_.empty()) {
    encode_saw_data_ = false; // TODO: does it make sense to reset here?
    return std::chrono::milliseconds(100);
  }

  ENVOY_LOG(critical, "limiter: timer wakeup: buffered bytes in encode_q={}", bytes_in_encode_q_);

  if (!encode_saw_data_) {
    encode_token_bucket_.reset(1);
    encode_saw_data_ = true;
  }

  Buffer::OwnedImpl data_to_write;
  uint64_t tokens_needed = 0;
  uint64_t request_size = encode_q_.front()->size();

  ENVOY_LOG(critical, "limiter: request size = {}", request_size);

  tokens_needed = (request_size + bytes_per_time_slice_ - 1) / bytes_per_time_slice_;
  const uint64_t tokens_obtained = encode_token_bucket_.consume(tokens_needed, false);

  if (tokens_obtained != 0) {
    ENVOY_LOG(critical, "limiter: tokens_needed={} tokens_obtained={}", tokens_needed,
              tokens_obtained);

    Http::StreamEncoderFilterCallbacks* cb = encode_q_.front()->encoder_callbacks();
    ASSERT(cb != nullptr);
    encode_q_.front()->dispatcher().post(
        [cb] { cb->continueEncoding(); }); // allow the filter to continue passing data

    ENVOY_LOG(critical, "limiter: popping request from encode queue");
    encode_q_.pop_front();

    bytes_in_encode_q_ -= request_size;
  } else {
    ENVOY_LOG(critical, "limiter: not enough tokens obtained, tokens needed {}", tokens_needed);
  }

  return encode_token_bucket_.allTokensAvailable(tokens_needed);
}

void QueueManager::addEncoderToQueue(Http::StreamEncoderFilterCallbacks* callbacks, uint64_t size) {
  std::lock_guard<std::mutex> lck(mtx_);
  RequestSharedPtr req = std::make_shared<Request>(callbacks->dispatcher(), callbacks, size);
  encode_q_.push_back(req);
  bytes_in_encode_q_ += size;
}

void QueueManager::addDecoderToQueue(Http::StreamDecoderFilterCallbacks* callbacks, uint64_t size) {
  std::lock_guard<std::mutex> lck(mtx_);
  RequestSharedPtr req = std::make_shared<Request>(callbacks->dispatcher(), callbacks, size);
  decode_q_.push_back(req);
  bytes_in_decode_q_ += size;
}

// Note: You should always be holding the queues mutex when calling this function
void QueueManager::transform_encoder_queue(std::function<void(Buffer::Instance&)> buf_func,
                                           std::function<void(Http::HeaderMap&)> header_func) {
  ENVOY_LOG(critical, "Running transformation on encoder queue!");
  for (RequestSharedPtr req : encode_q_) {
    if (!req->adapted()) {
      Http::StreamEncoderFilterCallbacks* cb = req->encoder_callbacks();
      cb->modifyEncodingBuffer(buf_func);
      // we also need to modify headers sometimes (e.g. content-length)
      cb->modifyEncodingHeaders(header_func);
      req->set_adapted(true);
    }
    /* req->dispatcher().post([cb, func] {
      cb->modifyDecodingBuffer(func);
    }); */ // allow the filter to continue passing data
  }
  ENVOY_LOG(critical, "Finished encoder queue transformation!");
}

// Note: You should always be holding the queues mutex when calling this function
void QueueManager::transform_decoder_queue(std::function<void(Buffer::Instance&)> buf_func,
                                           std::function<void(Http::HeaderMap&)> header_func) {
  ENVOY_LOG(critical, "Running transformation on decoder queue!");
  // TODO: this is way too slow.... O(n)
  for (RequestSharedPtr req : decode_q_) {
    if (!req->adapted()) {
      Http::StreamDecoderFilterCallbacks* cb = req->decoder_callbacks();
      cb->modifyDecodingBuffer(buf_func);
      cb->modifyDecodingHeaders(header_func);
      req->set_adapted(true);
    }
    /* req->dispatcher().post([cb, func] {
      cb->modifyDecodingBuffer(func);
    }); */ // allow the filter to continue passing data
  }
  ENVOY_LOG(critical, "Finished decoder queue transformation!");
}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy