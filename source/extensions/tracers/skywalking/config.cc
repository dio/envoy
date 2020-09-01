#include "extensions/tracers/skywalking/config.h"

#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/skywalking/skywalking_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

SkyWalkingTracerFactory::SkyWalkingTracerFactory() : FactoryBase("envoy.tracers.skywalking") {}

Tracing::HttpTracerSharedPtr SkyWalkingTracerFactory::createHttpTracerTyped(
    const envoy::config::trace::v3::SkyWalkingConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  Tracing::DriverPtr driver = std::make_unique<Driver>(
      proto_config, context.serverFactoryContext().clusterManager(),
      context.serverFactoryContext().scope(), context.serverFactoryContext().threadLocal());
  return std::make_shared<Tracing::HttpTracerImpl>(std::move(driver),
                                                   context.serverFactoryContext().localInfo());
}

/**
 * Static registration for the skywalking tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(SkyWalkingTracerFactory, Server::Configuration::TracerFactory);

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
