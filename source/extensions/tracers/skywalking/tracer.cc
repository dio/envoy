#include "extensions/tracers/skywalking/tracer.h"
#include <chrono>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

constexpr char StatusCodeTag[] = "status_code";
constexpr char UrlTag[] = "url";

namespace {

uint64_t getTimestamp(SystemTime time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

} // namespace

Tracing::SpanPtr Tracer::startSpan(const SpanContext& context, const Tracing::Config& config,
                                   SystemTime start_time, const Endpoint& endpoint) {
  SpanObject span_object(context, time_source_, random_generator_);

  // TODO(dio): Use information about context here.

  span_object.span_type_ =
      config.operationName() == Tracing::OperationName::Egress ? SpanType::Exit : SpanType::Entry;
  span_object.start_time_ = getTimestamp(start_time);
  span_object.operation_name_ = endpoint.value();

  span_object.setContextLocalInfo(node_, service_, address_);
  span_object.setContextParentEndpoint(span_object.operation_name_);

  return std::make_unique<Span>(span_object, *this);
}

void Tracer::report(const SpanObject& span_object) { reporter_->report(span_object); }

Tracer::~Tracer() { reporter_->closeStream(); }

Span::Span(SpanObject span_object, Tracer& tracer) : span_object_(span_object), tracer_(tracer) {}

void Span::setOperation(absl::string_view operation_name) {
  span_object_.operation_name_ = operation_name;
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  if (name == Tracing::Tags::get().HttpUrl) {
    span_object_.tags_.push_back({UrlTag, std::string(value)});
  }

  if (name == Tracing::Tags::get().HttpStatusCode) {
    span_object_.tags_.push_back({StatusCodeTag, std::string(value)});
  }

  if (name == Tracing::Tags::get().Error) {
    span_object_.is_error_ = value == Tracing::Tags::get().True ? true : false;
  }

  span_object_.tags_.push_back({std::string(name), std::string(value)});
}

void Span::log(SystemTime timestamp, const std::string& event) {
  const uint64_t event_timestamp = getTimestamp(timestamp);
  const Tag tag{event, absl::StrCat(event_timestamp)};
  const Log log{event_timestamp, {tag}};
  span_object_.logs_.push_back(log);
}

void Span::finishSpan() {
  span_object_.finish();
  tracer_.report(span_object_);
}

void Span::injectContext(Http::RequestHeaderMap& request_headers) {
  span_object_.span_context_.inject(request_headers);
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config&, const std::string&, SystemTime) {
  return std::make_unique<Tracing::NullSpan>();
}

void Span::setSampled(bool sampled) { span_object_.span_context_.setSampled(sampled); }

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
