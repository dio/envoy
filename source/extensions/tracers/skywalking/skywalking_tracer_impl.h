#pragma once

#include "envoy/config/trace/v3/skywalking.pb.h"

#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/tracers/common/ot/opentracing_driver_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

#define SKYWALKING_TRACER_STATS(COUNTER)                                                           \
  COUNTER(traces_sent)                                                                             \
  COUNTER(timer_flushed)                                                                           \
  COUNTER(reports_skipped_no_cluster)                                                              \
  COUNTER(reports_sent)                                                                            \
  COUNTER(reports_dropped)                                                                         \
  COUNTER(reports_failed)

struct SkyWalkingTracerStats {
  SKYWALKING_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

class SegmentReporter;
using SegmentReporterPtr = std::unique_ptr<SegmentReporter>;
using OpenTracingTracerSharedPtr = std::shared_ptr<opentracing::Tracer>;

class Driver : public Common::Ot::OpenTracingDriver {
public:
  Driver(const envoy::config::trace::v3::SkyWalkingConfig& proto_config,
         Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
         ThreadLocal::SlotAllocator& tls);

  SkyWalkingTracerStats& tracerStats() { return tracer_stats_; }

  // Tracer::OpenTracingDriver
  opentracing::Tracer& tracer() override;
  PropagationMode propagationMode() const override {
    return Common::Ot::OpenTracingDriver::PropagationMode::TracerNative;
  }

private:
  struct TlsTracer : ThreadLocal::ThreadLocalObject {
    TlsTracer(const OpenTracingTracerSharedPtr& tracer, SegmentReporterPtr&& reporter,
              Driver& driver);

    OpenTracingTracerSharedPtr tracer_;
    SegmentReporterPtr reporter_;
    Driver& driver_;
  };

  SkyWalkingTracerStats tracer_stats_;
  ThreadLocal::SlotPtr tls_;
};

class SegmentReporter : protected Logger::Loggable<Logger::Id::tracing> {
public:
  SegmentReporter();
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
