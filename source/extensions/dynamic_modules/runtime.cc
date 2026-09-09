#include "source/extensions/dynamic_modules/runtime.h"

#include <algorithm>
#include <array>

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

using Result = envoy_dynamic_module_type_runtime_read_result;
using Kind = envoy_dynamic_module_type_runtime_value_kind;
using Value = envoy_dynamic_module_type_runtime_value;

absl::string_view view(envoy_dynamic_module_type_module_buffer buffer) {
  return buffer.length == 0 ? absl::string_view{} : absl::string_view(buffer.ptr, buffer.length);
}

} // namespace

Result readRuntimeBatch(Runtime::Loader& loader,
                        const envoy_dynamic_module_type_runtime_request* requests,
                        size_t requests_size,
                        const envoy_dynamic_module_type_runtime_condition* condition, Value* values,
                        char* strings, size_t strings_capacity, size_t* strings_size_out) {
  if (strings_size_out == nullptr) {
    return Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument;
  }
  *strings_size_out = 0;
  if ((requests_size != 0 && (requests == nullptr || values == nullptr)) ||
      (strings_capacity != 0 && strings == nullptr)) {
    return Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument;
  }
  if (requests_size > ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_KEYS ||
      strings_capacity > ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_OUTPUT_BYTES) {
    return Result::envoy_dynamic_module_type_runtime_read_result_LimitExceeded;
  }
  size_t input_bytes = 0;
  const auto validate_buffer = [&input_bytes](envoy_dynamic_module_type_module_buffer buffer) {
    if (buffer.ptr == nullptr && buffer.length != 0) {
      return Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument;
    }
    if (buffer.length > ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_INPUT_BYTES - input_bytes) {
      return Result::envoy_dynamic_module_type_runtime_read_result_LimitExceeded;
    }
    input_bytes += buffer.length;
    return Result::envoy_dynamic_module_type_runtime_read_result_Ok;
  };
  for (size_t i = 0; i < requests_size; ++i) {
    const auto result = validate_buffer(requests[i].key);
    if (result != Result::envoy_dynamic_module_type_runtime_read_result_Ok) {
      return result;
    }
    switch (requests[i].kind) {
    case Kind::envoy_dynamic_module_type_runtime_value_kind_Boolean:
      break;
    case Kind::envoy_dynamic_module_type_runtime_value_kind_Integer:
    case Kind::envoy_dynamic_module_type_runtime_value_kind_Double:
    case Kind::envoy_dynamic_module_type_runtime_value_kind_String:
      if (Runtime::isRuntimeFeature(view(requests[i].key))) {
        return Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument;
      }
      break;
    default:
      return Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument;
    }
  }
  if (condition != nullptr) {
    for (const auto buffer : {condition->key, condition->expected}) {
      const auto result = validate_buffer(buffer);
      if (result != Result::envoy_dynamic_module_type_runtime_read_result_Ok) {
        return result;
      }
    }
    if (condition->key.length == 0 || Runtime::isRuntimeFeature(view(condition->key))) {
      return Result::envoy_dynamic_module_type_runtime_read_result_InvalidArgument;
    }
  }

  // Retain exactly one snapshot, including while sizing and copying output. A later runtime
  // publication must not change the values in this batch or invalidate its borrowed strings.
  auto snapshot = loader.threadsafeSnapshot();
  if (snapshot == nullptr) {
    return Result::envoy_dynamic_module_type_runtime_read_result_Unavailable;
  }
  if (condition != nullptr) {
    const auto current = snapshot->get(view(condition->key));
    if (current.has_value() && current->get() == view(condition->expected)) {
      return Result::envoy_dynamic_module_type_runtime_read_result_Unchanged;
    }
  }

  std::array<Value, ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_KEYS> pending{};
  std::array<absl::string_view, ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_KEYS> borrowed;
  size_t required = 0;
  for (size_t i = 0; i < requests_size; ++i) {
    const auto& request = requests[i];
    auto& value = pending[i];
    const auto key = view(request.key);
    switch (request.kind) {
    case Kind::envoy_dynamic_module_type_runtime_value_kind_Boolean:
      value.boolean_value = snapshot->getBoolean(key, request.fallback_boolean);
      break;
    case Kind::envoy_dynamic_module_type_runtime_value_kind_Integer:
      value.integer_value = snapshot->getInteger(key, request.fallback_integer);
      break;
    case Kind::envoy_dynamic_module_type_runtime_value_kind_Double:
      value.double_value = snapshot->getDouble(key, request.fallback_double);
      break;
    case Kind::envoy_dynamic_module_type_runtime_value_kind_String: {
      const auto raw = snapshot->get(key);
      value.string_present = raw.has_value();
      if (raw.has_value()) {
        borrowed[i] = raw->get();
        if (borrowed[i].size() > ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_OUTPUT_BYTES - required) {
          return Result::envoy_dynamic_module_type_runtime_read_result_LimitExceeded;
        }
        value.string_offset = required;
        value.string_length = borrowed[i].size();
        required += borrowed[i].size();
      }
      break;
    }
    }
  }
  *strings_size_out = required;
  if (required > strings_capacity) {
    return Result::envoy_dynamic_module_type_runtime_read_result_BufferTooSmall;
  }
  for (size_t i = 0; i < requests_size; ++i) {
    if (pending[i].string_length != 0) {
      std::copy(borrowed[i].begin(), borrowed[i].end(), strings + pending[i].string_offset);
    }
    values[i] = pending[i];
  }
  return Result::envoy_dynamic_module_type_runtime_read_result_Ok;
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
