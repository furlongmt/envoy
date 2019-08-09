#pragma once

#include "envoy/http/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

/**
 * This class defines the state that we need to maintain for each request in our queue.
 */
class Request {
public:
  Request(Event::Dispatcher& dispatcher, Http::StreamEncoderFilterCallbacks* encoder_callbacks,
          uint64_t size, bool headers_only, const Http::HeaderMap& headers)
      : dispatcher_(dispatcher), encoder_callbacks_(encoder_callbacks), size_(size),
        headers_only_(headers_only), headers_(headers) {}

  Request(Event::Dispatcher& dispatcher, Http::StreamDecoderFilterCallbacks* decoder_callbacks,
          uint64_t size, bool headers_only, const Http::HeaderMap& headers)
      : dispatcher_(dispatcher), decoder_callbacks_(decoder_callbacks), size_(size),
        headers_only_(headers_only), headers_(headers) {}

  Event::Dispatcher& dispatcher() { return dispatcher_; };
  Http::StreamEncoderFilterCallbacks* encoder_callbacks() { return encoder_callbacks_; }
  Http::StreamDecoderFilterCallbacks* decoder_callbacks() { return decoder_callbacks_; }
  uint64_t size() { return size_; }
  bool headers_only() { return headers_only_; }
  bool adapted() {// TODO: this may not be correct as sometimes we may want to adapt just request headers perhaps?
    return adapted_ || headers_only_;
  } 
  const Http::HeaderMap& headers() { return headers_; }

  void set_adapted(bool adapted) { adapted_ = adapted; }

private:
  Event::Dispatcher& dispatcher_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  uint64_t size_;
  bool headers_only_; // Specifies if request has no payload
  const Http::HeaderMap& headers_;
  bool adapted_{};    // TODO: this shouldn't be a bool as there's levels of adaption
};

using RequestSharedPtr = std::shared_ptr<Request>;

}
}
}
}