#include <string>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/listener/text_protocol_inspector/text_protocol_inspector.h"
#include "extensions/filters/listener/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TextProtocolInspector {

/**
 * Config registration for the text protocol inspector filter. @see NamedNetworkFilterConfigFactory.
 */
class TextProtocolInspectorConfigFactory
    : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::ListenerFactoryContext& context) override {
    ConfigSharedPtr config(new Config(context.scope()));
    return [config](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(std::make_unique<Filter>(config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Envoy::ProtobufWkt::Empty>();
  }

  std::string name() override { return ListenerFilterNames::get().TextProtocolInspector; }
};

/**
 * Static registration for the text protocol inspector filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<TextProtocolInspectorConfigFactory,
                                 Server::Configuration::NamedListenerFilterConfigFactory>
    registered_;

} // namespace TextProtocolInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
