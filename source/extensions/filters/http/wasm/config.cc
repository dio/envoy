#include "envoy/registry/registry.h"

#include "extensions/filters/http/wasm/config.h"
#include "extensions/filters/http/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

Http::FilterFactoryCb BasicAuthFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::wasm::v2::Wasm& proto_config, const std::string&,
    Server::Configuration::FactoryContext&) {
  BasicAuthFilterConfigPtr config =
      std::make_shared<BasicAuthFilterConfig>(BasicAuthFilterConfig(proto_config));

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new BasicAuthFilter(config)});
  };
}

static Registry::RegisterFactory<BasicAuthFilterConfigFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    registered_;

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy