#include "extensions/tracers/skywalking/trace_segment_reporter.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

TraceSegmentReporter::TraceSegmentReporter(Grpc::AsyncClientFactoryPtr&& factory,
                                           Event::Dispatcher& dispatcher)
    : client_(factory->create()),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "TraceSegmentReportService.collect")) {
  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  establishNewStream();
}

void TraceSegmentReporter::sendTraceSegment(const SegmentObject& request) {
  // TODO(dio): Buffer when stream is not yet established.
  if (stream_ != nullptr) {
    stream_->sendMessage(request, false);
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