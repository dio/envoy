#include "extensions/tracers/skywalking/skywalking_tracer_impl.h"
#include "extensions/tracers/skywalking/skywalking_types.h"

#include "common/common/macros.h"
#include "common/common/utility.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class Span : public Tracing::Span {
public:
  Span(const Tracing::Config& config, Tracer& tracer);

  void setOperation(absl::string_view operation) override;
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestamp, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Http::RequestHeaderMap& request_headers) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool sampled) override;

private:
  Tracer& tracer_;
  SpanObjectSegment span_;
};

Driver::Driver(const envoy::config::trace::v3::SkyWalkingConfig& proto_config,
               Server::Configuration::TracerFactoryContext& context)
    : tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()) {
  tls_slot_ptr_->set([this, proto_config, &context](
                         Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    TracerPtr tracer = std::make_unique<Tracer>(context.serverFactoryContext().clusterManager(),
                                                context.serverFactoryContext().scope(), dispatcher,
                                                proto_config.grpc_service());
    return std::make_shared<TlsTracer>(std::move(tracer), *this);
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config, Http::RequestHeaderMap&,
                                   const std::string&, Envoy::SystemTime, const Tracing::Decision) {
  auto* tracer = tls_slot_ptr_->getTyped<Driver::TlsTracer>().tracer_.get();
  return std::make_unique<Span>(config, *tracer);
}

Span::Span(const Tracing::Config&, Tracer& tracer) : tracer_(tracer) {
  span_.segment_context_ = SegmentContext{"id"};
  span_.span_object_ = SpanObject{1};
}

void Span::setOperation(absl::string_view) {}

void Span::setTag(absl::string_view, absl::string_view) {}

void Span::log(SystemTime, const std::string&) {}

void Span::finishSpan() {
  tracer_.test(span_);
}

void Span::injectContext(Http::RequestHeaderMap&) {}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config&, const std::string&, SystemTime) {
  return std::make_unique<Tracing::NullSpan>();
}

void Span::setSampled(bool) {}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
