package main

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"sync/atomic"
	"time"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

var control = &http.Client{Timeout: 20 * time.Second}

func exchange(path string) string {
	r, err := control.Get(os.Getenv("UPSTREAM_CONTROL") + path)
	if err != nil {
		panic(err)
	}
	defer r.Body.Close()
	b, err := io.ReadAll(r.Body)
	if err != nil {
		panic(err)
	}
	return string(b)
}

func event(path string) {
	if os.Getenv("UPSTREAM_BENCH") == "true" && strings.HasPrefix(path, "destroy/") {
		return
	}
	exchange("/event/" + path)
}

type config struct{}

func (*config) Create([]byte) (shared.ClusterFactory, error) { return &factory{}, nil }

type factory struct{ shared.EmptyClusterFactory }

func (*factory) Create(h shared.ClusterHandle) shared.Cluster { return &cluster{h: h} }

type cluster struct {
	shared.EmptyCluster
	h         shared.ClusterHandle
	scheduler shared.ClusterScheduler
	host      shared.ClusterHostHandle
	leases    atomic.Int64
	retiring  atomic.Bool
	removed   bool
}

func (c *cluster) OnInit() {
	c.scheduler = c.h.NewScheduler()
	go func() { c.scheduler.Schedule(1) }()
}
func (c *cluster) OnScheduled(id uint64) {
	if id == 1 {
		hosts, ok := c.h.AddHosts([]shared.ClusterHostSpec{{Address: os.Getenv("UPSTREAM_ADDRESS"), Weight: 1}})
		if !ok || len(hosts) != 1 || hosts[0] == nil {
			panic("host initialization failed")
		}
		c.host = hosts[0]
		c.h.PreInitComplete()
		go event("initialized")
	}
	if id == 2 {
		if c.removed {
			return
		}
		c.retiring.Store(true)
		go event("retiring")
		if c.leases.Load() != 0 {
			return
		}
		if c.h.RemoveHosts([]shared.ClusterHostHandle{c.host}) != 1 {
			panic("remove failed")
		}
		c.removed = true
		go event("removed")
	}
}
func (c *cluster) OnServerInitialized() {
	go func() { exchange("/mutation"); c.scheduler.Schedule(2) }()
}

func (c *cluster) OnDestroy() {
	c.scheduler.Close()
	if c.scheduler.Schedule(9) {
		panic("closed scheduler accepted event")
	}
	event("cluster-destroyed")
}
func (c *cluster) NewLoadBalancer(h shared.ClusterLoadBalancerHandle) shared.ClusterLoadBalancer {
	ctx, cancel := context.WithCancel(context.Background())
	return &worker{cluster: c, h: h, ctx: ctx, cancel: cancel}
}

type worker struct {
	cluster *cluster
	h       shared.ClusterLoadBalancerHandle
	ctx     context.Context
	cancel  context.CancelFunc
}

func (w *worker) OnDestroy() {
	w.cancel()
	if os.Getenv("UPSTREAM_TEARDOWN_BARRIER") == "true" {
		exchange("/teardown")
	}
	event("worker-destroyed")
}
func (w *worker) OnHostMembershipUpdate() { go event(fmt.Sprintf("membership/%d", len(w.h.Hosts()))) }
func (w *worker) ChooseHost(c shared.ClusterLoadBalancerContext) shared.ClusterHostHandle {
	if c == nil {
		return nil
	}
	value, ok := c.GetDownstreamHeader("x-operation")
	if !ok {
		return nil
	}
	id := string(value.ToBytes())
	if id == "block-worker" {
		exchange("/block-worker")
		return nil
	}
	if w.cluster.retiring.Load() {
		return nil
	}
	hosts := w.h.Hosts()
	if len(hosts) != 1 {
		return nil
	}
	w.cluster.leases.Add(1)
	var destroyed atomic.Bool
	var calls atomic.Int32
	op, ok := c.BeginAsync(func() {
		destroyed.Store(true)
		if w.cluster.leases.Add(-1) == 0 && w.cluster.retiring.Load() {
			w.cluster.scheduler.Schedule(2)
		}
		if calls.Add(1) != 1 {
			panic("double destruction")
		}
		go event("destroy/" + id)
	})
	if !ok {
		panic("HTTP context has no dispatcher")
	}
	if id == "immediate" {
		if !op.Complete(hosts[0], "") {
			panic("immediate completion rejected")
		}
		if destroyed.Load() {
			panic("completion destroyed handle inline")
		}
		return nil
	}
	go func() {
		exchange("/select/" + id)
		accepted := op.Complete(hosts[0], "")
		event(fmt.Sprintf("posted/%s/%t", id, accepted))
	}()
	return nil
}

// Compile-time assertions keep fixtures coupled to the consumer-facing interfaces.
var _ shared.ClusterConfigFactory = (*config)(nil)

func init() {
	sdk.RegisterClusterConfigFactories(map[string]shared.ClusterConfigFactory{"lifecycle": &config{}})
}
func main() {}
