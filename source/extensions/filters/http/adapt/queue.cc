
#include "extensions/filters/http/adapt/queue.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

/*
 * These constants define the possible types of adapation that a user
 * can define in their config file 
 */
const std::string Queue::FROM_FRONT = "front";
const std::string Queue::EVERY_NTH = "every_nth";
const std::string Queue::LARGER_THAN = "larger_than";
const std::string Queue::USE_EDGE = "use_edge";

/** 
 * Our token bucket uses actual (real) time to determine when to drain 
 * requests from the queue
 */
RealTimeSource Queue::time_source_;

Queue::Queue(bool encode, bool transform)
    : transform_(transform), encode_(encode),
      token_bucket_(MaxTokens, time_source_, SecondDivisor) {
  bytes_per_time_slice_ = ((max_kbps_ * 1024) / SecondDivisor);
}

void Queue::set_max_kbps(uint64_t max_kbps) {
  std::lock_guard<std::mutex> lck(mtx_);
  if (max_kbps != max_kbps_) {
    max_kbps_ = max_kbps;
    bytes_per_time_slice_ = ((max_kbps_ * 1024) / SecondDivisor);
  }
}

void Queue::AddDropStrategy(std::string type, uint64_t n, uint64_t queue_length) {
  if (type == USE_EDGE) // DEMO: drop this after demo
    cloud_threshold_ = n;

  DropperConfigSharedPtr dropper = std::make_shared<DropperConfig>(n, queue_length);
  droppers_[type] = dropper;
}

void Queue::Push(MessageSharedPtr req) {
  std::lock_guard<std::mutex> lck(mtx_);
  queue_.push_back(req);
  transform_set_.insert(req);
  bytes_in_q_ += req->size();
  ENVOY_LOG(trace, "limiter: adding request with size {}, new queue size in bytes = {}", req->size(), bytes_in_q_);
  if (!encode_) // TODO: just for demo
    std::cout << "Queue size: " << queue_.size() << std::endl;
  // Potentially adapt queue when we add a new request
  adapt_queue();
  cv_.notify_one();
}

void Queue::Pop() {
  std::lock_guard<std::mutex> lck(mtx_);
  pop();
}

void Queue::pop() {
  MessageSharedPtr req = queue_.front();
  ENVOY_LOG(trace, "limiter: popping request from queue");
  queue_.pop_front();
  bytes_in_q_ -= req->size();
  transform_set_.erase(req);
}

std::chrono::milliseconds Queue::DrainRequest() {
  std::unique_lock<std::mutex> lck(mtx_);

  // A sanity check that if the queue is empty than we have 0 bytes in the queue
  if (queue_.empty()) {
    ASSERT(bytes_in_q_ == 0);
    // TODO: should we reset the token bucket more often?
    // e.g. if we don't have requests for awhile, we will build up a lot of tokens
    // and attempt to send all of this data at once...
    // saw_data_ = false;
  }

  // Wait while our queue is empty
  cv_.wait(lck, [&] { return !queue_.empty(); });

  ENVOY_LOG(critical, "limiter: timer wakeup: buffered bytes in queue={}", bytes_in_q_);

  // DEMO: remove after demo
  if (!encode_ && max_kbps_ < cloud_threshold_) {
    ENVOY_LOG(critical, "limiter: no wait, sending all messages");
    while (!queue_.empty()) {
      MessageSharedPtr req = queue_.front();
      Http::StreamDecoderFilterCallbacks* cb = req->decoder_callbacks();
      ASSERT(cb != nullptr);
      req->dispatcher().post([cb] {
          ASSERT(cb != nullptr);
          cb->continueDecoding();
      });
      pop();
    }

    return std::chrono::milliseconds(0);
  }

  // The first time we see data we should reset the token bucket to have just a single token
  if (!saw_data_) {
    token_bucket_.reset(1);
    saw_data_ = true;
  }

  uint64_t tokens_needed = 0;
  MessageSharedPtr req = queue_.front();
  uint64_t request_size = req->size();

  ENVOY_LOG(trace, "limiter: request size = {}", request_size);

  tokens_needed = (request_size + bytes_per_time_slice_ - 1) / bytes_per_time_slice_;
  const uint64_t tokens_obtained = token_bucket_.consume(tokens_needed, false);

  // TODO: I can't remember why I have this req->headers_only()
  if (tokens_obtained != 0 || req->headers_only()) {
    // Sanity check that we have obtained the number of tokens that we need
    ASSERT(tokens_needed == tokens_obtained || req->headers_only());

    ENVOY_LOG(critical, "limiter: sending {} bytes from queue", request_size);

    /**
     * To allow the filter to send it's data, we must post on the dispatcher's event loop
     * so that the original worker thread is making the callback to 
     * continueEncoding()/continueDecoding(). This a strange hack to get around Envoy's
     * attempt to handle a connection entirely on on thread, but it seems to work well enough. 
     */
    if (encode_) {
      Http::StreamEncoderFilterCallbacks* cb = req->encoder_callbacks();
      // A sanity check that this request is for an encoded response
      ASSERT(cb != nullptr);
      req->dispatcher().post(
          [cb] { cb->continueEncoding(); }); 
    } else {
      Http::StreamDecoderFilterCallbacks* cb = req->decoder_callbacks();
      // A sanity check that this request is for a decoded request
      ASSERT(cb != nullptr);
      try {
        req->dispatcher().post([cb] {
          // DEMO: remove below here after demo
          // Modify all requests to redirect them to our cloud service
          /*std::function<void(Http::HeaderMap&)> head_f = [](Http::HeaderMap& headers) {
            if (headers.Host()) {
              //headers.insertEnvoyOriginalUrl().value(
                  //absl::StrCat("http://", headers.Host()->value().getStringView(),
                               //headers.Path()->value().getStringView()));
              headers.insertHost().value(std::string("10.170.69.147:5001"));
              headers.insertEnvoyDecoratorOperation().value(std::string("10.170.69.147:5001"));
              ENVOY_LOG(critical, "Changed host headers to {}", headers);
            }
          };
          cb->modifyDecodingHeaders(head_f);*/
          cb->continueDecoding();
        });           // allow the filter to continue passing data
      } catch (...) { // TODO: this is dangerous
        ENVOY_LOG(error, "Exception when continuing decoding...");
      }
    }

    pop(); // Remove the request that we just sent

    // We need to reset the tokens needed to the tokens needed for our next request in the queue
    if (!queue_.empty()) {
      tokens_needed = (queue_.front()->size() + bytes_per_time_slice_ - 1) / bytes_per_time_slice_;
    } else {
      // Since our queue is empty, returning 0 should cause us to immediately re-enter this function
      // but we'll wait on our cv until something new is added to the queue
      tokens_needed = 0; 
    }
  } else {
    ENVOY_LOG(critical, "limiter: not enough tokens obtained, tokens needed {}", tokens_needed);
  }

  // Return the amount of time that we must sleep until enough tokens are available to send the next request
  return token_bucket_.allTokensAvailable(tokens_needed);
}

void Queue::adapt_queue() {
  /**
   * This is just one simple transformation that I did for testing purposes
   */
  if (transform_ && !transform_set_.empty()) {
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

  /**
   * Iterate through all of our drop configurations and apply drop strategy if applicable
   */
  for (auto dropper : droppers_) {
    if (queue_.size() >= dropper.second->threshold && dropper.second->threshold > 0) {
      // first is the type here
      drop_based_on_type(dropper.first, dropper.second->value);
    } else if (dropper.first == USE_EDGE) { // TODO: remove after demo
      drop_based_on_type(dropper.first, dropper.second->value);
    }
  }
}

void Queue::drop_messages_to_cloud(uint64_t n) {
  if (max_kbps_ > n) {
    drop_messages_to_url(absl::string_view("edge-object-detector.default.svc.cluster.local:5002"));
  } else {
    drop_messages_to_url(absl::string_view("cloud-object-detector.default.svc.cluster.local:5001"));
  }
}

void Queue::drop_based_on_type(std::string type, uint64_t n) {
  // Unfortunately we can't easily switch on strings so we have this if/else chain
  if (type == FROM_FRONT) {
    drop_first_n_requests(n);
  } else if (type == EVERY_NTH){ 
    drop_every_nth_request(n);
  } else if (type == LARGER_THAN) {
    drop_large_messages(n);
  } else if (type == USE_EDGE){ // DEMO: really just for demo
    drop_messages_to_cloud(n);
   } else {
    ENVOY_LOG(critical, "Type {} not recognized!", type);
  }
}

void Queue::transform(std::function<void(Buffer::Instance&)> buf_func,
                      std::function<void(Http::HeaderMap&)> header_func) {

  for (MessageSharedPtr req : transform_set_) {
    if (!req->adapted()) { // We don't want to transform the same request twice (this is somewhat redundant since we're using the transform_set)
      // We can modify both the payload and the headers (e.g. content-length) for each request
      if (encode_) {
        Http::StreamEncoderFilterCallbacks* cb = req->encoder_callbacks();
        cb->modifyEncodingBuffer(buf_func);
        cb->modifyEncodingHeaders(header_func);
      } else {
        Http::StreamDecoderFilterCallbacks* cb = req->decoder_callbacks();
        cb->modifyDecodingBuffer(buf_func);
        cb->modifyDecodingHeaders(header_func);
      }
      req->set_adapted(true);
    }
  }
  ENVOY_LOG(critical, "Finished queue transformation, adapted {} responses", transform_set_.size());
  transform_set_.clear(); // Clear set so that we don't adapt messages twice
}

std::list<MessageSharedPtr>::iterator Queue::drop(std::list<MessageSharedPtr>::iterator it) {
    MessageSharedPtr req = *it;

    // TODO: DEMO: we wnat to drop both ways
    if (encode_) {
        ASSERT(false);
        return it;
    } 
    /*
     * TODO: there's currently no sendLocalReply option for responses, so we should add this
     * and then enable this and add an else for decoded messages and remove the above 
    if (encode_) {
      Http::StreamEncoderFilterCallbacks* cb = req->encoder_callbacks();
      ASSERT(cb != nullptr);
      req->dispatcher().post([cb] {
        // cb->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::FaultInjected);
        ASSERT(cb != nullptr);
        ASSERT(cb->connection()->state() == Network::Connection::State::Open);
        cb->sendLocalReply(Http::Code::TooManyRequests, "", nullptr, absl::nullopt);
      });
    }*/
    Http::StreamDecoderFilterCallbacks* cb = req->decoder_callbacks();
    ASSERT(cb != nullptr);
    req->dispatcher().post([cb] {
      //cb->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::FaultInjected);
      ASSERT(cb != nullptr);
      ASSERT(cb->connection()->state() == Network::Connection::State::Open);
      cb->sendLocalReply(Http::Code::TooManyRequests, "", nullptr, absl::nullopt);
    }); 

    ENVOY_LOG(critical, "Dropping request of size {}", req->size());

    bytes_in_q_ -= req->size();

    // We need to also erase it from out adapt set
    transform_set_.erase(req);


    // Set this message to dropped
    req->set_dropped(true);

    return queue_.erase(it);
}

// Note: must be holding lock here
void Queue::drop_every_nth_request(uint64_t n) {
  ENVOY_LOG(critical, "Dropping every {}th request from our queue with {} requests", n,
            queue_.size());
  uint64_t dropped_msgs = 0;
  auto it = queue_.begin();
  while (it != queue_.end()) {
    it = drop(it);
    std::advance(it, n-1); // n-1 because we just removed an element from our list
    dropped_msgs++;
  } 
  ENVOY_LOG(critical, "Dropped {} messages", dropped_msgs);
}

void Queue::drop_first_n_requests(uint64_t n) {

  ENVOY_LOG(critical, "Dropping first {} messages in queue of size {}", n, queue_.size());
  for (uint64_t i = 0; i < n && i < queue_.size(); i++) {
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

void Queue::drop_messages_to_url(absl::string_view url) {
  ENVOY_LOG(critical, "Dropping all requests to {}", url);
  auto it = queue_.begin();
  while (it != queue_.end()) {
    ASSERT(*it != nullptr);
    const Http::HeaderMap& headers = (*it)->headers();
    if (headers.Host() && headers.Host()->value().getStringView() == url) {
      it = drop(it);
    } else {
      it++;
    }
  }

}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy