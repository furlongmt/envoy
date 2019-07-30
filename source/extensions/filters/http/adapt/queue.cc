
#include "extensions/filters/http/adapt/queue.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

RealTimeSource Queue::time_source_;

Queue::Queue(bool encode, bool drop, bool transform)
    : drop_(drop), transform_(transform), encode_(encode),
      token_bucket_(MaxTokens, time_source_, SecondDivisor) {
  bytes_per_time_slice_ = ((max_kbps_ * 1024) / SecondDivisor);
  drop_iterator_ = queue_.begin();
}

void Queue::SetMaxKbps(uint64_t max_kbps) {
  std::lock_guard<std::mutex> lck(mtx_);
  if (max_kbps != max_kbps_) {
    max_kbps_ = max_kbps;
    bytes_per_time_slice_ = ((max_kbps_ * 1024) / SecondDivisor);
    ENVOY_LOG(critical, "Updated max kbps to {}", max_kbps_);
  }
}

void Queue::Push(RequestSharedPtr req) {
  std::lock_guard<std::mutex> lck(mtx_);
  queue_.push_back(req);
  adapt_set_.insert(req);
  bytes_in_q_ += req->size();
  ENVOY_LOG(critical, "limiter: adding request with size {}, new queue size in bytes = {}", req->size(), bytes_in_q_);
  cv_.notify_one();
}

void Queue::Pop() {
  std::lock_guard<std::mutex> lck(mtx_);
  pop();
}

void Queue::pop() {
  RequestSharedPtr req = queue_.front();
  ENVOY_LOG(trace, "limiter: popping request from queue");
  queue_.pop_front();
  bytes_in_q_ -= req->size();
  adapt_set_.erase(req);
}

std::chrono::milliseconds Queue::drain_request() {
  std::unique_lock<std::mutex> lck(mtx_);

  if (transform_ && bytes_in_q_ > BytesThreshold && !adapt_set_.empty()) {
    const absl::string_view s("\n transformation on request");
    std::function<void(Http::HeaderMap&)> header_f = [s](Http::HeaderMap& headers) {
      int str_len = s.length();
      auto length_header = headers.ContentLength();
      uint64_t prev_content_length;
      if (StringUtil::atoull(length_header->value().c_str(), prev_content_length))  {
        headers.ContentLength()->value(prev_content_length + str_len);
      }
    };
    std::function<void(Buffer::Instance&)> buf_f = [s](Buffer::Instance& buf) { buf.add(s); };
    transform(buf_f, header_f);
  }

  // TODO: don't hardcode decode bytes threshold * 100
  if (drop_ && bytes_in_q_ > BytesThreshold * 400) {
      //drop_every_nth_request(decode_q_, decode_adapt_set_, 2);
      drop_large_messages(200000);
  } 
  if (drop_ && queue_.size() > 20) {
    drop_first_n_requests(queue_.size());
  }

  // TODO: should we reset the token bucket more often? 
  // e.g. if we don't have requests for awhile, we will build up a lot of tokens
  // and attempt to send all of this data at once...
  /* if (queue_.empty()) {
    saw_data_ = false; 
  } */
  if (queue_.empty()) ASSERT(bytes_in_q_ == 0);

  // Wait while our queue is empty
  cv_.wait(lck, [&] { return !queue_.empty(); });

  ENVOY_LOG(critical, "limiter: timer wakeup: buffered bytes in queue={}", bytes_in_q_);

  if (!saw_data_) {
    token_bucket_.reset(1);
    saw_data_ = true;
  }

  uint64_t tokens_needed = 0;
  RequestSharedPtr req = queue_.front();
  uint64_t request_size = req->size();

  ENVOY_LOG(trace, "limiter: request size = {}", request_size);

  tokens_needed = (request_size + bytes_per_time_slice_ - 1) / bytes_per_time_slice_;
  const uint64_t tokens_obtained = token_bucket_.consume(tokens_needed, false);

  // TODO: I can't remember why I have this req->headers_only()
  if (tokens_obtained != 0 || req->headers_only()) {
    ASSERT(tokens_needed == tokens_obtained || req->headers_only());

    ENVOY_LOG(critical, "limiter: sending {} bytes from queue", request_size);

    if (encode_) {
      Http::StreamEncoderFilterCallbacks* cb = req->encoder_callbacks();
      ASSERT(cb != nullptr);
      queue_.front()->dispatcher().post(
          [cb] { cb->continueEncoding(); }); // allow the filter to continue passing data
    } else {
      Http::StreamDecoderFilterCallbacks* cb = req->decoder_callbacks();
      ASSERT(cb != nullptr);
      try {
        queue_.front()->dispatcher().post([cb] {
          ASSERT(cb != nullptr);
          cb->continueDecoding();
        });           // allow the filter to continue passing data
      } catch (...) { // TODO: this is dangerous
        ENVOY_LOG(error, "Exception when continuing decoding...");
      }
    }

    pop();
  } else {
    ENVOY_LOG(critical, "limiter: not enough tokens obtained, tokens needed {}", tokens_needed);
  }

  return token_bucket_.allTokensAvailable(tokens_needed);
}

void Queue::transform(std::function<void(Buffer::Instance&)> buf_func,
                      std::function<void(Http::HeaderMap&)> header_func) {

  for (RequestSharedPtr req : adapt_set_) {
    if (!req->headers_only()) {
      if (encode_) {
        Http::StreamEncoderFilterCallbacks* cb = req->encoder_callbacks();
        cb->modifyEncodingBuffer(buf_func);
        // we also need to modify headers sometimes (e.g. content-length)
        cb->modifyEncodingHeaders(header_func);
      } else {
        Http::StreamDecoderFilterCallbacks* cb = req->decoder_callbacks();
        cb->modifyDecodingBuffer(buf_func);
        cb->modifyDecodingHeaders(header_func);
      }
      req->set_adapted(true);
    }
    ENVOY_LOG(critical, "Finished queue transformation, adapted {} responses",
              adapt_set_.size());
    adapt_set_.clear(); // clear set so that we don't adapt messages twice
  }
}

std::list<RequestSharedPtr>::iterator Queue::drop(std::list<RequestSharedPtr>::iterator it) {
    RequestSharedPtr req = *it;

    // TODO: remove this, it's only for the demo so we don't drop these requests
    if(!strcmp(req->headers().Method()->value().c_str(), "GET")) {
      ENVOY_LOG(critical, "Not dropping GET request here...");
      return it++;
    }

    // TODO
    if (encode_) {
        ASSERT(false);
        return it;
    }
    Http::StreamDecoderFilterCallbacks* cb = req->decoder_callbacks();
    ASSERT(cb != nullptr);
    req->dispatcher().post([cb] {
      //cb->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::FaultInjected);
      ASSERT(cb != nullptr);
      ASSERT(cb->connection()->state() == Network::Connection::State::Open);
      cb->sendLocalReply(Http::Code::TooManyRequests, "", nullptr, absl::nullopt);
    }); 

    ENVOY_LOG(critical, "Dropping request with size {}", req->size());

    bytes_in_q_ -= req->size();

    // We need to also erase it from out adapt set
    adapt_set_.erase(req);

    return queue_.erase(it);
}

// Note: must be holding lock here
// TODO: this function doesn't currently work...
void Queue::drop_every_nth_request(uint64_t n) {
  ASSERT(false);
  ENVOY_LOG(critical, "Dropping every {}th request from our queue with {} requests", n,
            queue_.size());
  uint64_t dropped_msgs = 0;
  //auto it = decode_drop_iterator_;
  while (drop_iterator_ != queue_.end()) {
    drop_iterator_ = drop(drop_iterator_);
    std::advance(drop_iterator_, n-1); // n-1 because we just removed an element from our list
    dropped_msgs++;
  } 
  ENVOY_LOG(critical, "Dropped {} messages", dropped_msgs);
}

void Queue::drop_first_n_requests(uint64_t n) {
  ASSERT(n <= queue_.size());
  ENVOY_LOG(critical, "Dropping first {} messages in queue of size {}", n, queue_.size());
  for (uint64_t i = 0; i < n; i++) {
    drop(queue_.begin());
  }
}

// Note: must be holding lock here
// TODO: maybe we should just drop messages until our queue size is small enough?
void Queue::drop_large_messages(uint64_t size) {
  ENVOY_LOG(critical,
            "Dropping requests with size > {} from queue with {} requests, bytes in queue = {}",
            size, queue_.size(), bytes_in_q_);

  uint64_t dropped_msgs = 0;
  auto it = queue_.begin();
  while (it != queue_.end()) {
    ASSERT(*it != nullptr);
    if ((*it)->size() >= size) {
      it = drop(it);
      dropped_msgs++;
    } else {
      it++;
    }
  }

  ENVOY_LOG(critical,
            "We now have {} messages in our queue after dropping {} requests, bytes in queue = {}",
            queue_.size(), dropped_msgs, bytes_in_q_);
}
} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy