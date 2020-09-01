#include "extensions/tracers/skywalking/tracer.h"

#include <iostream>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

std::unique_ptr<opentracing::Span>
Tracer::StartSpanWithOptions(opentracing::string_view,
                             const opentracing::StartSpanOptions&) const noexcept {
  std::cerr << "StartSpanWithOptions\n";
  return std::make_unique<Span>(shared_from_this(), SpanContext{});
}

opentracing::expected<void> Tracer::Inject(const opentracing::SpanContext&, std::ostream&) const {
  return opentracing::make_unexpected(opentracing::invalid_span_context_error);
}
opentracing::expected<void> Tracer::Inject(const opentracing::SpanContext&,
                                           const opentracing::TextMapWriter&) const {
  return opentracing::make_unexpected(opentracing::invalid_span_context_error);
}

opentracing::expected<void> Tracer::Inject(const opentracing::SpanContext&,
                                           const opentracing::HTTPHeadersWriter&) const {
  std::cerr << "Tracer::Inject\n";
  return opentracing::make_unexpected(opentracing::invalid_span_context_error);
}

opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
Tracer::Extract(std::istream&) const {
  return opentracing::make_unexpected(opentracing::invalid_span_context_error);
}

opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
Tracer::Extract(const opentracing::TextMapReader&) const {
  return opentracing::make_unexpected(opentracing::invalid_span_context_error);
}

opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
Tracer::Extract(const opentracing::HTTPHeadersReader&) const {
  std::cerr << "Tracer::Extract\n";
  return opentracing::make_unexpected(opentracing::invalid_span_context_error);
}

void Tracer::Close() noexcept {}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
