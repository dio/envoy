#include "extensions/filters/http/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

Http::FilterHeadersStatus WasmFilter::decodeHeaders(Http::HeaderMap&, bool) {
  // TODO(dio): Send the input, intercept and send the result back to the client.
  config_->program_->readAndRunModule();
  return Http::FilterHeadersStatus::Continue;
}

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
