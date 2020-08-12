#pragma once

#include <bits/stdint-uintn.h>
#include <stdint.h>
#include <string>

#include "envoy/http/header_map.h"
#include "envoy/common/time.h"
#include "envoy/common/random_generator.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class SpanContext {

public:
  void initialize(Random::RandomGenerator& random_generator);
  bool extract(Http::RequestHeaderMap& request_headers);
  void inject(Http::RequestHeaderMap& request_headers) const;

  bool isNew() const { return is_new_; }
  void setSampled(bool sampled) { sampled_ = sampled ? 1 : 0; }
  void setService(const std::string& service) { service_ = service; }
  void setServiceInstance(const std::string& service_instance) {
    service_instance_ = service_instance;
  }
  void setParentEndpoint(const std::string parent_endpoint) { parent_endpoint_ = parent_endpoint; }
  void setCallerAddress(const std::string& caller_address) { caller_address_ = caller_address; }

  const std::string& traceId() const { return trace_id_; }
  const std::string& traceSegmentId() const { return trace_segment_id_; }
  const std::string& service() const { return service_; }
  const std::string& serviceInstance() const { return service_instance_; }

private:
  int sampled_{1};
  std::string trace_id_;
  std::string trace_segment_id_;
  std::string parent_span_id_{"-1"};
  std::string service_;
  std::string service_instance_;
  std::string parent_endpoint_;
  std::string caller_address_;

  bool is_new_{true};
};

enum class SpanType { Entry, Exit, Local };
enum class SpanLayer { Unknown, Database, RpcFramework, Http, Mq, Cache };
enum class RefType { CrossProcess, CrossThread };

using Tag = std::pair<std::string, std::string>;

struct Log {
  uint64_t timestamp_;
  std::vector<Tag> data;
};

class Endpoint {
public:
  explicit Endpoint(Http::RequestHeaderMap& request_headers)
      : method_(request_headers.getMethodValue()), path_(request_headers.getPathValue()) {}

  std::string value() const { return absl::StrCat("/", method_, path_); }

private:
  const std::string method_;
  const std::string path_;
};

struct SpanObject {
  SpanObject(const SpanContext& span_context, TimeSource& time_source,
             Random::RandomGenerator& random_generator)
      : span_context_(span_context), time_source_(time_source) {
    span_context_.initialize(random_generator);
  }

  void setContextLocalInfo(const std::string& node, const std::string& service,
                           const std::string& address) {
    span_context_.setServiceInstance(node);
    span_context_.setService(service);
    span_context_.setCallerAddress(address);
  }

  void setContextParentEndpoint(const std::string& endpoint) {
    span_context_.setParentEndpoint(endpoint);
  }

  void finish();

  SpanContext span_context_;

  int32_t span_id_{0};
  int32_t parent_span_id_{-1};

  uint64_t start_time_;
  uint64_t end_time_;

  std::string operation_name_;

  // TODO(dio): Ask for component id to upstream.
  int32_t component_id_{5004};

  bool is_error_{false};

  SpanType span_type_{SpanType::Entry};
  SpanLayer span_layer_{SpanLayer::Http};

  std::vector<Tag> tags_;
  std::vector<Log> logs_;

  TimeSource& time_source_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy