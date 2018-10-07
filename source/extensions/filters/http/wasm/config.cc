#include "extensions/filters/http/wasm/config.h"

#include "envoy/config/filter/http/wasm/v2alpha/wasm.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

Http::FilterFactoryCb WasmFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::wasm::v2::Wasm& proto_config, const std::string&,
    Server::Configuration::FactoryContext&) {
  FilterConfigPtr config = std::make_shared<FilterConfig>(FilterConfig(proto_config));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{new WasmFilter(config)});
  };
}

static Registry::RegisterFactory<WasmFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    registered_;
} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy