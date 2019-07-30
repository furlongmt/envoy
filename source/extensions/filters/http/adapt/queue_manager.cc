
#include "extensions/filters/http/adapt/queue_manager.h"

#include "common/http/utility.h"
#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

QueueManager::QueueManager() : encode_q_(true, false, false), decode_q_(false, true, false) {

  // Start decoder timer
  std::thread([this]() {
    while (true) {
      std::chrono::milliseconds ms = decode_q_.drain_request();
      std::this_thread::sleep_for(ms);
    }
  }).detach();

  // Start encoder timer
  std::thread([this]() {
    while (true) {
      std::chrono::milliseconds ms = encode_q_.drain_request();
      std::this_thread::sleep_for(ms);
    }
  }).detach();
}

void QueueManager::setDecodeMaxKbps(uint64_t max_kbps) {
  decode_q_.SetMaxKbps(max_kbps);
}

void QueueManager::addEncoderToQueue(Http::StreamEncoderFilterCallbacks* callbacks, uint64_t size,
                                     bool headers_only, const Http::HeaderMap& headers) {
  RequestSharedPtr req =
      std::make_shared<Request>(callbacks->dispatcher(), callbacks, size, headers_only, headers);
  encode_q_.Push(req);
}

void QueueManager::addDecoderToQueue(Http::StreamDecoderFilterCallbacks* callbacks, uint64_t size,
                                     bool headers_only, const Http::HeaderMap& headers) {
  RequestSharedPtr req =
      std::make_shared<Request>(callbacks->dispatcher(), callbacks, size, headers_only, headers);
  decode_q_.Push(req);
}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy