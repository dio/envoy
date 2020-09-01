#pragma once

#include "opentracing/span.h"
#include "extensions/tracers/skywalking/propagation.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class Tracer;

class Span : public opentracing::Span {
public:
  Span(std::shared_ptr<const Tracer> tracer, SpanContext context);

  // opentracing::Span methods.
  void FinishWithOptions(const opentracing::FinishSpanOptions&) noexcept override;
  void SetOperationName(opentracing::string_view) noexcept override;
  void SetTag(opentracing::string_view, const opentracing::Value&) noexcept override;
  void SetBaggageItem(opentracing::string_view, opentracing::string_view) noexcept override;
  std::string BaggageItem(opentracing::string_view) const noexcept override;
  void Log(std::initializer_list<std::pair<opentracing::string_view, opentracing::Value>>) noexcept
      override;
  const opentracing::SpanContext& context() const noexcept override;
  const opentracing::Tracer& tracer() const noexcept override;

private:
  std::shared_ptr<const Tracer> tracer_;
  SpanContext context_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy