#include "extensions/tracers/skywalking/skywalking_tracer_impl.h"

#include "common/common/macros.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

Driver::Driver(const envoy::config::trace::v3::SkyWalkingConfig& proto_config,
               Server::Configuration::TracerFactoryContext& context)
    : tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()) {
  tls_slot_ptr_->set([this, proto_config, &context](
                         Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    TracerPtr tracer = std::make_unique<Tracer>(context.serverFactoryContext().clusterManager(),
                                                dispatcher, context.serverFactoryContext().scope(),
                                                proto_config.grpc_service());
    return std::make_shared<TlsTracer>(std::move(tracer), *this);
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config&, Http::RequestHeaderMap&,
                                   const std::string&, Envoy::SystemTime, const Tracing::Decision) {
  auto* tracer = tls_slot_ptr_->getTyped<Driver::TlsTracer>().tracer_.get();
  tracer->test();
  return nullptr;
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
