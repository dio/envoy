#include "extensions/tracers/opencensus/exporters/zipkin_exporter.h"

#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "absl/strings/str_replace.h"
#include "opencensus/trace/exporter/attribute_value.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {
namespace Exporters {

void ZipkinSpanExporterHandler::Export(
    const std::vector<::opencensus::trace::exporter::SpanData>& spans) {
  Thread::LockGuard write_lock(write_lock_);
  spans_.insert(spans_.end(), spans.begin(), spans.end());
}

void ZipkinSpanExporterHandler::send(std::vector<::opencensus::trace::exporter::SpanData>&&) {}

void ZipkinSpanExporterHandler::flush() {
  std::vector<::opencensus::trace::exporter::SpanData> spans_to_be_sent;

  {
    Thread::LockGuard write_lock(write_lock_);
    if (!spans_.empty()) {
      spans_to_be_sent.reserve(spans_.size());
      spans_to_be_sent = std::move(spans_);
      spans_.clear();
    }
  }

  enableTimer();
}

} // namespace Exporters
} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy