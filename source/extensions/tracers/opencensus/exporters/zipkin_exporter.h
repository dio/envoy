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
  ZipkinSpanExporterHandler(Event::Dispatcher& dispatcher,
                            Upstream::ClusterManager& cluster_manager,
                            const LocalInfo::LocalInfo& local_info)
      : flush_timer_(dispatcher.createTimer([this]() -> void { flush(); })), cm_(cluster_manager),
        collector_cluster_("zipkin"), collector_path_("/api/v2/spans"),
        local_endpoint_(createLocalEndpoint(local_info)), address_(local_info.address()) {
    enableTimer();
  }

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

  std::string serializeSpans(std::vector<::opencensus::trace::exporter::SpanData>&& spans) const;
  const ProtobufWkt::Struct
  translateSpanData(const ::opencensus::trace::exporter::SpanData& span_data,
                    Zipkin::Utility::Replacements& replacements) const;
  const ProtobufWkt::Value createLocalEndpoint(const LocalInfo::LocalInfo& local_info) const;

  Event::TimerPtr flush_timer_;
  Upstream::ClusterManager& cm_;
  const std::string collector_cluster_;
  const std::string collector_path_;
  const ProtobufWkt::Value local_endpoint_;
  Network::Address::InstanceConstSharedPtr address_;

  Thread::MutexBasicLockable write_lock_;
  std::vector<::opencensus::trace::exporter::SpanData> spans_ ABSL_GUARDED_BY(write_lock_);
};
} // namespace Exporters
} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
}; // namespace Envoy
