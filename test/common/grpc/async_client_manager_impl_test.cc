#include "envoy/config/core/v3/grpc_service.pb.h"

#include "common/api/api_impl.h"
#include "common/grpc/async_client_manager_impl.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Return;

namespace Envoy {
namespace Grpc {
namespace {

class AsyncClientManagerImplTest : public testing::Test {
public:
  AsyncClientManagerImplTest()
      : api_(Api::createApiForTest()), stat_names_(scope_.symbolTable()),
        async_client_manager_(cm_, tls_, test_time_.timeSystem(), *api_, stat_names_) {}

  Upstream::MockClusterManager cm_;
  Upstream::MockClusterInfo* mock_cluster_info_ = new NiceMock<Upstream::MockClusterInfo>();
  Upstream::ClusterInfoConstSharedPtr cluster_info_ptr_{mock_cluster_info_};
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::MockStore scope_;
  DangerousDeprecatedTestTime test_time_;
  Api::ApiPtr api_;
  StatNames stat_names_;
  AsyncClientManagerImpl async_client_manager_;
};

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcOk) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, get("foo"));
  EXPECT_CALL(cm_.thread_local_cluster_, info()).WillOnce(Return(cluster_info_ptr_));
  EXPECT_CALL(*mock_cluster_info_, addedViaApi());
  async_client_manager_.factoryForGrpcService(grpc_service, scope_, false);
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcUnknown) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, get("foo")).WillOnce(Return(nullptr));
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "gRPC async client: unknown cluster 'foo'");
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcDynamicCluster) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, get("foo"));
  EXPECT_CALL(cm_.thread_local_cluster_, info()).WillOnce(Return(cluster_info_ptr_));
  EXPECT_CALL(*mock_cluster_info_, addedViaApi()).WillOnce(Return(true));
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "gRPC async client: invalid cluster 'foo': currently only static (non-CDS) clusters are "
      "supported");
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpc) {
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_NE(nullptr, async_client_manager_.factoryForGrpcService(grpc_service, scope_, false));
#else
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcUnknownOk) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, clusters()).Times(0);
  ASSERT_NO_THROW(async_client_manager_.factoryForGrpcService(grpc_service, scope_, true));
}

} // namespace
} // namespace Grpc
} // namespace Envoy
