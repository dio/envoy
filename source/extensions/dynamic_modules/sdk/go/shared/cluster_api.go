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
	// AddHosts adds a batch of priority-zero hosts. The returned handles correspond to specs.
	// Callers must submit only new, distinct addresses; mixed duplicate batches are unsupported.
	AddHosts(specs []ClusterHostSpec) ([]ClusterHostHandle, bool)
	// RemoveHosts removes hosts on the main thread. Callers must first end all selection leases.
	RemoveHosts(hosts []ClusterHostHandle) int
	// NewScheduler creates a scheduler on the main thread. Close it during cluster destruction.
	NewScheduler() ClusterScheduler
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
	// GetFilterState copies the request's string filter state into owned Go memory.
	GetFilterState(key string) (string, bool)
	// BeginAsync starts one asynchronous selection. ChooseHost must then return nil.
	// It rejects contexts without a worker dispatcher. onDestroy runs exactly once, including
	// after normal completion, and must release any selected host lease. The operation remains
	// valid after cancellation: subsequent Complete calls return false without accessing Envoy.
	BeginAsync(onDestroy func()) (ClusterAsyncSelection, bool)
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
	NewLoadBalancer(handle ClusterLoadBalancerHandle) ClusterLoadBalancer
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
func (*EmptyCluster) NewLoadBalancer(ClusterLoadBalancerHandle) ClusterLoadBalancer {
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

// ClusterScheduler posts events to ClusterScheduled.OnScheduled on Envoy's main thread.
// Schedule and Close are safe to call concurrently. Close prevents new events, but events
// already queued can still run before cluster destruction. Envoy drops them after destruction.
type ClusterScheduler interface {
	Schedule(event uint64) bool
	Close()
}

// ClusterScheduled is an optional main-thread event hook.
type ClusterScheduled interface{ OnScheduled(event uint64) }

// ClusterLoadBalancerHandle is borrowed during worker lifecycle and ChooseHost callbacks.
// Hosts returns an owned slice of priority-zero host handles, whose pointees remain Envoy-owned.
type ClusterLoadBalancerHandle interface{ Hosts() []ClusterHostHandle }

// ClusterMembershipObserver is an optional worker membership hook. Read a fresh snapshot via
// ClusterLoadBalancerHandle.Hosts during this callback; do not retain borrowed Envoy containers.
type ClusterMembershipObserver interface{ OnHostMembershipUpdate() }

// ClusterAsyncSelection serializes completion with the final cancellation/destruction hook.
// Complete is callable from any goroutine at most once; a false result means it was already
// completed or destroyed. A true result means posted, not delivered. The caller must retain
// its host lease until onDestroy, even after Complete returns. Do not hold other locks across it.
type ClusterAsyncSelection interface {
	Complete(host ClusterHostHandle, details string) bool
}
