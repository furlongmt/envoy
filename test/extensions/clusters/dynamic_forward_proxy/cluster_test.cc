#include "common/singleton/manager_impl.h"

#include "extensions/clusters/dynamic_forward_proxy/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"

using testing::AtLeast;
using testing::DoAll;
using testing::InSequence;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicForwardProxy {

class ClusterTest : public testing::Test,
                    public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  ClusterTest() {
    const std::string yaml_config = R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: envoy.clusters.dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig
    dns_cache_config:
      name: foo
      dns_lookup_family: AUTO
  )EOF";

    envoy::api::v2::Cluster cluster_config = Upstream::parseClusterFromV2Yaml(yaml_config);
    envoy::config::cluster::dynamic_forward_proxy::v2alpha::ClusterConfig config;
    Config::Utility::translateOpaqueConfig(cluster_config.cluster_type().typed_config(),
                                           ProtobufWkt::Struct::default_instance(),
                                           ProtobufMessage::getStrictValidationVisitor(), config);
    Stats::ScopePtr scope = stats_store_.createScope("cluster.name.");
    Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_store_,
        singleton_manager_, tls_, validation_visitor_, *api_);
    EXPECT_CALL(*dns_cache_manager_, getCache(_));
    // Below we return a nullptr handle which has no effect on the code under test but isn't
    // actually correct. It's possible this will have to change in the future.
    EXPECT_CALL(*dns_cache_manager_->dns_cache_, addUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&update_callbacks_), Return(nullptr)));
    cluster_ = std::make_shared<Cluster>(cluster_config, config, runtime_, *this, local_info_,
                                         factory_context, std::move(scope), false);
    thread_aware_lb_ = std::make_unique<Cluster::ThreadAwareLoadBalancer>(*cluster_);
    lb_factory_ = thread_aware_lb_->factory();
    refreshLb();

    ON_CALL(lb_context_, downstreamHeaders()).WillByDefault(Return(&downstream_headers_));
  }

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  void makeTestHost(const std::string& host, const std::string& address) {
    EXPECT_TRUE(host_map_.find(host) == host_map_.end());
    host_map_[host] = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
    host_map_[host]->address_ = Network::Utility::parseInternetAddress(address);

    // Allow touch() to still be strict.
    EXPECT_CALL(*host_map_[host], address()).Times(AtLeast(0));
  }

  void updateTestHostAddress(const std::string& host, const std::string& address) {
    EXPECT_FALSE(host_map_.find(host) == host_map_.end());
    host_map_[host]->address_ = Network::Utility::parseInternetAddress(address);
  }

  void refreshLb() { lb_ = lb_factory_->create(); }

  Upstream::MockLoadBalancerContext* setHostAndReturnContext(const std::string& host) {
    downstream_headers_.remove(":authority");
    downstream_headers_.addCopy(":authority", host);
    return &lb_context_;
  }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest().currentThreadId()};
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_{Api::createApiForTest(stats_store_)};
  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager> dns_cache_manager_{
      new Extensions::Common::DynamicForwardProxy::MockDnsCacheManager()};
  std::shared_ptr<Cluster> cluster_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerFactorySharedPtr lb_factory_;
  Upstream::LoadBalancerPtr lb_;
  NiceMock<Upstream::MockLoadBalancerContext> lb_context_;
  Http::TestHeaderMapImpl downstream_headers_;
  Extensions::Common::DynamicForwardProxy::DnsCache::UpdateCallbacks* update_callbacks_{};
  absl::flat_hash_map<std::string,
                      std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>>
      host_map_;
};

// fixfix test add removed/callbacks.

TEST_F(ClusterTest, BasicFlow) {
  makeTestHost("host1", "1.2.3.4");
  InSequence s;

  // Verify no host LB cases.
  EXPECT_EQ(nullptr, lb_->chooseHost(setHostAndReturnContext("foo")));

  // LB will not resolve host1 until it has been updated.
  update_callbacks_->onDnsHostAddOrUpdate("host1", host_map_["host1"]);
  EXPECT_EQ(nullptr, lb_->chooseHost(setHostAndReturnContext("host1")));
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ("1.2.3.4:0",
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address()->asString());
  refreshLb();
  EXPECT_CALL(*host_map_["host1"], touch());
  EXPECT_EQ("1.2.3.4:0", lb_->chooseHost(setHostAndReturnContext("host1"))->address()->asString());

  // After changing the address, LB will immediately resolve the new address with a refresh.
  updateTestHostAddress("host1", "2.3.4.5");
  update_callbacks_->onDnsHostAddOrUpdate("host1", host_map_["host1"]);
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ("2.3.4.5:0",
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address()->asString());
  EXPECT_CALL(*host_map_["host1"], touch());
  EXPECT_EQ("2.3.4.5:0", lb_->chooseHost(setHostAndReturnContext("host1"))->address()->asString());

  // Remove the host, LB will still resolve until it is refreshed.
  update_callbacks_->onDnsHostRemove("host1");
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_CALL(*host_map_["host1"], touch());
  EXPECT_EQ("2.3.4.5:0", lb_->chooseHost(setHostAndReturnContext("host1"))->address()->asString());
  refreshLb();
  EXPECT_EQ(nullptr, lb_->chooseHost(setHostAndReturnContext("host1")));
}

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
