#include "extensions/tracers/skywalking/skywalking_tracer_impl.h"
#include "extensions/tracers/skywalking/tracer.h"

#include "envoy/config/trace/v3/skywalking.pb.h"

#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

Driver::TlsTracer::TlsTracer(const OpenTracingTracerSharedPtr& tracer,
                             SegmentReporterPtr&& reporter, Driver& driver)
    : tracer_(tracer), reporter_(std::move(reporter)), driver_(driver) {}

Driver::Driver(const envoy::config::trace::v3::SkyWalkingConfig&, Upstream::ClusterManager&,
               Stats::Scope& scope, ThreadLocal::SlotAllocator& tls)
    : OpenTracingDriver{scope}, tracer_stats_{SKYWALKING_TRACER_STATS(
                                    POOL_COUNTER_PREFIX(scope, "tracing.skywalking."))},
      tls_(tls.allocateSlot()) {
  tls_->set([this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    auto reporter = std::make_unique<SegmentReporter>();
    auto tracer = std::shared_ptr<opentracing::Tracer>{new Tracer{}};
    return ThreadLocal::ThreadLocalObjectSharedPtr{
        new TlsTracer(tracer, std::move(reporter), *this)};
  });
}

opentracing::Tracer& Driver::tracer() { return *tls_->getTyped<TlsTracer>().tracer_; }

SegmentReporter::SegmentReporter() {}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
