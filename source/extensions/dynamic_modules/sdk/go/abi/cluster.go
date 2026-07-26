package abi

/*
#include <stdbool.h>
#include <stdint.h>
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
	plugin shared.ClusterLoadBalancer
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

func (h *dymClusterHandle) AddHosts(
	specs []shared.ClusterHostSpec,
) ([]shared.ClusterHostHandle, bool) {
	if len(specs) == 0 {
		return []shared.ClusterHostHandle{}, true
	}

	addressStrings := make([]string, len(specs))
	hostnameStrings := make([]string, len(specs))
	addresses := make([]C.envoy_dynamic_module_type_module_buffer, len(specs))
	hostnames := make([]C.envoy_dynamic_module_type_module_buffer, len(specs))
	weights := make([]C.uint32_t, len(specs))
	localities := make([]C.envoy_dynamic_module_type_module_buffer, len(specs))
	results := make([]C.envoy_dynamic_module_type_cluster_host_envoy_ptr, len(specs))

	for i, spec := range specs {
		addressStrings[i] = spec.Address
		hostnameStrings[i] = spec.Hostname
		addresses[i] = stringToModuleBuffer(addressStrings[i])
		hostnames[i] = stringToModuleBuffer(hostnameStrings[i])
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
	runtime.KeepAlive(specs)
	runtime.KeepAlive(addressStrings)
	runtime.KeepAlive(hostnameStrings)
	runtime.KeepAlive(addresses)
	runtime.KeepAlive(hostnames)
	runtime.KeepAlive(weights)
	runtime.KeepAlive(localities)
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
	_ C.envoy_dynamic_module_type_cluster_lb_envoy_ptr,
) C.envoy_dynamic_module_type_cluster_lb_module_ptr {
	wrapper := unwrapClusterHandle[clusterWrapper](unsafe.Pointer(clusterModulePtr))
	if wrapper == nil {
		return nil
	}
	plugin := wrapper.plugin.NewLoadBalancer()
	if plugin == nil {
		return nil
	}
	loadBalancerWrapper := &clusterLoadBalancerWrapper{plugin: plugin}
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
	wrapper.plugin.OnDestroy()
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

	var context shared.ClusterLoadBalancerContext
	if contextEnvoyPtr != nil {
		context = &dymClusterLoadBalancerContext{hostPluginPtr: contextEnvoyPtr}
	}
	host := wrapper.plugin.ChooseHost(context)
	*hostOut = C.envoy_dynamic_module_type_cluster_host_envoy_ptr(unsafe.Pointer(host))
}
