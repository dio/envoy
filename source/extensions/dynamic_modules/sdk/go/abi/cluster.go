package abi

/*
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../../abi/abi.h"

static inline void* envoy_dynamic_module_go_handle_to_pointer(uintptr_t handle) {
  return (void*)handle;
}

static inline uintptr_t envoy_dynamic_module_go_pointer_to_handle(void* pointer) {
  return (uintptr_t)pointer;
}
*/
import "C"

import (
	"runtime"
	"runtime/cgo"
	"sync"
	"unsafe"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type clusterConfigWrapper struct {
	pluginFactory shared.ClusterFactory
}

type clusterWrapper struct {
	plugin shared.Cluster
}

type clusterLoadBalancerWrapper struct {
	plugin        shared.ClusterLoadBalancer
	hostPluginPtr C.envoy_dynamic_module_type_cluster_lb_envoy_ptr
	timer         C.envoy_dynamic_module_type_cluster_worker_timer_module_ptr
	mu            sync.Mutex
	operations    map[*clusterAsyncSelection]struct{}
}

type dymClusterHandle struct {
	hostPluginPtr C.envoy_dynamic_module_type_cluster_envoy_ptr
}

func recordClusterHandle[T any](value *T) unsafe.Pointer {
	handle := cgo.NewHandle(value)
	return C.envoy_dynamic_module_go_handle_to_pointer(C.uintptr_t(handle))
}

func unwrapClusterHandle[T any](pointer unsafe.Pointer) *T {
	if pointer == nil {
		return nil
	}
	handle := cgo.Handle(C.envoy_dynamic_module_go_pointer_to_handle(pointer))
	value, _ := handle.Value().(*T)
	return value
}

func removeClusterHandle(pointer unsafe.Pointer) {
	if pointer == nil {
		return
	}
	handle := cgo.Handle(C.envoy_dynamic_module_go_pointer_to_handle(pointer))
	handle.Delete()
}

func allocateClusterSlice[T any](count int) ([]T, unsafe.Pointer) {
	var value T
	allocation := C.malloc(C.size_t(count) * C.size_t(unsafe.Sizeof(value)))
	if allocation == nil {
		return nil, nil
	}
	return unsafe.Slice((*T)(allocation), count), allocation
}

func copyClusterString(value string) (C.envoy_dynamic_module_type_module_buffer, unsafe.Pointer) {
	if len(value) == 0 {
		return C.envoy_dynamic_module_type_module_buffer{}, nil
	}
	allocation := C.CBytes([]byte(value))
	return C.envoy_dynamic_module_type_module_buffer{
		ptr:    (*C.char)(allocation),
		length: C.size_t(len(value)),
	}, allocation
}

func (h *dymClusterHandle) AddHosts(
	specs []shared.ClusterHostSpec,
) ([]shared.ClusterHostHandle, bool) {
	if len(specs) == 0 {
		return []shared.ClusterHostHandle{}, true
	}

	addresses, addressesAllocation := allocateClusterSlice[C.envoy_dynamic_module_type_module_buffer](len(specs))
	hostnames, hostnamesAllocation := allocateClusterSlice[C.envoy_dynamic_module_type_module_buffer](len(specs))
	weights, weightsAllocation := allocateClusterSlice[C.uint32_t](len(specs))
	localities, localitiesAllocation := allocateClusterSlice[C.envoy_dynamic_module_type_module_buffer](len(specs))
	results, resultsAllocation := allocateClusterSlice[C.envoy_dynamic_module_type_cluster_host_envoy_ptr](len(specs))
	allocations := []unsafe.Pointer{
		addressesAllocation,
		hostnamesAllocation,
		weightsAllocation,
		localitiesAllocation,
		resultsAllocation,
	}
	for _, allocation := range allocations {
		if allocation == nil {
			for _, allocation := range allocations {
				C.free(allocation)
			}
			return []shared.ClusterHostHandle{}, false
		}
	}
	defer func() {
		for _, allocation := range allocations {
			C.free(allocation)
		}
	}()
	clear(localities)
	clear(results)

	stringAllocations := make([]unsafe.Pointer, 0, 2*len(specs))
	defer func() {
		for _, allocation := range stringAllocations {
			C.free(allocation)
		}
	}()
	for i, spec := range specs {
		var addressAllocation, hostnameAllocation unsafe.Pointer
		addresses[i], addressAllocation = copyClusterString(spec.Address)
		hostnames[i], hostnameAllocation = copyClusterString(spec.Hostname)
		stringAllocations = append(stringAllocations, addressAllocation, hostnameAllocation)
		if (len(spec.Address) > 0 && addressAllocation == nil) ||
			(len(spec.Hostname) > 0 && hostnameAllocation == nil) {
			return []shared.ClusterHostHandle{}, false
		}
		weights[i] = C.uint32_t(spec.Weight)
	}

	ok := C.envoy_dynamic_module_callback_cluster_add_hosts_with_hostnames(
		h.hostPluginPtr,
		0,
		unsafe.SliceData(addresses),
		unsafe.SliceData(hostnames),
		unsafe.SliceData(weights),
		unsafe.SliceData(localities),
		unsafe.SliceData(localities),
		unsafe.SliceData(localities),
		nil,
		0,
		C.size_t(len(specs)),
		unsafe.SliceData(results),
	)
	if !bool(ok) {
		return []shared.ClusterHostHandle{}, false
	}

	hosts := make([]shared.ClusterHostHandle, len(results))
	for i, result := range results {
		hosts[i] = shared.ClusterHostHandle(unsafe.Pointer(result))
	}
	return hosts, true
}

func (h *dymClusterHandle) UpdateHostHealth(
	host shared.ClusterHostHandle,
	health shared.ClusterHostHealth,
) bool {
	return bool(C.envoy_dynamic_module_callback_cluster_update_host_health(
		h.hostPluginPtr,
		C.envoy_dynamic_module_type_cluster_host_envoy_ptr(unsafe.Pointer(host)),
		C.envoy_dynamic_module_type_host_health(health),
	))
}

func (h *dymClusterHandle) PreInitComplete() {
	C.envoy_dynamic_module_callback_cluster_pre_init_complete(h.hostPluginPtr)
}

type dymClusterLoadBalancerContext struct {
	hostPluginPtr C.envoy_dynamic_module_type_cluster_lb_context_envoy_ptr
	worker        *clusterLoadBalancerWrapper
	operation     unsafe.Pointer
}

func (c *dymClusterLoadBalancerContext) GetDownstreamHeader(
	key string,
) (shared.UnsafeEnvoyBuffer, bool) {
	var result C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
		c.hostPluginPtr,
		stringToModuleBuffer(key),
		&result,
		0,
		nil,
	)
	runtime.KeepAlive(key)
	if !bool(ok) {
		return shared.UnsafeEnvoyBuffer{}, false
	}
	return envoyBufferToUnsafeEnvoyBuffer(result), true
}

//export envoy_dynamic_module_on_cluster_config_new
func envoy_dynamic_module_on_cluster_config_new(
	_ C.envoy_dynamic_module_type_cluster_config_envoy_ptr,
	name C.envoy_dynamic_module_type_envoy_buffer,
	config C.envoy_dynamic_module_type_envoy_buffer,
) C.envoy_dynamic_module_type_cluster_config_module_ptr {
	nameString := envoyBufferToUnsafeEnvoyBuffer(name).ToString()
	configBytes := envoyBufferToUnsafeEnvoyBuffer(config).ToBytes()
	pluginFactory, err := sdk.NewClusterFactory(nameString, configBytes)
	if err != nil || pluginFactory == nil {
		return nil
	}
	wrapper := &clusterConfigWrapper{pluginFactory: pluginFactory}
	return C.envoy_dynamic_module_type_cluster_config_module_ptr(recordClusterHandle(wrapper))
}

//export envoy_dynamic_module_on_cluster_config_destroy
func envoy_dynamic_module_on_cluster_config_destroy(
	configModulePtr C.envoy_dynamic_module_type_cluster_config_module_ptr,
) {
	wrapper := unwrapClusterHandle[clusterConfigWrapper](unsafe.Pointer(configModulePtr))
	if wrapper == nil {
		return
	}
	wrapper.pluginFactory.OnDestroy()
	removeClusterHandle(unsafe.Pointer(configModulePtr))
}

//export envoy_dynamic_module_on_cluster_new
func envoy_dynamic_module_on_cluster_new(
	configModulePtr C.envoy_dynamic_module_type_cluster_config_module_ptr,
	clusterEnvoyPtr C.envoy_dynamic_module_type_cluster_envoy_ptr,
) C.envoy_dynamic_module_type_cluster_module_ptr {
	configWrapper := unwrapClusterHandle[clusterConfigWrapper](unsafe.Pointer(configModulePtr))
	if configWrapper == nil {
		return nil
	}
	handle := &dymClusterHandle{hostPluginPtr: clusterEnvoyPtr}
	plugin := configWrapper.pluginFactory.Create(handle)
	if plugin == nil {
		return nil
	}
	wrapper := &clusterWrapper{plugin: plugin}
	return C.envoy_dynamic_module_type_cluster_module_ptr(recordClusterHandle(wrapper))
}

//export envoy_dynamic_module_on_cluster_init
func envoy_dynamic_module_on_cluster_init(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	clusterModulePtr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	wrapper := unwrapClusterHandle[clusterWrapper](unsafe.Pointer(clusterModulePtr))
	if wrapper != nil {
		wrapper.plugin.OnInit()
	}
}

//export envoy_dynamic_module_on_cluster_server_initialized
func envoy_dynamic_module_on_cluster_server_initialized(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	clusterModulePtr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	wrapper := unwrapClusterHandle[clusterWrapper](unsafe.Pointer(clusterModulePtr))
	if wrapper != nil {
		wrapper.plugin.OnServerInitialized()
	}
}

//export envoy_dynamic_module_on_cluster_destroy
func envoy_dynamic_module_on_cluster_destroy(
	clusterModulePtr C.envoy_dynamic_module_type_cluster_module_ptr,
) {
	wrapper := unwrapClusterHandle[clusterWrapper](unsafe.Pointer(clusterModulePtr))
	if wrapper == nil {
		return
	}
	wrapper.plugin.OnDestroy()
	removeClusterHandle(unsafe.Pointer(clusterModulePtr))
}

//export envoy_dynamic_module_on_cluster_lb_new
func envoy_dynamic_module_on_cluster_lb_new(
	clusterModulePtr C.envoy_dynamic_module_type_cluster_module_ptr,
	lbEnvoyPtr C.envoy_dynamic_module_type_cluster_lb_envoy_ptr,
) C.envoy_dynamic_module_type_cluster_lb_module_ptr {
	wrapper := unwrapClusterHandle[clusterWrapper](unsafe.Pointer(clusterModulePtr))
	if wrapper == nil {
		return nil
	}
	loadBalancerWrapper := &clusterLoadBalancerWrapper{
		hostPluginPtr: lbEnvoyPtr, operations: make(map[*clusterAsyncSelection]struct{}),
	}
	plugin := wrapper.plugin.NewLoadBalancer(loadBalancerWrapper)
	if plugin == nil {
		return nil
	}
	loadBalancerWrapper.plugin = plugin
	return C.envoy_dynamic_module_type_cluster_lb_module_ptr(
		recordClusterHandle(loadBalancerWrapper),
	)
}

//export envoy_dynamic_module_on_cluster_lb_destroy
func envoy_dynamic_module_on_cluster_lb_destroy(
	loadBalancerModulePtr C.envoy_dynamic_module_type_cluster_lb_module_ptr,
) {
	wrapper := unwrapClusterHandle[clusterLoadBalancerWrapper](unsafe.Pointer(loadBalancerModulePtr))
	if wrapper == nil {
		return
	}
	wrapper.mu.Lock()
	operations := make([]*clusterAsyncSelection, 0, len(wrapper.operations))
	for operation := range wrapper.operations {
		operations = append(operations, operation)
	}
	wrapper.mu.Unlock()
	for _, operation := range operations {
		operation.mu.Lock()
		operation.disabled = true
		operation.mu.Unlock()
	}
	wrapper.plugin.OnDestroy()
	if wrapper.timer != nil {
		C.envoy_dynamic_module_callback_cluster_worker_timer_delete(wrapper.timer)
	}
	removeClusterHandle(unsafe.Pointer(loadBalancerModulePtr))
}

//export envoy_dynamic_module_on_cluster_lb_choose_host
func envoy_dynamic_module_on_cluster_lb_choose_host(
	loadBalancerModulePtr C.envoy_dynamic_module_type_cluster_lb_module_ptr,
	contextEnvoyPtr C.envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
	hostOut *C.envoy_dynamic_module_type_cluster_host_envoy_ptr,
	asyncHandleOut *C.envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr,
) {
	*hostOut = nil
	*asyncHandleOut = nil

	wrapper := unwrapClusterHandle[clusterLoadBalancerWrapper](unsafe.Pointer(loadBalancerModulePtr))
	if wrapper == nil {
		return
	}

	if contextEnvoyPtr == nil {
		host := wrapper.plugin.ChooseHost(nil)
		*hostOut = C.envoy_dynamic_module_type_cluster_host_envoy_ptr(unsafe.Pointer(host))
		return
	}
	context := &dymClusterLoadBalancerContext{hostPluginPtr: contextEnvoyPtr, worker: wrapper}
	host := wrapper.plugin.ChooseHost(context)
	if context.operation != nil {
		*asyncHandleOut = C.envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr(context.operation)
	} else {
		*hostOut = C.envoy_dynamic_module_type_cluster_host_envoy_ptr(unsafe.Pointer(host))
	}
}

func (h *dymClusterHandle) RemoveHosts(hosts []shared.ClusterHostHandle) int {
	if len(hosts) == 0 {
		return 0
	}
	values, allocation := allocateClusterSlice[C.envoy_dynamic_module_type_cluster_host_envoy_ptr](len(hosts))
	if allocation == nil {
		return 0
	}
	defer C.free(allocation)
	for i, host := range hosts {
		values[i] = C.envoy_dynamic_module_type_cluster_host_envoy_ptr(unsafe.Pointer(host))
	}
	return int(C.envoy_dynamic_module_callback_cluster_remove_hosts(h.hostPluginPtr, unsafe.SliceData(values), C.size_t(len(values))))
}

type clusterScheduler struct {
	mu      sync.Mutex
	pointer C.envoy_dynamic_module_type_cluster_scheduler_module_ptr
}

func (h *dymClusterHandle) NewScheduler() shared.ClusterScheduler {
	return &clusterScheduler{pointer: C.envoy_dynamic_module_callback_cluster_scheduler_new(h.hostPluginPtr)}
}

func (s *clusterScheduler) Schedule(event uint64) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.pointer == nil {
		return false
	}
	// This ABI only posts an event; it cannot invoke OnScheduled inline.
	C.envoy_dynamic_module_callback_cluster_scheduler_commit(s.pointer, C.uint64_t(event))
	return true
}

func (s *clusterScheduler) Close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.pointer != nil {
		C.envoy_dynamic_module_callback_cluster_scheduler_delete(s.pointer)
		s.pointer = nil
	}
}

//export envoy_dynamic_module_on_cluster_scheduled
func envoy_dynamic_module_on_cluster_scheduled(
	_ C.envoy_dynamic_module_type_cluster_envoy_ptr,
	clusterModulePtr C.envoy_dynamic_module_type_cluster_module_ptr, event C.uint64_t,
) {
	wrapper := unwrapClusterHandle[clusterWrapper](unsafe.Pointer(clusterModulePtr))
	if observer, ok := wrapper.plugin.(shared.ClusterScheduled); ok {
		observer.OnScheduled(uint64(event))
	}
}

func (w *clusterLoadBalancerWrapper) Hosts() []shared.ClusterHostHandle {
	count := C.envoy_dynamic_module_callback_cluster_lb_get_hosts_count(w.hostPluginPtr, 0)
	hosts := make([]shared.ClusterHostHandle, int(count))
	for i := range hosts {
		hosts[i] = shared.ClusterHostHandle(C.envoy_dynamic_module_callback_cluster_lb_get_host(w.hostPluginPtr, 0, C.size_t(i)))
	}
	return hosts
}

//export envoy_dynamic_module_on_cluster_lb_on_host_membership_update
func envoy_dynamic_module_on_cluster_lb_on_host_membership_update(
	_ C.envoy_dynamic_module_type_cluster_lb_envoy_ptr,
	lbModulePtr C.envoy_dynamic_module_type_cluster_lb_module_ptr, added C.size_t, removed C.size_t,
) {
	wrapper := unwrapClusterHandle[clusterLoadBalancerWrapper](unsafe.Pointer(lbModulePtr))
	if observer, ok := wrapper.plugin.(shared.ClusterMembershipObserver); ok {
		observer.OnHostMembershipUpdate()
	}
}

func (c *dymClusterLoadBalancerContext) GetFilterState(key string) (string, bool) {
	var result C.envoy_dynamic_module_type_envoy_buffer
	ok := C.envoy_dynamic_module_callback_cluster_lb_context_get_filter_state_bytes(c.hostPluginPtr, stringToModuleBuffer(key), &result)
	runtime.KeepAlive(key)
	if !bool(ok) {
		return "", false
	}
	return string(envoyBufferToUnsafeEnvoyBuffer(result).ToBytes()), true
}

type clusterAsyncSelection struct {
	mu        sync.Mutex
	disabled  bool
	posted    bool
	context   C.envoy_dynamic_module_type_cluster_lb_context_envoy_ptr
	worker    *clusterLoadBalancerWrapper
	onDestroy func()
}

func (c *dymClusterLoadBalancerContext) BeginAsync(onDestroy func()) (shared.ClusterAsyncSelection, bool) {
	if c.operation != nil {
		return nil, false
	}
	w := c.worker
	if w.timer == nil {
		w.timer = C.envoy_dynamic_module_callback_cluster_worker_timer_new(w.hostPluginPtr)
		if w.timer == nil {
			return nil, false
		}
	}
	operation := &clusterAsyncSelection{context: c.hostPluginPtr, worker: w, onDestroy: onDestroy}
	w.mu.Lock()
	w.operations[operation] = struct{}{}
	w.mu.Unlock()
	c.operation = recordClusterHandle(operation)
	return operation, true
}

func (o *clusterAsyncSelection) Complete(host shared.ClusterHostHandle, details string) bool {
	o.mu.Lock()
	defer o.mu.Unlock()
	if o.disabled || o.posted {
		return false
	}
	o.posted = true
	// BeginAsync proved worker dispatcher availability. Completion therefore only posts; the
	// final hook cannot re-enter this operation inline. No catalog or worker lock is held.
	C.envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(o.worker.hostPluginPtr,
		o.context, C.envoy_dynamic_module_type_cluster_host_envoy_ptr(unsafe.Pointer(host)), stringToModuleBuffer(details))
	runtime.KeepAlive(details)
	return true
}

//export envoy_dynamic_module_on_cluster_lb_cancel_host_selection
func envoy_dynamic_module_on_cluster_lb_cancel_host_selection(
	_ C.envoy_dynamic_module_type_cluster_lb_module_ptr,
	asyncHandle C.envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr,
) {
	operation := unwrapClusterHandle[clusterAsyncSelection](unsafe.Pointer(asyncHandle))
	operation.mu.Lock()
	operation.disabled = true
	callback := operation.onDestroy
	operation.onDestroy = nil
	operation.mu.Unlock()
	operation.worker.mu.Lock()
	delete(operation.worker.operations, operation)
	operation.worker.mu.Unlock()
	if callback != nil {
		callback()
	}
	removeClusterHandle(unsafe.Pointer(asyncHandle))
}
