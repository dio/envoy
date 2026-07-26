package shared

import "unsafe"

// ClusterHostHandle is an opaque handle to an Envoy upstream host.
//
// Envoy owns the referenced host. The handle remains valid while the host belongs to the cluster.
type ClusterHostHandle unsafe.Pointer

// ClusterHostSpec describes an upstream host to add to a dynamic-module cluster.
type ClusterHostSpec struct {
	// Address is the concrete IP:port Envoy connects to.
	Address string
	// Hostname is the logical hostname exposed through HostDescription::hostname(). An empty
	// hostname uses the same synthesized hostname behavior as the existing address-only callback.
	Hostname string
	// Weight must be between 1 and 128.
	Weight uint32
}

// ClusterHostHealth is the health state of an upstream host.
type ClusterHostHealth uint32

const (
	ClusterHostUnhealthy ClusterHostHealth = iota
	ClusterHostDegraded
	ClusterHostHealthy
)

// ClusterHandle provides main-thread operations on an Envoy dynamic-module cluster.
//
// Its methods must only be called from cluster lifecycle callbacks that run on Envoy's main
// thread.
type ClusterHandle interface {
	// AddHosts adds a batch of priority-zero hosts. The returned handles correspond to specs. A
	// nil handle means Envoy skipped that spec because its address already belongs to the cluster.
	AddHosts(specs []ClusterHostSpec) ([]ClusterHostHandle, bool)
	// UpdateHostHealth updates one host's health state.
	UpdateHostHealth(host ClusterHostHandle, health ClusterHostHealth) bool
	// PreInitComplete signals that initial host discovery has completed.
	PreInitComplete()
}

// ClusterLoadBalancerContext exposes request information during host selection.
//
// The context is valid only for the duration of ClusterLoadBalancer.ChooseHost.
type ClusterLoadBalancerContext interface {
	// GetDownstreamHeader returns the first value for a downstream request header.
	GetDownstreamHeader(key string) (UnsafeEnvoyBuffer, bool)
}

// ClusterLoadBalancer selects an upstream host for each request.
type ClusterLoadBalancer interface {
	// ChooseHost returns the selected host, or nil when no host is available.
	ChooseHost(context ClusterLoadBalancerContext) ClusterHostHandle
	// OnDestroy is called when Envoy destroys this worker-local load balancer.
	OnDestroy()
}

// EmptyClusterLoadBalancer provides no-op load-balancer hooks.
type EmptyClusterLoadBalancer struct{}

// ChooseHost implements ClusterLoadBalancer.
func (*EmptyClusterLoadBalancer) ChooseHost(ClusterLoadBalancerContext) ClusterHostHandle {
	return nil
}

// OnDestroy implements ClusterLoadBalancer.
func (*EmptyClusterLoadBalancer) OnDestroy() {}

// Cluster is the module-side instance of an Envoy cluster.
type Cluster interface {
	// OnInit performs initial host discovery and must eventually call PreInitComplete.
	OnInit()
	// OnServerInitialized is called on Envoy's main thread after all clusters have initialized and
	// before workers start.
	OnServerInitialized()
	// NewLoadBalancer creates a worker-local load balancer.
	NewLoadBalancer() ClusterLoadBalancer
	// OnDestroy is called when Envoy destroys the cluster.
	OnDestroy()
}

// EmptyCluster provides no-op cluster hooks.
type EmptyCluster struct{}

// OnInit implements Cluster.
func (*EmptyCluster) OnInit() {}

// OnServerInitialized implements Cluster.
func (*EmptyCluster) OnServerInitialized() {}

// NewLoadBalancer implements Cluster.
func (*EmptyCluster) NewLoadBalancer() ClusterLoadBalancer {
	return &EmptyClusterLoadBalancer{}
}

// OnDestroy implements Cluster.
func (*EmptyCluster) OnDestroy() {}

// ClusterFactory creates a module-side cluster instance.
type ClusterFactory interface {
	// Create constructs a cluster bound to handle.
	Create(handle ClusterHandle) Cluster
	// OnDestroy is called when Envoy destroys this parsed cluster configuration.
	OnDestroy()
}

// EmptyClusterFactory provides a no-op configuration-destroy hook.
type EmptyClusterFactory struct{}

// OnDestroy implements ClusterFactory.
func (*EmptyClusterFactory) OnDestroy() {}

// ClusterConfigFactory parses cluster configuration.
type ClusterConfigFactory interface {
	// Create parses unparsedConfig and returns a cluster factory.
	Create(unparsedConfig []byte) (ClusterFactory, error)
}
