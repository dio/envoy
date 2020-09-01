#include "extensions/tracers/skywalking/span.h"
#include "extensions/tracers/skywalking/tracer.h"

#include <iostream>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

Span::Span(std::shared_ptr<const Tracer> tracer, SpanContext context)
    : tracer_(std::move(tracer)), context_(context) {}

void Span::FinishWithOptions(const opentracing::FinishSpanOptions&) noexcept {
    std::cerr << "FinishWithOptions\n";
}
void Span::SetOperationName(opentracing::string_view) noexcept {
    std::cerr << "SetOperationName\n";
}
void Span::SetTag(opentracing::string_view, const opentracing::Value&) noexcept {}
void Span::SetBaggageItem(opentracing::string_view, opentracing::string_view) noexcept {}
std::string Span::BaggageItem(opentracing::string_view) const noexcept { return ""; }
void Span::Log(
    std::initializer_list<std::pair<opentracing::string_view, opentracing::Value>>) noexcept {}
const opentracing::SpanContext& Span::context() const noexcept { return context_; }
const opentracing::Tracer& Span::tracer() const noexcept { return *tracer_; }

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy