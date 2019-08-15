#pragma once

#include "common/buffer/buffer_impl.h"
#include "common/common/token_bucket_impl.h"
#include "envoy/http/filter.h"
#include "extensions/filters/http/adapt/queue.h"

#include <mutex>
#include <queue>

//#define TRANSFORM
#define DROP

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

class QueueManager : Logger::Loggable<Logger::Id::filter> {
public:
  /**
   * Queue Manager is a singleton instance that spawns a thread for
   * both our encode and decode queues to rate limit requests.
   */
  static QueueManager& Instance() {
    static QueueManager instance;
    return instance;
  }

  void SetDecodeMaxKbps(uint64_t max_kbps);
  void SetEncodeMaxKbps(uint64_t max_kbps);

  /**
   * Add a new drop adaption strategy when the config file for this filter changes.
   */
  void AddDropAdaptation(std::string type, uint64_t n, uint64_t queue_length);

  /**
   * Add a new redirect adaptation strategy when the config file for this filter changes. 
   */
  void AddRedirectAdaptation(std::string orig_host, std::string to_ip, uint64_t queue_length);

  /**
   * Add a new message to the encoding queue.
   */
  void AddEncoderToQueue(MessageSharedPtr m);

  /**
   * Add a new message to the decoding queue.
   */
  void AddDecoderToQueue(MessageSharedPtr m);

protected:
  QueueManager();
  ~QueueManager(){};

private:
  Queue encode_q_;
  Queue decode_q_;
};

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy