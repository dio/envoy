#pragma once

#include "extensions/tracers/zipkin/utility.h"
#include "envoy/event/dispatcher.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/protobuf/utility.h"
#include "common/common/lock_guard.h"
#include "common/common/thread.h"

#include "opencensus/trace/exporter/span_exporter.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {
namespace Exporters {

class ZipkinSpanExporterHandler : public ::opencensus::trace::exporter::SpanExporter::Handler,
                                  public Http::AsyncClient::Callbacks {
public:
  ZipkinSpanExporterHandler(Event::Dispatcher& dispatcher)
      : flush_timer_(dispatcher.createTimer([this]() -> void { flush(); })) {}

  // ::opencensus::trace::exporter::SpanExporter::Handler
  void Export(const std::vector<::opencensus::trace::exporter::SpanData>& spans) override;

  // Http::AsyncClient::Callbacks.
  // The callbacks below record Zipkin-span-related stats.
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override {}
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override {}
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

  /**
   * Send list of OpenCensus spans to the defined Zipkin V2 API server's /spans endpoint.
   * @param spans list of list of OpenCensus spans.
   */
  void send(std::vector<::opencensus::trace::exporter::SpanData>&& spans);

private:
  void enableTimer() { flush_timer_->enableTimer(std::chrono::milliseconds(5000U)); }
  void flush();

  Event::TimerPtr flush_timer_;
  Thread::MutexBasicLockable write_lock_;
  std::vector<::opencensus::trace::exporter::SpanData> spans_ ABSL_GUARDED_BY(write_lock_);
};

} // namespace Exporters
} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy