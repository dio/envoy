#pragma once

#include "envoy/config/filter/http/wasm/v2/wasm.pb.h"
#include "envoy/http/filter.h"

#include "extensions/filters/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

struct FilterConfig {
  FilterConfig(const envoy::config::filter::http::wasm::v2::Wasm& config)
      : program_{std::make_shared<Filters::Common::Wasm::Program>(config.wasm_file())} {}

  const std::shared_ptr<Filters::Common::Wasm::Program> program_;
};
typedef std::shared_ptr<FilterConfig> FilterConfigPtr;

class WasmFilter : public Http::StreamDecoderFilter {
public:
  WasmFilter(FilterConfigPtr config) : config_(config) {}
  ~WasmFilter() {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callback) override {
    decoder_callbacks_ = &callback;
  }

private:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  FilterConfigPtr config_;
};

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy