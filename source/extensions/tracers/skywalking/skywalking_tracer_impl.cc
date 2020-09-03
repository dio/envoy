#include "extensions/tracers/skywalking/skywalking_tracer_impl.h"

#include <memory>

#include "common/common/macros.h"
#include "common/common/utility.h"

#include "extensions/tracers/skywalking/skywalking_types.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

Driver::Driver(const envoy::config::trace::v3::SkyWalkingConfig& proto_config,
               Server::Configuration::TracerFactoryContext& context)
    : tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()) {
  tls_slot_ptr_->set([this, proto_config, &context](
                         Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    auto& factory_context = context.serverFactoryContext();
    TracerPtr tracer =
        std::make_unique<Tracer>(factory_context.clusterManager(), factory_context.scope(),
                                 factory_context.localInfo(), factory_context.timeSource(),
                                 factory_context.random(), dispatcher, proto_config.grpc_service());
    return std::make_shared<TlsTracer>(std::move(tracer), *this);
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Http::RequestHeaderMap& request_headers, const std::string&,
                                   Envoy::SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {
  auto& tracer = *tls_slot_ptr_->getTyped<Driver::TlsTracer>().tracer_;
  return tracer.startSpan(config, start_time,
                          SpanContext{Endpoint{request_headers}, tracer.service(), tracer.node()},
                          SpanContext::extract(request_headers, tracing_decision));
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
