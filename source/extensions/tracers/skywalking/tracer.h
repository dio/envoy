#pragma once

#include "extensions/tracers/skywalking/trace_segment_reporter.h"
#include "extensions/tracers/skywalking/skywalking_types.h"

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class Tracer {
public:
  explicit Tracer(Upstream::ClusterManager& cm, Stats::Scope& scope, Event::Dispatcher& dispatcher,
                  const envoy::config::core::v3::GrpcService& grpc_service)
      : reporter_(std::make_unique<TraceSegmentReporter>(
            cm.grpcAsyncClientManager().factoryForGrpcService(grpc_service, scope, false),
            dispatcher)) {}

  void test(const SpanObjectSegment& span) { reporter_->test(span); }

private:
  TraceSegmentReporterPtr reporter_;
};

using TracerPtr = std::unique_ptr<Tracer>;

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
