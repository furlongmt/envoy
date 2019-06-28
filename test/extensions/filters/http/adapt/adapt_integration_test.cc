#include "test/integration/http_protocol_integration.h"

namespace Envoy {
class AdaptIntegrationTest : public Event::TestUsingSimulatedTime,
                                        public HttpProtocolIntegrationTest {
public:

  void initializeFilter() {
    config_helper_.addFilter("{ name: adapt-filter, config: { limit_kbps: 1 } }");
    initialize();
  }
};

INSTANTIATE_TEST_CASE_P(Protocols, AdaptIntegrationTest,
                        testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                        HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(AdaptIntegrationTest, Test1) {
  initializeFilter();
  //Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};

  IntegrationCodecClientPtr codec_client;
  //FakeHttpConnectionPtr fake_upstream_connection;
  //FakeStreamPtr request_stream;
  codec_client = makeHttpConnection(lookupPort("http"));
  auto r = codec_client->startRequest(default_request_headers_);

  Buffer::OwnedImpl d(std::string(130, 'b'));
  r.first.encodeData(d, true); // for some reason simulated time doesn't seem to work here as the queue just goes...
  waitForNextUpstreamRequest();
  
  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(127, 'a'));
  upstream_request_->encodeData(data, true);
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.adapt.response_queue_size")->value());
  //EXPECT_EQ(127UL, test_server_->gauge("http.config_test.adapt.request_queue_size")->value());

  // Wait for a tick worth of data
  r.second->waitForBodyData(64);
  EXPECT_EQ(63UL, test_server_->gauge("http.config_test.adapt.response_queue_size")->value()); 

  // Wait for a tick worth of data and end stream
  simTime().sleep(std::chrono::milliseconds(63));
  r.second->waitForBodyData(127);
  r.second->waitForEndStream();

  //EXPECT_EQ(127UL, test_server_->gauge("http.config_test.adapt.response_queue_size")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.adapt.response_queue_size")->value());

  codec_client->close();
}
} // namespace Envoy
