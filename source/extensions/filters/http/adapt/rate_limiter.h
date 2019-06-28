#pragma once

#include "common/buffer/buffer_impl.h"
#include "common/buffer/watermark_buffer.h"
#include "common/common/token_bucket_impl.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/filter.h"

#include <memory>
#include <queue>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

class Request {
  public:
    Request(Buffer::Instance& buffer, bool end_stream) : buffer_(buffer), saw_end_stream_(end_stream) {}
    Buffer::Instance& buffer() { return buffer_; }
    bool saw_end_stream() { return saw_end_stream_; }
    bool saw_trailers() { return saw_trailers_; }

    void add_to_buffer(Buffer::Instance& buffer) { buffer_.add(buffer); }
    bool set_end_stream(bool end_stream) { saw_end_stream_ = end_stream; }
    void set_trailers(bool saw_trailers) { saw_trailers_ = saw_trailers; }

  private:
    Buffer::OwnedImpl buffer_;
    bool saw_end_stream_;
    bool saw_trailers_;
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
  void writeData(Buffer::Instance& incoming_buffer, bool end_stream);

  /**
   * Called if the stream receives trailers.
   */
  Http::FilterTrailersStatus onTrailers();

private:
  void onTokenTimer();

  // We currently divide each second into 16 segments for the token bucket. Thus, the rate limit is
  // KiB per second, divided into 16 segments, ~63ms apart. 16 is used because it divides into 1024
  // evenly.
  static constexpr uint64_t SecondDivisor = 16;

  const uint64_t bytes_per_time_slice_;
  const std::function<void(Buffer::Instance&, bool)> write_data_cb_;
  const std::function<void()> continue_cb_;
  TokenBucketImpl token_bucket_;
  Event::TimerPtr token_timer_;
  uint64_t bytes_in_q_;
  bool new_request_{};
  std::queue<RequestPtr> request_q_;
};

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
