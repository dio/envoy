#pragma once

#include "envoy/api/api.h"
#include "envoy/api/v2/core/grpc_service.pb.h"
#include "envoy/grpc/google_grpc_creds.h"

#include "grpcpp/grpcpp.h"

namespace Envoy {
namespace Grpc {

grpc::SslCredentialsOptions buildSslOptionsFromConfig(
    const envoy::api::v2::core::GrpcService::GoogleGrpc::SslCredentials& ssl_config);

std::shared_ptr<grpc::ChannelCredentials>
getGoogleGrpcChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service,
                                Api::Api& api);

class CredsUtility {
public:
  /**
   * Translation from envoy::api::v2::core::GrpcService to grpc::ChannelCredentials
   * for channel credentials.
   * @param google_grpc Google gRPC config.
   * @param api reference to the Api object
   * @return std::shared_ptr<grpc::ChannelCredentials> channel credentials. A nullptr
   *         will be returned in the absence of any configured credentials.
   */
  static std::shared_ptr<grpc::ChannelCredentials>
  getChannelCredentials(const envoy::api::v2::core::GrpcService::GoogleGrpc& google_grpc,
                        Api::Api& api);

  /**
   * Static translation from envoy::api::v2::core::GrpcService to a vector of grpc::CallCredentials.
   * Any plugin based call credentials will be elided.
   * @param grpc_service Google gRPC config.
   * @return std::vector<std::shared_ptr<grpc::CallCredentials>> call credentials.
   */
  static std::vector<std::shared_ptr<grpc::CallCredentials>>
  callCredentials(const envoy::api::v2::core::GrpcService::GoogleGrpc& google_grpc);

  /**
   * Default translation from envoy::api::v2::core::GrpcService to grpc::ChannelCredentials for SSL
   * channel credentials.
   * @param grpc_service_config gRPC service config.
   * @param api reference to the Api object
   * @return std::shared_ptr<grpc::ChannelCredentials> SSL channel credentials. Empty SSL
   *         credentials will be set in the absence of any configured SSL in grpc_service_config,
   *         forcing the channel to SSL.
   */
  static std::shared_ptr<grpc::ChannelCredentials>
  defaultSslChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config,
                               Api::Api& api);

  /**
   * Default static translation from envoy::api::v2::core::GrpcService to grpc::ChannelCredentials
   * for all non-plugin based channel and call credentials.
   * @param grpc_service_config gRPC service config.
   * @param api reference to the Api object
   * @return std::shared_ptr<grpc::ChannelCredentials> composite channel and call credentials.
   *         will be set in the absence of any configured SSL in grpc_service_config, forcing the
   *         channel to SSL.
   */
  static std::shared_ptr<grpc::ChannelCredentials>
  defaultChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config,
                            Api::Api& api);
};

class GoogleGrpcCredentialsFactoryContextImpl : public GoogleGrpcCredentialsFactoryContext {
public:
  GoogleGrpcCredentialsFactoryContextImpl(Api::Api& api, Event::TimeSystem& time_system)
      : api_(api), time_system_(time_system) {}

  Api::Api& api() override { return api_; }

  Event::TimeSystem& timeSystem() override { return time_system_; }

private:
  Api::Api& api_;
  Event::TimeSystem& time_system_;
};

} // namespace Grpc
} // namespace Envoy
