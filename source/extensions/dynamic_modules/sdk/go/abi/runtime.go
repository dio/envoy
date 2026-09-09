package abi

/*
#include <stdlib.h>
#include "../../../abi/abi.h"
*/
import "C"

import (
	"unsafe"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type runtimeReadCallback func(*C.envoy_dynamic_module_type_runtime_request, C.size_t,
	*C.envoy_dynamic_module_type_runtime_condition, *C.envoy_dynamic_module_type_runtime_value,
	*C.char, C.size_t, *C.size_t) C.envoy_dynamic_module_type_runtime_read_result

func readRuntimeBatch(requests []shared.RuntimeRequest, condition *shared.RuntimeCondition, scratch []byte,
	callback runtimeReadCallback) ([]shared.RuntimeValue, shared.RuntimeReadStatus, int) {
	if len(requests) > C.ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_KEYS || len(scratch) > C.ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_OUTPUT_BYTES {
		return nil, shared.RuntimeReadLimitExceeded, 0
	}
	inputBytes := 0
	addSize := func(n int) bool {
		if n > C.ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_INPUT_BYTES-inputBytes {
			return false
		}
		inputBytes += n
		return true
	}
	for _, request := range requests {
		if request.Kind > shared.RuntimeString {
			return nil, shared.RuntimeReadInvalidArgument, 0
		}
		if !addSize(len(request.Key)) {
			return nil, shared.RuntimeReadLimitExceeded, 0
		}
	}
	if condition != nil && (!addSize(len(condition.Key)) || !addSize(len(condition.Expected))) {
		return nil, shared.RuntimeReadLimitExceeded, 0
	}
	// C-owned descriptor/key storage prevents arrays of Go pointers from crossing the cgo boundary.
	keys := C.malloc(C.size_t(inputBytes + 1))
	defer C.free(keys)
	keyBytes := unsafe.Slice((*byte)(keys), inputBytes+1)
	offset := 0
	buffer := func(value string) C.envoy_dynamic_module_type_module_buffer {
		start := offset
		offset += copy(keyBytes[offset:], value)
		return C.envoy_dynamic_module_type_module_buffer{ptr: (*C.char)(unsafe.Add(keys, start)), length: C.size_t(len(value))}
	}
	requestStorage := C.calloc(C.size_t(len(requests)+1), C.sizeof_envoy_dynamic_module_type_runtime_request)
	defer C.free(requestStorage)
	if requestStorage == nil {
		return nil, shared.RuntimeReadUnavailable, 0
	}
	raw := unsafe.Slice((*C.envoy_dynamic_module_type_runtime_request)(requestStorage), len(requests))
	for i, request := range requests {
		raw[i].key = buffer(request.Key)
		raw[i].kind = C.envoy_dynamic_module_type_runtime_value_kind(request.Kind)
		raw[i].fallback_boolean = C.bool(request.FallbackBoolean)
		raw[i].fallback_integer = C.uint64_t(request.FallbackInteger)
		raw[i].fallback_double = C.double(request.FallbackDouble)
	}
	var conditionValue C.envoy_dynamic_module_type_runtime_condition
	var conditionPtr *C.envoy_dynamic_module_type_runtime_condition
	if condition != nil {
		conditionValue.key = buffer(condition.Key)
		conditionValue.expected = buffer(condition.Expected)
		conditionPtr = &conditionValue
	}
	valueStorage := C.calloc(C.size_t(len(requests)+1), C.sizeof_envoy_dynamic_module_type_runtime_value)
	defer C.free(valueStorage)
	if valueStorage == nil {
		return nil, shared.RuntimeReadUnavailable, 0
	}
	var size C.size_t
	status := shared.RuntimeReadStatus(callback((*C.envoy_dynamic_module_type_runtime_request)(requestStorage),
		C.size_t(len(requests)), conditionPtr, (*C.envoy_dynamic_module_type_runtime_value)(valueStorage),
		(*C.char)(unsafe.Pointer(unsafe.SliceData(scratch))), C.size_t(len(scratch)), &size))
	if status != shared.RuntimeReadOK {
		return nil, status, int(size)
	}
	if uint64(size) > uint64(len(scratch)) {
		return nil, shared.RuntimeReadInvalidArgument, 0
	}
	values := unsafe.Slice((*C.envoy_dynamic_module_type_runtime_value)(valueStorage), len(requests))
	result := make([]shared.RuntimeValue, len(requests))
	for i, value := range values {
		result[i].Boolean = bool(value.boolean_value)
		result[i].Integer = uint64(value.integer_value)
		result[i].Double = float64(value.double_value)
		result[i].StringPresent = bool(value.string_present)
		if requests[i].Kind == shared.RuntimeString && result[i].StringPresent {
			if value.string_offset > size || value.string_length > size-value.string_offset {
				return nil, shared.RuntimeReadInvalidArgument, 0
			}
			start, end := int(value.string_offset), int(value.string_offset+value.string_length)
			result[i].String = string(scratch[start:end])
		}
	}
	return result, status, int(size)
}

func (h *dymHttpFilterHandle) ReadRuntimeBatch(requests []shared.RuntimeRequest, condition *shared.RuntimeCondition,
	scratch []byte) ([]shared.RuntimeValue, shared.RuntimeReadStatus, int) {
	return readRuntimeBatch(requests, condition, scratch, func(requests *C.envoy_dynamic_module_type_runtime_request,
		count C.size_t, condition *C.envoy_dynamic_module_type_runtime_condition, values *C.envoy_dynamic_module_type_runtime_value,
		strings *C.char, capacity C.size_t, size *C.size_t) C.envoy_dynamic_module_type_runtime_read_result {
		return C.envoy_dynamic_module_callback_http_filter_runtime_read_batch(h.hostPluginPtr, requests, count,
			condition, values, strings, capacity, size)
	})
}

func (h *dymConfigHandle) ReadRuntimeBatch(requests []shared.RuntimeRequest, condition *shared.RuntimeCondition,
	scratch []byte) ([]shared.RuntimeValue, shared.RuntimeReadStatus, int) {
	return readRuntimeBatch(requests, condition, scratch, func(requests *C.envoy_dynamic_module_type_runtime_request,
		count C.size_t, condition *C.envoy_dynamic_module_type_runtime_condition, values *C.envoy_dynamic_module_type_runtime_value,
		strings *C.char, capacity C.size_t, size *C.size_t) C.envoy_dynamic_module_type_runtime_read_result {
		return C.envoy_dynamic_module_callback_http_filter_config_runtime_read_batch(h.hostConfigPtr, requests, count,
			condition, values, strings, capacity, size)
	})
}

var _ shared.RuntimeReader = (*dymHttpFilterHandle)(nil)
var _ shared.RuntimeReader = (*dymConfigHandle)(nil)
