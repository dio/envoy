#pragma once

#include <memory>
#include "opentracing/tracer.h"
#include "extensions/tracers/skywalking/propagation.h"
#include "extensions/tracers/skywalking/span.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class Tracer : public opentracing::Tracer, public std::enable_shared_from_this<Tracer> {
public:
  std::unique_ptr<opentracing::Span>
  StartSpanWithOptions(opentracing::string_view,
                       const opentracing::StartSpanOptions&) const noexcept override;

  opentracing::expected<void> Inject(const opentracing::SpanContext&, std::ostream&) const override;
  opentracing::expected<void> Inject(const opentracing::SpanContext&,
                                     const opentracing::TextMapWriter&) const override;

  opentracing::expected<void> Inject(const opentracing::SpanContext&,
                                     const opentracing::HTTPHeadersWriter&) const override;
  opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
  Extract(std::istream&) const override;

  opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
  Extract(const opentracing::TextMapReader&) const override;

  opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
  Extract(const opentracing::HTTPHeadersReader&) const override;

  void Close() noexcept override;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
