#pragma once

#include "common/buffer/buffer_impl.h"
#include "common/buffer/watermark_buffer.h"
#include "common/common/token_bucket_impl.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/filter.h"
#include "boost/lockfree/queue.hpp"

#include <memory>
#include <queue>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

class Request {
  public:
    Request(Buffer::Instance& buffer) : saw_trailers_(false) {
      buffer_.move(buffer);
    }
    Buffer::OwnedImpl& buffer() { return buffer_; }
    //bool saw_end_stream() { return saw_end_stream_; }
    bool saw_trailers() { return saw_trailers_; }

    void add_to_buffer(Buffer::Instance& buffer) { buffer_.add(buffer); }
    void set_trailers(bool saw_trailers) { saw_trailers_ = saw_trailers; }

  private:
    Buffer::OwnedImpl buffer_;
    bool saw_trailers_{};
};

using RequestPtr = std::shared_ptr<Request>;

/*
 * This is almost an exact copy of StreamRateLimiter from extensions/filters/http/fault/fault_filter.h
 * except that now we're storing requests seperately within the queue.
 */
class AdaptRateLimiter : Logger::Loggable<Logger::Id::filter> {
public:
  /**
   * @param max_kbps maximum rate in KiB/s.
   * @param max_buffered_data maximum data to buffer before invoking the pause callback.
   * @param pause_data_cb callback invoked when the limiter has buffered too much data.
   * @param resume_data_cb callback invoked when the limiter has gone under the buffer limit.
   * @param write_data_cb callback invoked to write data to the stream.
   * @param continue_cb callback invoked to continue the stream. This is only used to continue
   *                    trailers that have been paused during body flush.
   * @param time_source the time source to run the token bucket with.
   * @param dispatcher the stream's dispatcher to use for creating timers.
   */
  AdaptRateLimiter(uint64_t max_kbps, uint64_t max_buffered_data,
                    std::function<void(Buffer::Instance&, bool)> write_data_cb,
                    std::function<void()> continue_cb, TimeSource& time_source,
                    Event::Dispatcher& dispatcher);
  
  /**
   * Called by the stream to write data. All data writes happen asynchronously, the stream should
   * be stopped after this call (all data will be drained from incoming_buffer).
   */
  void writeData(Buffer::Instance& incoming_buffer);

  /**
   * Called if the stream receives trailers.
   */
  Http::FilterTrailersStatus onTrailers();

  void destroy() { token_timer_.reset(); }
  bool destroyed() { return token_timer_ == nullptr; }

private:
  void onTokenTimer();

  // We currently divide each second into 16 segments for the token bucket. Thus, the rate limit is
  // KiB per second, divided into 16 segments, ~63ms apart. 16 is used because it divides into 1024
  // evenly.
  static constexpr uint64_t SecondDivisor = 16;
  static constexpr uint64_t MaxTokens = 10000; // this number is completely random

  const uint64_t bytes_per_time_slice_;
  const std::function<void(Buffer::Instance&, bool)> write_data_cb_;
  const std::function<void()> continue_cb_;
  Event::TimerPtr token_timer_;

  static bool saw_data_;
  static std::shared_ptr<TokenBucketImpl> token_bucket_;
  static uint64_t bytes_in_q_;
  //static boost::lockfree::queue<RequestPtr> request_q_; // global queue for all rate limiting
  static std::mutex queue_mutex_;
  static std::queue<RequestPtr> request_q_; // global queue for all rate limiting
};

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
