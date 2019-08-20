
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

void QueueManager::AddRedirectAdaptation(std::string orig_host, std::string to_ip, uint64_t queue_length) {
  decode_q_.AddRedirectStrategy(orig_host, to_ip, queue_length);
}

bool QueueManager::MessageInDecoderQueue(MessageSharedPtr m) { return decode_q_.FindInQueue(m); }
bool QueueManager::MessageInEncoderQueue(MessageSharedPtr m) { return encode_q_.FindInQueue(m); }

void QueueManager::AddEncoderToQueue(MessageSharedPtr m) { encode_q_.Push(m); }

void QueueManager::AddDecoderToQueue(MessageSharedPtr m) {
  decode_q_.Push(m);
}

} // namespace AdaptFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy