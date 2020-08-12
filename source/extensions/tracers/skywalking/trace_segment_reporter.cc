#include "extensions/tracers/skywalking/trace_segment_reporter.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {

SegmentObject toSegmentObject(const SpanObject& span_object) {
  SegmentObject segment_object;
  segment_object.set_traceid(span_object.span_context_.traceId());
  segment_object.set_tracesegmentid(span_object.span_context_.traceSegmentId());
  segment_object.set_service(span_object.span_context_.service());
  segment_object.set_serviceinstance(span_object.span_context_.serviceInstance());

  auto* span = segment_object.mutable_spans()->Add();
  span->set_parentspanid(span_object.parent_span_id_);
  span->set_spanid(span_object.span_id_);
  span->set_operationname(span_object.operation_name_);
  span->set_spanlayer(::SpanLayer::Http);
  span->set_spantype(::SpanType::Entry);
  span->set_componentid(span_object.component_id_);
  span->set_starttime(span_object.start_time_);
  span->set_endtime(span_object.end_time_);
  span->set_iserror(span_object.is_error_);

  for (const auto& span_tag : span_object.tags_) {
    auto* tag = span->mutable_tags()->Add();
    tag->set_key(span_tag.first);
    tag->set_value(span_tag.second);
  }

  return segment_object;
}

} // namespace

TraceSegmentReporter::TraceSegmentReporter(Grpc::AsyncClientFactoryPtr&& factory,
                                           Event::Dispatcher& dispatcher)
    : client_(factory->create()),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "TraceSegmentReportService.collect")) {
  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  establishNewStream();
}

void TraceSegmentReporter::report(const SpanObject& span_object) {
  sendTraceSegment(toSegmentObject(span_object));
}

void TraceSegmentReporter::sendTraceSegment(const SegmentObject& request) {
  // TODO(dio): Buffer when stream is not yet established.
  if (stream_ != nullptr) {
    stream_->sendMessage(request, false);
  }
}

void TraceSegmentReporter::closeStream() {
  if (stream_ != nullptr) {
    stream_->closeStream();
  }
}

void TraceSegmentReporter::onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) {
  stream_ = nullptr;
  handleFailure();
}

void TraceSegmentReporter::establishNewStream() {
  stream_ = client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
  if (stream_ == nullptr) {
    handleFailure();
    return;
  }
}

void TraceSegmentReporter::handleFailure() { setRetryTimer(); }

void TraceSegmentReporter::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(5000));
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy