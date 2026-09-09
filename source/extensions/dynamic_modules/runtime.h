#pragma once

#include "envoy/runtime/runtime.h"

#include "source/extensions/dynamic_modules/abi/abi.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// Shared implementation for role-specific runtime callbacks. Callers keep the loader alive for
// this call. All returned values are copied; no snapshot or host ownership escapes the callback.
envoy_dynamic_module_type_runtime_read_result
readRuntimeBatch(Runtime::Loader& loader, const envoy_dynamic_module_type_runtime_request* requests,
                 size_t requests_size, const envoy_dynamic_module_type_runtime_condition* condition,
                 envoy_dynamic_module_type_runtime_value* values, char* strings,
                 size_t strings_capacity, size_t* strings_size_out);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
