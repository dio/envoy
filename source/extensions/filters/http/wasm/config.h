#pragma once

#include "envoy/config/filter/http/wasm/v2alpha/wasm.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {
class WasmFilterConfig : public Common::FactoryBase<envoy::config::filter::http::wasm::v2::Wasm> {
public:
  WasmFilterConfig() : FactoryBase(HttpFilterNames::get().Wasm) {}

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
