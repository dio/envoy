package main

import (
	"strings"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterClusterConfigFactories(map[string]shared.ClusterConfigFactory{
		"go_cluster_test": &clusterConfigFactory{},
	})
}

func main() {} //nolint:all

type clusterConfigFactory struct{}

func (*clusterConfigFactory) Create([]byte) (shared.ClusterFactory, error) {
	return &clusterFactory{}, nil
}

type clusterFactory struct {
	shared.EmptyClusterFactory
}

func (*clusterFactory) Create(handle shared.ClusterHandle) shared.Cluster {
	return &cluster{handle: handle}
}

type cluster struct {
	shared.EmptyCluster
	handle shared.ClusterHandle
	hosts  map[string]shared.ClusterHostHandle
}

func (c *cluster) OnInit() {
	c.addHost(shared.ClusterHostSpec{
		Address:  "127.0.0.1:10001",
		Hostname: "service-a.test",
		Weight:   1,
	})
	c.handle.PreInitComplete()
}

func (c *cluster) OnServerInitialized() {
	c.addHost(shared.ClusterHostSpec{
		Address:  "127.0.0.1:10002",
		Hostname: "service-b.test",
		Weight:   1,
	})
}

func (c *cluster) addHost(spec shared.ClusterHostSpec) {
	hosts, ok := c.handle.AddHosts([]shared.ClusterHostSpec{spec})
	if !ok || hosts[0] == nil {
		return
	}
	if c.hosts == nil {
		c.hosts = make(map[string]shared.ClusterHostHandle)
	}
	c.hosts[spec.Hostname] = hosts[0]
	c.handle.UpdateHostHealth(hosts[0], shared.ClusterHostHealthy)
}

func (c *cluster) NewLoadBalancer() shared.ClusterLoadBalancer {
	return &clusterLoadBalancer{hosts: c.hosts}
}

type clusterLoadBalancer struct {
	shared.EmptyClusterLoadBalancer
	hosts map[string]shared.ClusterHostHandle
}

func (l *clusterLoadBalancer) ChooseHost(
	context shared.ClusterLoadBalancerContext,
) shared.ClusterHostHandle {
	target := "service-a.test"
	if context != nil {
		if value, ok := context.GetDownstreamHeader("x-target-host"); ok {
			target = strings.ToLower(value.ToString())
		}
	}
	return l.hosts[target]
}
