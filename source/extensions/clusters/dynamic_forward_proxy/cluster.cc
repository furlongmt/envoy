#include "extensions/clusters/dynamic_forward_proxy/cluster.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicForwardProxy {

Cluster::Cluster(
    const envoy::api::v2::Cluster& cluster,
    const envoy::config::cluster::dynamic_forward_proxy::v2alpha::ClusterConfig& config,
    Runtime::Loader& runtime,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    const LocalInfo::LocalInfo& local_info,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : Upstream::BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                                       added_via_api),
      dns_cache_manager_(cache_manager_factory.get()),
      dns_cache_(dns_cache_manager_->getCache(config.dns_cache_config())),
      update_callbacks_handle_(dns_cache_->addUpdateCallbacks(*this)), local_info_(local_info),
      host_map_(std::make_shared<HostInfoMap>()) {
  // fixfix do initial load from cache.
}

void Cluster::onDnsHostAddOrUpdate(
    const std::string& host,
    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info) {

  ASSERT(host_info->address() != nullptr); // fixfix

  HostInfoMapSharedPtr current_map = getCurrentHostMap();
  const auto host_map_it = current_map->find(host);
  if (host_map_it != current_map->end()) {
    // If we only have an address change, we can do that swap inline without any other updates. The
    // appropriate locking is in place to allow this.
    ASSERT(host_info == host_map_it->second.shared_host_info_);
    ASSERT(host_map_it->second.shared_host_info_->address() !=
           host_map_it->second.logical_host_->address());
    ENVOY_LOG(debug, "updating dfproxy cluster host address '{}'", host);
    host_map_it->second.logical_host_->setNewAddress(host_info->address(), dummy_lb_endpoint_);
    return;
  }

  ENVOY_LOG(debug, "adding new dfproxy cluster host '{}'", host);
  const auto new_host_map = std::make_shared<HostInfoMap>(*current_map);
  const auto emplaced = new_host_map->try_emplace(
      host, host_info,
      std::make_shared<Upstream::LogicalHost>(info(), host, host_info->address(),
                                              dummy_locality_lb_endpoint_, dummy_lb_endpoint_));
  Upstream::HostVector hosts_added;
  hosts_added.emplace_back(emplaced.first->second.logical_host_);

  // Swap in the new map. This will be picked up when the per-worker LBs are recreated via
  // the host set update.
  swapAndUpdateMap(new_host_map, hosts_added, {});
}

void Cluster::swapAndUpdateMap(const HostInfoMapSharedPtr& new_hosts_map,
                               const Upstream::HostVector& hosts_added,
                               const Upstream::HostVector& hosts_removed) {
  {
    absl::WriterMutexLock lock(&host_map_lock_);
    host_map_ = new_hosts_map;
  }

  Upstream::PriorityStateManager priority_state_manager(*this, local_info_, nullptr);
  priority_state_manager.initializePriorityFor(dummy_locality_lb_endpoint_);
  for (const auto& host : (*new_hosts_map)) {
    priority_state_manager.registerHostForPriority(host.second.logical_host_,
                                                   dummy_locality_lb_endpoint_);
  }
  priority_state_manager.updateClusterPrioritySet(
      0, std::move(priority_state_manager.priorityState()[0].first), hosts_added, hosts_removed,
      absl::nullopt, absl::nullopt);
}

void Cluster::onDnsHostRemove(const std::string& host) {
  HostInfoMapSharedPtr current_map = getCurrentHostMap();
  const auto host_map_it = current_map->find(host);
  ASSERT(host_map_it != current_map->end());
  const auto new_host_map = std::make_shared<HostInfoMap>(*current_map);
  Upstream::HostVector hosts_removed;
  hosts_removed.emplace_back(host_map_it->second.logical_host_);
  new_host_map->erase(host);
  ENVOY_LOG(debug, "removing dfproxy cluster host '{}'", host);

  // Swap in the new map. This will be picked up when the per-worker LBs are recreated via
  // the host set update.
  swapAndUpdateMap(new_host_map, {}, hosts_removed);
}

Upstream::HostConstSharedPtr
Cluster::LoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  // fixfix check context for headers.
  const auto host_it =
      host_map_->find(context->downstreamHeaders()->Host()->value().getStringView());
  if (host_it == host_map_->end()) {
    return nullptr;
  } else {
    host_it->second.shared_host_info_->touch();
    return host_it->second.logical_host_;
  }
}

class DnsCacheManagerFactoryImpl
    : public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  DnsCacheManagerFactoryImpl(Singleton::Manager& singleton_manager, Event::Dispatcher& dispatcher,
                             ThreadLocal::SlotAllocator& tls)
      : singleton_manager_(singleton_manager), dispatcher_(dispatcher), tls_(tls) {}

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return Extensions::Common::DynamicForwardProxy::getCacheManager(singleton_manager_, dispatcher_,
                                                                    tls_);
  }

private:
  Singleton::Manager& singleton_manager_;
  Event::Dispatcher& dispatcher_;
  ThreadLocal::SlotAllocator& tls_;
};

std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
ClusterFactory::createClusterWithConfig(
    const envoy::api::v2::Cluster& cluster,
    const envoy::config::cluster::dynamic_forward_proxy::v2alpha::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  DnsCacheManagerFactoryImpl cache_manager_factory(context.singletonManager(), context.dispatcher(),
                                                   context.tls());
  auto new_cluster = std::make_shared<Cluster>(
      cluster, proto_config, context.runtime(), cache_manager_factory, context.localInfo(),
      socket_factory_context, std::move(stats_scope), context.addedViaApi());
  auto lb = std::make_unique<Cluster::ThreadAwareLoadBalancer>(*new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
