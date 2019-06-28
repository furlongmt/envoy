#include <iostream>
#include <memory>

#include "test/mocks/network/mocks.h"

#include "adapt.h"
#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace AdaptFilter {
namespace {
class AdaptFilterTest : public testing::Test {
public:
  void SetUpTest(const std::string& yaml) {
    envoy::config::filter::network::adapt::v2::Adapt proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config);
    config_.reset(new Config(proto_config, stats_store_, runtime_));
    filter_ = std::make_unique<Adapt>(config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  ~AdaptFilterTest() {
    std::cout << "STATS TIME" << std::endl;
    for (const Stats::GaugeSharedPtr& counter : stats_store_.gauges()) {
        std::cout << counter->name() << ": " << counter->value() << std::endl;
    }
  }

  std::unique_ptr<Adapt> filter_;
  NiceMock<Runtime::MockLoader> runtime_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;

  ConfigSharedPtr config_;  
  const std::string filter_config_ = R"EOF(
stat_prefix: name
queue_size: 4
  )EOF";
};

TEST_F(AdaptFilterTest, OK) {
  SetUpTest(filter_config_);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl buf("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(buf, false));
  EXPECT_EQ(1, stats_store_.counter("adapt.name.queue_size").value());
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(buf, false));
  EXPECT_EQ(2, stats_store_.counter("adapt.name.queue_size").value());
}

} // namespace
} // namespace AdaptFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
