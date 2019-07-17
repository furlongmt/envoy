#include "extensions/filters/http/adapt/rate_limiter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/event/timer.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

constexpr uint64_t AdaptRateLimiter::SecondDivisor;
constexpr uint64_t AdaptRateLimiter::MaxTokens;
bool AdaptRateLimiter::saw_data_ = false;
std::shared_ptr<TokenBucketImpl> AdaptRateLimiter::token_bucket_ = nullptr;
uint64_t AdaptRateLimiter::bytes_in_q_ = 0;
std::mutex AdaptRateLimiter::queue_mutex_;
std::queue<RequestPtr> AdaptRateLimiter::request_q_; // global queue for all rate limiting

// TODO: start spilling buffer to disk after it's too full (as defined by max_buffered_data)
AdaptRateLimiter::AdaptRateLimiter(uint64_t max_kbps, uint64_t max_buffered_data,
                                     std::function<void(Buffer::Instance&, bool)> write_data_cb,
                                     std::function<void()> continue_cb, TimeSource& time_source,
                                     Event::Dispatcher& dispatcher)
    : // bytes_per_time_slice is KiB converted to bytes divided by the number of ticks per second.
      bytes_per_time_slice_((max_kbps * 1024) / SecondDivisor), write_data_cb_(write_data_cb),
      continue_cb_(continue_cb),
      // The token bucket is configured with a max token count of the number of ticks per second,
      // and refills at the same rate, so that we have a per second limit which refills gradually in
      // ~63ms intervals.
      // MATT changed the max number of ticks
      //token_bucket_(MaxTokens, time_source, SecondDivisor),
      token_timer_(dispatcher.createTimer([this] { onTokenTimer(); })) {
  ASSERT(bytes_per_time_slice_ > 0);
  ASSERT(max_buffered_data > 0);
  std::lock_guard<std::mutex> lck(queue_mutex_);
  if (token_bucket_ == nullptr) { // initialize our token bucket once
    token_bucket_ = std::make_shared<TokenBucketImpl>(MaxTokens, time_source, SecondDivisor);
    //token_bucket_ = std::make_shared<TokenBucketImpl>(MaxTokens, RealTimeSource(), SecondDivisor);
  }
}

void AdaptRateLimiter::onTokenTimer() {
  std::lock_guard<std::mutex> lck(queue_mutex_);
  ENVOY_LOG(critical, "limiter: timer wakeup: buffered bytes={}", bytes_in_q_);

  if (!saw_data_) {
    token_bucket_->reset(1);
    saw_data_ = true;
  }

  uint64_t tokens_needed = 0;

  // Compute the number of tokens needed (rounded up), try to obtain that many tickets, and then
  // figure out how many bytes to write given the number of tokens we actually got.
  while (!request_q_.empty()) {
    Buffer::OwnedImpl data_to_write;
    RequestPtr req = request_q_.front();
    Buffer::OwnedImpl &buf = req->buffer();
    const int buf_length = buf.length();

    ENVOY_LOG(critical, "limiter: buf length = {}", buf_length);

    tokens_needed = (buf_length + bytes_per_time_slice_ - 1) / bytes_per_time_slice_;
    const uint64_t tokens_obtained = token_bucket_->consume(tokens_needed, false);

    if (tokens_obtained == 0) {
      ENVOY_LOG(critical, "limiter: not enough tokens obtained, tokens needed {}", tokens_needed);
      break; // not enough tokens to send whole request
    }

    //uint64_t bytes_to_write = std::min(tokens_obtained * bytes_per_time_slice_, bytes_in_q_);
    //ENVOY_LOG(critical, "limiter: tokens_needed={} tokens_obtained={} to_write={}", tokens_needed,
              //tokens_obtained, bytes_to_write);
    ENVOY_LOG(critical, "limiter: tokens_needed={} tokens_obtained={}", tokens_needed, tokens_obtained);

    data_to_write.move(buf);
    assert(buf.length() == 0);

    ENVOY_LOG(critical, "limiter: popping request from queue");
    request_q_.pop();

    // Write the data out, indicating end stream if we saw end stream, there is no further data to
    // send, and there are no trailers.
    write_data_cb_(data_to_write, true); // buf.length() == 0 && !req->saw_trailers());

    // If there is no more data to send for this request and we saw trailers, we need to continue
    // iteration to release the trailers to further filters.
    if (buf.length() == 0 && req->saw_trailers()) {
      continue_cb_();
    }

    bytes_in_q_ -= buf_length;
  }

  // If the queue still contains data in it, we couldn't get enough tokens, so schedule the next
  // token available time.
  if (bytes_in_q_ > 0) {
    const std::chrono::milliseconds ms = token_bucket_->allTokensAvailable(tokens_needed);
    if (ms.count() > 0 && token_timer_) {
      ENVOY_LOG(critical, "limiter: scheduling wakeup for {}ms", ms.count());
      token_timer_->enableTimer(ms);
      ENVOY_LOG(critical, "limiter: seg fault not here");
    }
  } 
}

void AdaptRateLimiter::writeData(Buffer::Instance& incoming_buffer) {
  std::lock_guard<std::mutex> lck(queue_mutex_);
  bytes_in_q_ += incoming_buffer.length();

  ENVOY_LOG(trace, "limiter: incoming data length={} buffered bytes={}, buffered requests={}", incoming_buffer.length(),
            bytes_in_q_, request_q_.size());
  // We always assume that we have a request in the queue,
  // so if the queue is empty we need to add a request
  request_q_.push(std::make_shared<Request>(incoming_buffer));

  if (!token_timer_->enabled()) {
    // TODO(mattklein123): In an optimal world we would be able to continue iteration with the data
    // we want in the buffer, but have a way to clear end_stream in case we can't send it all.
    // The filter API does not currently support that and it will not be a trivial change to add.
    // Instead we cheat here by scheduling the token timer to run immediately after the stack is
    // unwound, at which point we can directly called encode/decodeData.
    token_timer_->enableTimer(std::chrono::milliseconds(0));
  }
}

// TODO: This feels very wrong...
Http::FilterTrailersStatus AdaptRateLimiter::onTrailers() {
  ASSERT(!request_q_.empty()); // does it make sense this could never be empty?
  RequestPtr req = request_q_.front();
  //req->set_end_stream(true);
  req->set_trailers(true);
  return req->buffer().length() > 0 ? Http::FilterTrailersStatus::StopIteration
                              : Http::FilterTrailersStatus::Continue;
}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
