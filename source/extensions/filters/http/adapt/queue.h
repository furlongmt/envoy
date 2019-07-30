

#include "extensions/filters/http/adapt/request.h"
#include "common/common/token_bucket_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

class Queue : Logger::Loggable<Logger::Id::filter> {

public:
    Queue(bool encode, bool drop, bool transform);
    void Push(RequestSharedPtr req);
    void Pop(); // currently unused
    void SetMaxKbps(uint64_t max_kbps);
    std::chrono::milliseconds drain_request();

private:
    
  void pop();
  void transform(std::function<void(Buffer::Instance&)> buf_func,
                               std::function<void(Http::HeaderMap&)> header_func);
  std::list<RequestSharedPtr>::iterator drop(std::list<RequestSharedPtr>::iterator it);
  void drop_every_nth_request(uint64_t n);
  void drop_first_n_requests(uint64_t n);
  void drop_large_messages(uint64_t size);
  
  // We currently divide each second into 16 segments for the token bucket. Thus, the rate limit
  // is KiB per second, divided into 16 segments, ~63ms apart. 16 is used because it divides
  // into 1024 evenly.
  const uint64_t SecondDivisor = 16;
  const uint64_t MaxTokens = 10000; // this number is completely random
  const uint64_t BytesThreshold = 1000;

  static RealTimeSource time_source_;
  
  bool drop_;
  bool transform_;
  bool encode_; // Either encode or decode(!encode_) queue
  bool saw_data_{};
  uint64_t max_kbps_ = 100;
  uint64_t bytes_per_time_slice_;
  uint64_t bytes_in_q_{0};
  TokenBucketImpl token_bucket_;
  std::list<RequestSharedPtr> queue_;
  std::unordered_set<RequestSharedPtr> adapt_set_;
  std::list<RequestSharedPtr>::iterator drop_iterator_;

  std::mutex mtx_;
  std::condition_variable cv_;
};

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy