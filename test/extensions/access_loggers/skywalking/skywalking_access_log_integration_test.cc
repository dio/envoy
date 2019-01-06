#include "envoy/config/accesslog/v2/als.pb.h"
#include "envoy/service/accesslog/v2/als.pb.h"
#include "service-mesh-probe/service-mesh.pb.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/version.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

class AccessLogIntegrationTest : public HttpIntegrationTest,
                                 public Grpc::GrpcClientIntegrationParamTest {
public:
  AccessLogIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion(), realTime()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_, timeSystem()));
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      accesslog_cluster->mutable_http2_protocol_options();
    });

    config_helper_.addConfigModifier(
        [this](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
                   hcm) {
          auto* access_log = hcm.add_access_log();
          access_log->set_name("envoy.skywalking_access_log");

          envoy::config::accesslog::v2::HttpGrpcAccessLogConfig config;
          auto* common_config = config.mutable_common_config();
          common_config->set_log_name("foo");
          setGrpcService(*common_config->mutable_grpc_service(), "accesslog",
                         fake_upstreams_.back()->localAddress());
          MessageUtil::jsonConvert(config, *access_log->mutable_config());
        });

    HttpIntegrationTest::initialize();
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForAccessLogConnection() {
    return fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_access_log_connection_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForAccessLogStream() {
    return fake_access_log_connection_->waitForNewStream(*dispatcher_, access_log_request_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForAccessLogRequest(const std::string& expected_request_msg_yaml) {
    ServiceMeshMetric request_msg;
    VERIFY_ASSERTION(access_log_request_->waitForGrpcMessage(*dispatcher_, request_msg));
    EXPECT_STREQ("POST", access_log_request_->headers().Method()->value().c_str());
    // https://github.com/apache/incubator-skywalking-data-collect-protocol/blob/9f145f80342f5cd28e5764b302d7e2e33bd00891/service-mesh-probe/service-mesh.proto#L27
    EXPECT_STREQ("/ServiceMeshMetricService/collect",
                 access_log_request_->headers().Path()->value().c_str());
    EXPECT_STREQ("application/grpc", access_log_request_->headers().ContentType()->value().c_str());

    ServiceMeshMetric expected_request_msg;
    MessageUtil::loadFromYaml(expected_request_msg_yaml, expected_request_msg);

    // TODO(dio): simulate time and then enable the following expectation.
    // EXPECT_EQ(request_msg.DebugString(), expected_request_msg.DebugString());
    EXPECT_EQ(request_msg.endpoint(), expected_request_msg.endpoint());

    return AssertionSuccess();
  }

  void cleanup() {
    if (fake_access_log_connection_ != nullptr) {
      AssertionResult result = fake_access_log_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_access_log_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  FakeHttpConnectionPtr fake_access_log_connection_;
  FakeStreamPtr access_log_request_;
};

INSTANTIATE_TEST_CASE_P(IpVersionsCientType, AccessLogIntegrationTest,
                        GRPC_CLIENT_INTEGRATION_PARAMS);

// Test a basic full access logging flow.
TEST_P(AccessLogIntegrationTest, BasicAccessLogFlow) {
  testRouterNotFound();
  ASSERT_TRUE(waitForAccessLogConnection());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(waitForAccessLogRequest(fmt::format(R"EOF(
sourceServiceName: "foo"
endpoint: "/notfound"
responseCode: 404
)EOF",
                                                  VersionInfo::version())));

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/notfound", "", downstream_protocol_, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
  ASSERT_TRUE(waitForAccessLogRequest(R"EOF(
sourceServiceName: "foo"
endpoint: "/notfound"
responseCode: 404
)EOF"));

  // Send an empty response and end the stream. This should never happen but make sure nothing
  // breaks and we make a new stream on a follow up request.
  access_log_request_->startGrpcStream();
  envoy::service::accesslog::v2::StreamAccessLogsResponse response_msg;
  access_log_request_->sendGrpcMessage(response_msg);
  access_log_request_->finishGrpcStream(Grpc::Status::Ok);
  switch (clientType()) {
  case Grpc::ClientType::EnvoyGrpc:
    test_server_->waitForGaugeEq("cluster.accesslog.upstream_rq_active", 0);
    break;
  case Grpc::ClientType::GoogleGrpc:
    test_server_->waitForCounterGe("grpc.accesslog.streams_closed_0", 1);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/notfound", "",
                                                downstream_protocol_, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
  ASSERT_TRUE(waitForAccessLogStream());
  ASSERT_TRUE(waitForAccessLogRequest(fmt::format(R"EOF(
sourceServiceName: "foo"
endpoint: "/notfound"
responseCode: 404
)EOF",
                                                  VersionInfo::version())));

  cleanup();
}

} // namespace
} // namespace Envoy
