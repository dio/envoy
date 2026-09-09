//! Copied reads from one Envoy runtime snapshot. Call only from the owning Envoy hook.

use crate::abi;
use crate::str_to_module_buffer;

/// A typed read and its fallback. Bytes has an explicit missing result instead of a fallback.
#[derive(Clone, Copy, Debug)]
pub enum RuntimeDefault {
  Boolean(bool),
  Integer(u64),
  Double(f64),
  Bytes,
}

/// One key to read from the merged runtime.
#[derive(Clone, Copy, Debug)]
pub struct RuntimeRequest<'a> {
  pub key: &'a str,
  pub default: RuntimeDefault,
}

/// Skip the batch if this application's revision is present and equal to the accepted value.
/// The application must change the revision for every relevant payload change or removal.
/// This condition neither authenticates the payload nor renews a freshness lease.
#[derive(Clone, Copy, Debug)]
pub struct RuntimeCondition<'a> {
  pub key: &'a str,
  pub expected: &'a str,
}

/// A copied runtime value. Bytes are owned independently of the scratch arena and Envoy.
#[derive(Debug, PartialEq)]
pub enum RuntimeValue {
  Boolean(bool),
  Integer(u64),
  Double(f64),
  Bytes(Option<Vec<u8>>),
}

/// A complete batch, or an application condition that matched without reading the payload.
#[derive(Debug, PartialEq)]
pub enum RuntimeBatch {
  Values(Vec<RuntimeValue>),
  Unchanged,
}

/// Whole-call failure. On BufferTooSmall, retry the entire batch with bounded scratch capacity.
/// A retry may observe a newer snapshot; do not combine results from separate attempts.
#[derive(Debug, PartialEq)]
pub enum RuntimeReadError {
  BufferTooSmall(usize),
  LimitExceeded,
  InvalidArgument,
  Unavailable,
  /// The SDK handle does not implement this capability, for example an older custom mock.
  Unsupported,
}

pub(crate) type ReadCallback =
  unsafe extern "C" fn(
    *mut std::ffi::c_void,
    *const abi::envoy_dynamic_module_type_runtime_request,
    usize,
    *const abi::envoy_dynamic_module_type_runtime_condition,
    *mut abi::envoy_dynamic_module_type_runtime_value,
    *mut std::ffi::c_char,
    usize,
    *mut usize,
  ) -> abi::envoy_dynamic_module_type_runtime_read_result;

// The caller guarantees a live host handle on its owning Envoy execution context. Inputs and
// output buffers remain alive for the synchronous call; the host retains none of their pointers.
pub(crate) unsafe fn read_batch(
  host: *mut std::ffi::c_void,
  callback: ReadCallback,
  requests: &[RuntimeRequest<'_>],
  condition: Option<RuntimeCondition<'_>>,
  scratch: &mut [u8],
) -> Result<RuntimeBatch, RuntimeReadError> {
  use abi::envoy_dynamic_module_type_runtime_read_result as Status;
  use abi::envoy_dynamic_module_type_runtime_value_kind as Kind;
  if requests.len() > abi::ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_KEYS as usize
    || scratch.len() > abi::ENVOY_DYNAMIC_MODULE_RUNTIME_MAX_OUTPUT_BYTES as usize
  {
    return Err(RuntimeReadError::LimitExceeded);
  }
  let raw: Vec<_> = requests
    .iter()
    .map(|request| {
      let mut result = abi::envoy_dynamic_module_type_runtime_request {
        key: str_to_module_buffer(request.key),
        kind: Kind::String,
        fallback_boolean: false,
        fallback_integer: 0,
        fallback_double: 0.0,
      };
      match request.default {
        RuntimeDefault::Boolean(v) => {
          result.kind = Kind::Boolean;
          result.fallback_boolean = v;
        },
        RuntimeDefault::Integer(v) => {
          result.kind = Kind::Integer;
          result.fallback_integer = v;
        },
        RuntimeDefault::Double(v) => {
          result.kind = Kind::Double;
          result.fallback_double = v;
        },
        RuntimeDefault::Bytes => {},
      }
      result
    })
    .collect();
  let condition = condition.map(|c| abi::envoy_dynamic_module_type_runtime_condition {
    key: str_to_module_buffer(c.key),
    expected: str_to_module_buffer(c.expected),
  });
  // These output types contain only primitive values. Zero initialization is valid.
  let mut values: Vec<abi::envoy_dynamic_module_type_runtime_value> =
    (0..requests.len()).map(|_| std::mem::zeroed()).collect();
  let mut size = 0;
  let status = callback(
    host,
    raw.as_ptr(),
    raw.len(),
    condition.as_ref().map_or(std::ptr::null(), |c| c),
    values.as_mut_ptr(),
    scratch.as_mut_ptr().cast(),
    scratch.len(),
    &mut size,
  );
  match status {
    Status::Unchanged => return Ok(RuntimeBatch::Unchanged),
    Status::BufferTooSmall => return Err(RuntimeReadError::BufferTooSmall(size)),
    Status::LimitExceeded => return Err(RuntimeReadError::LimitExceeded),
    Status::InvalidArgument => return Err(RuntimeReadError::InvalidArgument),
    Status::Unavailable => return Err(RuntimeReadError::Unavailable),
    Status::Ok => {},
  }
  if size > scratch.len() {
    return Err(RuntimeReadError::InvalidArgument);
  }
  let mut result = Vec::with_capacity(values.len());
  for (request, value) in requests.iter().zip(values) {
    result.push(match request.default {
      RuntimeDefault::Boolean(_) => RuntimeValue::Boolean(value.boolean_value),
      RuntimeDefault::Integer(_) => RuntimeValue::Integer(value.integer_value),
      RuntimeDefault::Double(_) => RuntimeValue::Double(value.double_value),
      RuntimeDefault::Bytes => RuntimeValue::Bytes(if value.string_present {
        let end = value
          .string_offset
          .checked_add(value.string_length)
          .filter(|end| *end <= size)
          .ok_or(RuntimeReadError::InvalidArgument)?;
        Some(scratch[value.string_offset..end].to_vec())
      } else {
        None
      }),
    });
  }
  Ok(RuntimeBatch::Values(result))
}

#[cfg(test)]
mod tests {
  use super::*;
  use crate::http::{EnvoyHttpFilter, EnvoyHttpFilterConfig};

  unsafe extern "C" fn callback(
    _host: *mut std::ffi::c_void,
    requests: *const abi::envoy_dynamic_module_type_runtime_request,
    count: usize,
    condition: *const abi::envoy_dynamic_module_type_runtime_condition,
    values: *mut abi::envoy_dynamic_module_type_runtime_value,
    strings: *mut std::ffi::c_char,
    capacity: usize,
    size: *mut usize,
  ) -> abi::envoy_dynamic_module_type_runtime_read_result {
    use abi::envoy_dynamic_module_type_runtime_read_result as Status;
    *size = 0;
    if !condition.is_null() {
      return Status::Unchanged;
    }
    *size = 3;
    if capacity < 3 {
      return Status::BufferTooSmall;
    }
    std::ptr::copy_nonoverlapping(b"a\0b".as_ptr(), strings.cast(), 3);
    for i in 0..count {
      let request = &*requests.add(i);
      let value = &mut *values.add(i);
      value.boolean_value = request.fallback_boolean;
      value.integer_value = request.fallback_integer;
      value.double_value = request.fallback_double;
      value.string_present = true;
      value.string_offset = 0;
      value.string_length = 3;
    }
    Status::Ok
  }

  macro_rules! host_stub {
    ($name:ident) => {
      #[no_mangle]
      unsafe extern "C" fn $name(
        host: *mut std::ffi::c_void,
        requests: *const abi::envoy_dynamic_module_type_runtime_request,
        count: usize,
        condition: *const abi::envoy_dynamic_module_type_runtime_condition,
        values: *mut abi::envoy_dynamic_module_type_runtime_value,
        strings: *mut std::ffi::c_char,
        capacity: usize,
        size: *mut usize,
      ) -> abi::envoy_dynamic_module_type_runtime_read_result {
        callback(
          host, requests, count, condition, values, strings, capacity, size,
        )
      }
    };
  }

  host_stub!(envoy_dynamic_module_callback_http_filter_runtime_read_batch);
  host_stub!(envoy_dynamic_module_callback_http_filter_config_runtime_read_batch);
  host_stub!(envoy_dynamic_module_callback_cluster_runtime_read_batch);
  host_stub!(envoy_dynamic_module_callback_cluster_lb_runtime_read_batch);

  #[test]
  fn wrappers_copy_values_and_report_whole_batch_retry() {
    let filter = crate::http::EnvoyHttpFilterImpl {
      raw_ptr: std::ptr::null_mut(),
    };
    let config = crate::http::EnvoyHttpFilterConfigImpl {
      raw_ptr: std::ptr::null_mut(),
    };
    let requests = [
      RuntimeRequest {
        key: "blob",
        default: RuntimeDefault::Bytes,
      },
      RuntimeRequest {
        key: "flag",
        default: RuntimeDefault::Boolean(true),
      },
      RuntimeRequest {
        key: "count",
        default: RuntimeDefault::Integer(42),
      },
      RuntimeRequest {
        key: "ratio",
        default: RuntimeDefault::Double(1.5),
      },
    ];
    assert_eq!(
      filter.runtime_read_batch(&requests, None, &mut []),
      Err(RuntimeReadError::BufferTooSmall(3))
    );
    let mut scratch = [0; 3];
    let result = config
      .runtime_read_batch(&requests, None, &mut scratch)
      .unwrap();
    scratch.fill(0);
    assert_eq!(
      result,
      RuntimeBatch::Values(vec![
        RuntimeValue::Bytes(Some(b"a\0b".to_vec())),
        RuntimeValue::Boolean(true),
        RuntimeValue::Integer(42),
        RuntimeValue::Double(1.5),
      ])
    );
    assert_eq!(
      filter.runtime_read_batch(
        &requests,
        Some(RuntimeCondition {
          key: "rev",
          expected: "r1"
        }),
        &mut []
      ),
      Ok(RuntimeBatch::Unchanged)
    );
  }

  #[test]
  fn refuses_oversized_request_before_crossing_abi() {
    let filter = crate::http::EnvoyHttpFilterImpl {
      raw_ptr: std::ptr::null_mut(),
    };
    let requests = vec![
      RuntimeRequest {
        key: "x",
        default: RuntimeDefault::Bytes
      };
      65
    ];
    assert_eq!(
      filter.runtime_read_batch(&requests, None, &mut []),
      Err(RuntimeReadError::LimitExceeded)
    );
  }
}
