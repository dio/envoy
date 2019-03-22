#include "extensions/access_loggers/skywalking/config.h"

#include "envoy/config/accesslog/v2/als.pb.validate.h"
#include "envoy/config/filter/accesslog/v2/accesslog.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/macros.h"
#include "common/grpc/async_client_impl.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/skywalking/skywalking_access_log_impl.h"
#include "extensions/access_loggers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Skywalking {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(skywalking_access_log_streamer);

AccessLog::InstanceSharedPtr SkywalkingAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
    Server::Configuration::FactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::config::accesslog::v2::HttpGrpcAccessLogConfig&>(config);
  std::shared_ptr<SkywalkingAccessLogStreamer> skywalking_access_log_streamer =
      context.singletonManager().getTyped<SkywalkingAccessLogStreamer>(
          SINGLETON_MANAGER_REGISTERED_NAME(skywalking_access_log_streamer),
          [&context, grpc_service = proto_config.common_config().grpc_service()] {
            return std::make_shared<SkywalkingAccessLogStreamerImpl>(
                context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
                    grpc_service, context.scope(), false),
                context.threadLocal(), context.localInfo());
          });

  return std::make_shared<SkywalkingAccessLog>(std::move(filter), proto_config,
                                               skywalking_access_log_streamer);
}

ProtobufTypes::MessagePtr SkywalkingAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{new envoy::config::accesslog::v2::HttpGrpcAccessLogConfig()};
}

std::string SkywalkingAccessLogFactory::name() const { return AccessLogNames::get().Skywalking; }

/**
 * Static registration for the HTTP Skywalking access log. @see RegisterFactory.
 */
static Registry::RegisterFactory<SkywalkingAccessLogFactory,
                                 Server::Configuration::AccessLogInstanceFactory>
    register_;

} // namespace Skywalking
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
