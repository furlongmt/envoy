
#include "extensions/filters/http/adapt/queue_manager.h"

#include "common/http/utility.h"
#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

QueueManager::QueueManager() : encode_q_(true, false), decode_q_(false, false) {

  // Start decoder timer
  std::thread([this]() {
    while (true) {
      std::chrono::milliseconds ms = decode_q_.DrainRequest();
      std::this_thread::sleep_for(ms);
    }
  }).detach();

  // Start encoder timer
  std::thread([this]() {
    while (true) {
      std::chrono::milliseconds ms = encode_q_.DrainRequest();
      std::this_thread::sleep_for(ms);
    }
  }).detach();
}

void QueueManager::SetDecodeMaxKbps(uint64_t max_kbps) {
  decode_q_.set_max_kbps(max_kbps);
  ENVOY_LOG(critical, "Updated decode max kbps to {}", max_kbps);
}

void QueueManager::SetEncodeMaxKbps(uint64_t max_kbps) {
  encode_q_.set_max_kbps(max_kbps);
  ENVOY_LOG(critical, "Updated encode max kbps to {}", max_kbps);
}

void QueueManager::AddDropAdaptation(std::string type, uint64_t n, uint64_t queue_length) {
  decode_q_.AddDropStrategy(type, n, queue_length);
  ENVOY_LOG(critical, "Set drop type {} to {} when queue_length = {}", type, n, queue_length);
}

void QueueManager::AddEncoderToQueue(Http::StreamEncoderFilterCallbacks* callbacks, uint64_t size,
                                     bool headers_only, const Http::HeaderMap& headers, bool& dropped) {
  MessageSharedPtr req =
      std::make_shared<Message>(callbacks->dispatcher(), callbacks, size, headers_only, headers, dropped);
  encode_q_.Push(req);
}

void QueueManager::AddDecoderToQueue(Http::StreamDecoderFilterCallbacks* callbacks, uint64_t size,
                                     bool headers_only, const Http::HeaderMap& headers, bool& dropped) {
  MessageSharedPtr req =
      std::make_shared<Message>(callbacks->dispatcher(), callbacks, size, headers_only, headers, dropped);
  decode_q_.Push(req);
}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy