#pragma once

#include "envoy/http/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

/**
 * This struct defines the state that we need to maintain for each request in our queue.
 */
struct Message {
  Message(Http::StreamEncoderFilterCallbacks* encoder_callbacks, const Http::HeaderMap& headers)
      : dispatcher_(encoder_callbacks->dispatcher()), encoder_callbacks_(encoder_callbacks),
        headers_(headers) {}

  Message(Http::StreamDecoderFilterCallbacks* decoder_callbacks, const Http::HeaderMap& headers)
      : dispatcher_(decoder_callbacks->dispatcher()), decoder_callbacks_(decoder_callbacks),
        headers_(headers) {}

  Event::Dispatcher& dispatcher_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  uint64_t size_{0};
  bool headers_only_{}; // Specifies if request has no payload
  const Http::HeaderMap& headers_;
  bool adapted_{};    // TODO: this shouldn't be a bool as there's levels of adaption
  bool dropped_{}; // This represents whether the request was drooped from our queue (it's a reference back to the dropped_ variable in adapt_filter.h)
  std::chrono::system_clock::time_point entered_tp_; // Time the message entered the queue
  std::chrono::system_clock::time_point exited_tp_; // Time the message exited the queue
};

using MessageSharedPtr = std::shared_ptr<Message>;

}
}
}
}