#include "extensions/access_loggers/skywalking/skywalking_access_log_impl.h"

#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/network/utility.h"
#include "common/stream_info/utility.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Skywalking {

SkywalkingAccessLogStreamerImpl::SkywalkingAccessLogStreamerImpl(
    Grpc::AsyncClientFactoryPtr&& factory, ThreadLocal::SlotAllocator& tls,
    const LocalInfo::LocalInfo& local_info)
    : tls_slot_(tls.allocateSlot()) {
  SharedStateSharedPtr shared_state = std::make_shared<SharedState>(std::move(factory), local_info);
  tls_slot_->set([shared_state](Event::Dispatcher&) {
    return ThreadLocal::ThreadLocalObjectSharedPtr{new ThreadLocalStreamer(shared_state)};
  });
}

void SkywalkingAccessLogStreamerImpl::ThreadLocalStream::onRemoteClose(Grpc::Status::GrpcStatus,
                                                                       const std::string&) {
  auto it = parent_.stream_map_.find(log_name_);
  ASSERT(it != parent_.stream_map_.end());
  if (it->second.stream_ != nullptr) {
    // Only erase if we have a stream. Otherwise we had an inline failure and we will clear the
    // stream data in send().
    parent_.stream_map_.erase(it);
  }
}

SkywalkingAccessLogStreamerImpl::ThreadLocalStreamer::ThreadLocalStreamer(
    const SharedStateSharedPtr& shared_state)
    : client_(shared_state->factory_->create()), shared_state_(shared_state) {}

void SkywalkingAccessLogStreamerImpl::ThreadLocalStreamer::send(ServiceMeshMetric& message,
                                                                const std::string& log_name) {
  auto stream_it = stream_map_.find(log_name);
  if (stream_it == stream_map_.end()) {
    stream_it = stream_map_.emplace(log_name, ThreadLocalStream(*this, log_name)).first;
  }

  auto& stream_entry = stream_it->second;
  if (stream_entry.stream_ == nullptr) {
    stream_entry.stream_ =
        client_->start(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                           "ServiceMeshMetricService.collect"),
                       stream_entry);

    // TODO(dio): Build the identifier from local info node.
    // auto* identifier = message.mutable_identifier();
    // *identifier->mutable_node() = shared_state_->local_info_.node();
    // identifier->set_log_name(log_name);
  }

  if (stream_entry.stream_ != nullptr) {
    stream_entry.stream_->sendMessage(message, false);
  } else {
    // Clear out the stream data due to stream creation failure.
    stream_map_.erase(stream_it);
  }
}

SkywalkingAccessLog::SkywalkingAccessLog(
    AccessLog::FilterPtr&& filter,
    const envoy::config::accesslog::v2::HttpGrpcAccessLogConfig& config,
    SkywalkingAccessLogStreamerSharedPtr skywalking_access_log_streamer)
    : filter_(std::move(filter)), config_(config),
      skywalking_access_log_streamer_(skywalking_access_log_streamer) {
  for (const auto& header : config_.additional_request_headers_to_log()) {
    request_headers_to_log_.emplace_back(header);
  }

  for (const auto& header : config_.additional_response_headers_to_log()) {
    response_headers_to_log_.emplace_back(header);
  }

  for (const auto& header : config_.additional_response_trailers_to_log()) {
    response_trailers_to_log_.emplace_back(header);
  }
}

void SkywalkingAccessLog::responseFlagsToAccessLogResponseFlags(
    envoy::data::accesslog::v2::AccessLogCommon& common_access_log,
    const StreamInfo::StreamInfo& stream_info) {

  static_assert(StreamInfo::ResponseFlag::LastFlag == 0x4000,
                "A flag has been added. Fix this code.");

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::FailedLocalHealthCheck)) {
    common_access_log.mutable_response_flags()->set_failed_local_healthcheck(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::NoHealthyUpstream)) {
    common_access_log.mutable_response_flags()->set_no_healthy_upstream(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout)) {
    common_access_log.mutable_response_flags()->set_upstream_request_timeout(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::LocalReset)) {
    common_access_log.mutable_response_flags()->set_local_reset(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamRemoteReset)) {
    common_access_log.mutable_response_flags()->set_upstream_remote_reset(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamConnectionFailure)) {
    common_access_log.mutable_response_flags()->set_upstream_connection_failure(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamConnectionTermination)) {
    common_access_log.mutable_response_flags()->set_upstream_connection_termination(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow)) {
    common_access_log.mutable_response_flags()->set_upstream_overflow(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::NoRouteFound)) {
    common_access_log.mutable_response_flags()->set_no_route_found(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DelayInjected)) {
    common_access_log.mutable_response_flags()->set_delay_injected(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::FaultInjected)) {
    common_access_log.mutable_response_flags()->set_fault_injected(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::RateLimited)) {
    common_access_log.mutable_response_flags()->set_rate_limited(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UnauthorizedExternalService)) {
    common_access_log.mutable_response_flags()->mutable_unauthorized_details()->set_reason(
        envoy::data::accesslog::v2::ResponseFlags_Unauthorized_Reason::
            ResponseFlags_Unauthorized_Reason_EXTERNAL_SERVICE);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::RateLimitServiceError)) {
    common_access_log.mutable_response_flags()->set_rate_limit_service_error(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DownstreamConnectionTermination)) {
    common_access_log.mutable_response_flags()->set_downstream_connection_termination(true);
  }
}

void SkywalkingAccessLog::log(const Http::HeaderMap*, const Http::HeaderMap*,
                              const Http::HeaderMap*, const StreamInfo::StreamInfo&) {

  ServiceMeshMetric message;
  message.set_starttime(1545618997);
  message.set_endtime(1545618997);
  message.set_sourceservicename("svc1");
  message.set_sourceserviceinstance("svc1");
  message.set_destservicename("svc2");
  message.set_destserviceinstance("svc2");
  message.set_endpoint("/svc2/hello");
  message.set_latency(1000);
  message.set_responsecode(200);
  message.set_status(true);
  message.set_protocol(Protocol::HTTP);
  message.set_detectpoint(DetectPoint::server);

  // TODO(dio): Consider batching multiple logs and flushing.
  skywalking_access_log_streamer_->send(message, config_.common_config().log_name());
}

} // namespace Skywalking
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
