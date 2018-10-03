#pragma once

#include "envoy/config/filter/http/wasm/v2/wasm.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

static const std::string& BASIC_AUTH_FILTER() {
  // TODO(dio): Should move this to a dedicated well_known_names.h header file.
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.wasm");
}

class BasicAuthFilterConfigFactory : public Common::FactoryBase<envoy::config::filter::http::wasm::v2::Wasm> {
public:
  BasicAuthFilterConfigFactory() : FactoryBase(BASIC_AUTH_FILTER()) {}

private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const envoy::config::filter::http::wasm::v2::Wasm& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy