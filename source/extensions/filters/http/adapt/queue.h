

#include "extensions/filters/http/adapt/message.h"
#include "common/common/token_bucket_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

class Queue : Logger::Loggable<Logger::Id::filter> {

public:
  /**
   * Our constructor for the queue sets up our token_bucket and determines
   * the initial number of bytes to be sent per time slice
   * @param encode True if this queue is for encoding requests,
   *               False if this a decode queue
   * @param transform This is only for easily being able to turn transformations on/off (TODO:
   * remove)
   */
  Queue(bool encode, bool transform);

  /**
   * Add a new drop adaptation to this queue
   * @param type How to drop messages from the queue (e.g. from the front, every_nth, etc)
   * @param n The number of messages to drop
   * @param queue_length Minimum queue length before dropping n messages
   */
  void AddDropStrategy(std::string type, uint64_t n, uint64_t queue_length);

  /**
   * Add a new redirect adaption to this queue
   * @param orig_host The host that this request was originally sent to
   * @param ip_to The ip address that we should redirect our request to
   * @param queue_length Minimum queue length before we redirect messages
   */
  void AddRedirectStrategy(std::string orig_host, std::string ip_to, uint64_t queue_length);

  /**
   * Add a new request to the end of our queue and update the appropriate
   * state (e.g. bytes_in_q_)  and adapt our queue if necessary
   * @param req The request to be added to the back of the queue
   */
  void Push(MessageSharedPtr req);

  /**
   * Grabs mutex before removing request from queue @see pop() below
   */
  void Pop(); // This function is currently unused

  /**
   * This function will attempt to remove a request from the queue if enough
   * tokens are available. It will only drain requests at the bandwidth specified
   * by max_kbps.
   * @return The amount of time to sleep before enough tokens will be available
   * for the next request to be sent.
   */
  std::chrono::milliseconds DrainRequest();

  bool FindInQueue(MessageSharedPtr m);

  /**
   * Set the maximum bandwidth in Kilobytes Per Second for the queue
   */
  void set_max_kbps(uint64_t max_kbps);

private:
  /**
   * This struct should ideally match the parameters that we provide via
   * our drop config api. Currently this is just a value and queue_length threshold.
   */
  struct DropperConfig {
    DropperConfig(uint64_t val, uint64_t thresh) : value(val), threshold(thresh) {}

    uint64_t value;
    uint64_t threshold;
  };

  typedef std::shared_ptr<DropperConfig> DropperConfigSharedPtr;

  /**
   * This struct should match the parameters that we provide via our 
   * request config api. 
   */
  struct RedirectConfig {
    RedirectConfig(std::string ip, uint64_t thresh) : to_ip(ip), threshold(thresh) {}

    std::string to_ip;
    uint64_t threshold;
  };

  typedef std::shared_ptr<RedirectConfig> RedirectConfigSharedPtr;

  /**
   * Checks to see if the queue requires any adaptations
   * and applies them if necessary. For now, this includes transformations
   * and drops. Any additional adaptation strategies should be added here.
   */
  void adapt_queue();

  /**
   * Does NOT acquire mutex. Mutex should should be held before calling this function.
   * Remove request from the front of our queue and update the appropriate
   * state (e.g. bytes_in_q_) and DOES NOT attempt to adapt the queue since
   * the assumption is that adaptation only needs to occur when the queue grows
   */
  void pop();

  /**
   * Iterates over the queue, applying the functions specified to the buffered data
   * and headers to EACH request.
   * @param buf_func Function to modify the internal buffer for request.
   * @param header_func Function to modify headers for the request.
   */
  void transform(std::function<void(Buffer::Instance&)> buf_func,
                 std::function<void(Http::HeaderMap&)> header_func);

  /**
   * Removes the requests passed into the function, updates the necessary internal state
   * and returns the next request in queue so that it can be called in a loop.
   * @param it Iterator to the request to drop
   * @return The next request in the queue
   */
  std::list<MessageSharedPtr>::iterator drop(std::list<MessageSharedPtr>::iterator it);

  /**
   * Checks type of drop request, and calls the appropriate drop function according to this type.
   * @param type Method to drop request
   * @param n How many requests to drop
   */
  void drop_based_on_type(std::string type, uint64_t n);

  /**
   * Sampling - drops every nth request from the queue
   */
  void drop_every_nth_request(uint64_t n);

  /**
   * Temporal - drop first n requests from the queue
   */
  void drop_first_n_requests(uint64_t n);

  /**
   * Size - drop all messages larger than size from the queue
   */
  void drop_large_messages(uint64_t size);

  /**
   * Destination - drops all messages to the specified url
   */
  void drop_messages_to_url(absl::string_view url);

  // DEMO
  void drop_messages_to_cloud(uint64_t n); // DEMO: just for demo
  // DEMO: also just for demo...
  uint64_t cloud_threshold_;

  // We currently divide each second into 1024 segments for the token bucket. Thus, the rate limit
  // is KiB per second, divided into 1024 segments, ~1ms apart. 1024 is used because it divides
  // into 1024 evenly.
  const uint64_t SecondDivisor = 1024;
  const uint64_t MaxTokens = 100000000; // This number is completely random

  static RealTimeSource time_source_;

  bool transform_;
  bool encode_; // Either encode or decode(!encode_) queue
  bool saw_data_{};
  uint64_t max_kbps_;
  uint64_t bytes_per_time_slice_;
  uint64_t bytes_in_q_{0};
  TokenBucketImpl token_bucket_;
  std::list<MessageSharedPtr> queue_;
  std::unordered_set<MessageSharedPtr> transform_set_;

  // orig_host -> RedirectConfig
  std::unordered_map<std::string, RedirectConfigSharedPtr> redirects_;

  // type -> DropperConfig
  std::unordered_map<std::string, DropperConfigSharedPtr> droppers_;
  const static std::string FROM_FRONT;
  const static std::string LARGER_THAN;
  const static std::string EVERY_NTH;
  const static std::string USE_EDGE;

  std::mutex mtx_;
  std::condition_variable cv_;
};

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
