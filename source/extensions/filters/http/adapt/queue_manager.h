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
  static QueueManager& Instance() {
    static QueueManager instance;
    return instance;
  }

  void setDecodeMaxKbps(uint64_t max_kbps);

  void addEncoderToQueue(Http::StreamEncoderFilterCallbacks* callbacks, uint64_t size,
                         bool headers_only, const Http::HeaderMap& headers);
  void addDecoderToQueue(Http::StreamDecoderFilterCallbacks* callbacks, uint64_t size,
                         bool headers_only, const Http::HeaderMap& headers);

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