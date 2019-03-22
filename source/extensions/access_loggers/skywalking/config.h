#pragma once

#include <string>

#include "envoy/server/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Skywalking {

/**
 * Config registration for the HTTP Skywalking access log. @see AccessLogInstanceFactory.
 */
class SkywalkingAccessLogFactory : public Server::Configuration::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

// TODO(mattklein123): Add TCP access log and refactor into base/concrete gRPC access logs.

} // namespace Skywalking
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
