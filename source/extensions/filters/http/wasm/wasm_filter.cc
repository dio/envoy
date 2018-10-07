#include "extensions/filters/http/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

Http::FilterHeadersStatus WasmFilter::decodeHeaders(Http::HeaderMap&, bool) {
  config_->program_->run();
  return Http::FilterHeadersStatus::Continue;
}

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy