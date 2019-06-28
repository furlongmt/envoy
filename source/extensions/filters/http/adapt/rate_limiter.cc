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
      token_bucket_(SecondDivisor, time_source, SecondDivisor),
      token_timer_(dispatcher.createTimer([this] { onTokenTimer(); })) {
  ASSERT(bytes_per_time_slice_ > 0);
  ASSERT(max_buffered_data > 0);
  new_request_ = true; // always start with a new request
}

void AdaptRateLimiter::onTokenTimer() {
  ENVOY_LOG(trace, "limiter: timer wakeup: buffered bytes={}, queue len={}", bytes_in_q_, request_q_.size());
  ASSERT(bytes_in_q_ >= 0);
  Buffer::OwnedImpl data_to_write;

  // Compute the number of tokens needed (rounded up), try to obtain that many tickets, and then
  // figure out how many bytes to write given the number of tokens we actually got.
  const uint64_t tokens_needed =
      (bytes_in_q_ + bytes_per_time_slice_ - 1) / bytes_per_time_slice_;
  const uint64_t tokens_obtained = token_bucket_.consume(tokens_needed, true);
  uint64_t bytes_to_write =
      std::min(tokens_obtained * bytes_per_time_slice_, bytes_in_q_);
  ENVOY_LOG(trace, "limiter: tokens_needed={} tokens_obtained={} to_write={}", tokens_needed,
            tokens_obtained, bytes_to_write);

  bytes_in_q_ -= bytes_to_write; 
  ASSERT(bytes_in_q_ >= 0); // can never be lower than 0 due to min function above

  // TODO: Need a way to identify when a request has partially been written out so we don't try to
  // transform a tainted request
  while (bytes_to_write > 0) { // move as much as data as possible (as defined by bytes_to_write) from our buffer queue
    RequestPtr req = request_q_.front();
    Buffer::OwnedImpl buf = req->buffer();
    uint64_t buf_length = buf.length();

    data_to_write.move(buf, bytes_to_write);
    if (buf.length() == 0) { // if we've finished sending this data, then remove the request from our queue
      request_q_.pop();
    }
    bytes_to_write -= buf_length;

    // Write the data out, indicating end stream if we saw end stream, there is no further data to
    // send, and there are no trailers.
    write_data_cb_(data_to_write, req->saw_end_stream() && buf.length() == 0 && !req->saw_trailers());

    // If there is no more data to send for this request and we saw trailers, we need to continue iteration to
    // release the trailers to further filters.
    if (buf.length() == 0 && req->saw_trailers()) {
      continue_cb_();
    }
  } 

  // If the queue still contains data in it, we couldn't get enough tokens, so schedule the next
  // token available time.
  if (bytes_in_q_ > 0) {
    const std::chrono::milliseconds ms = token_bucket_.nextTokenAvailable();
    if (ms.count() > 0) {
      ENVOY_LOG(trace, "limiter: scheduling wakeup for {}ms", ms.count());
      token_timer_->enableTimer(ms);
    }
  }
}

void AdaptRateLimiter::writeData(Buffer::Instance& incoming_buffer, bool end_stream) {
  ENVOY_LOG(trace, "limiter: incoming data length={} buffered bytes={}, buffered requests={}", incoming_buffer.length(),
            bytes_in_q_, request_q_.size());
  // We always assume that we have a request in the queue,
  // so if the queue is empty we need to add a request
  if (new_request_) {
    request_q_.push(std::make_shared<Request>(incoming_buffer, end_stream));
  } else {
    ASSERT(!request_q_.empty());
    RequestPtr req = request_q_.front();
    req->add_to_buffer(incoming_buffer);
    req->set_end_stream(end_stream);
  }

  // If we receive an end to our stream, then we know we're onto
  // a new request
  new_request_ = end_stream;

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
  req->set_end_stream(true);
  req->set_trailers(true);
  return req->buffer().length() > 0 ? Http::FilterTrailersStatus::StopIteration
                              : Http::FilterTrailersStatus::Continue;
}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
