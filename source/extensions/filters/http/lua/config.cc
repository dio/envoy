#include "extensions/filters/http/lua/config.h"

#include "envoy/config/filter/http/lua/v2/lua.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/lua/lua_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

const std::vector<Filters::Common::Lua::LuaCode>
getLuaCodes(const envoy::config::filter::http::lua::v2::Lua& config) {
  if (config.inline_codes().empty()) {
    return {Filters::Common::Lua::LuaCode{"global", config.inline_code()}};
  }

  std::vector<Filters::Common::Lua::LuaCode> codes{
      {Filters::Common::Lua::LuaCode{"global", config.inline_code()}}};
  codes.reserve(config.inline_codes().size() + 1);
  for (const auto& code : config.inline_codes()) {
    codes.push_back(Filters::Common::Lua::LuaCode{code.first, code.second});
  }
  return codes;
}

Http::FilterFactoryCb LuaFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::lua::v2::Lua& proto_config, const std::string&,
    Server::Configuration::FactoryContext& context) {
  FilterConfigConstSharedPtr filter_config(
      new FilterConfig{getLuaCodes(proto_config), context.threadLocal(), context.clusterManager()});
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(filter_config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
LuaFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::config::filter::http::lua::v2::LuaPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfigPerRoute>(proto_config);
}

/**
 * Static registration for the Lua filter. @see RegisterFactory.
 */
REGISTER_FACTORY(LuaFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
