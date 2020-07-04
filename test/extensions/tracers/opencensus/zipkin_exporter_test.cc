#include "extensions/tracers/opencensus/exporters/zipkin_exporter.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opencensus/trace/internal/span_impl.h"
#include "opencensus/trace/span.h"

namespace opencensus {
namespace trace {

class SpanTestPeer {
public:
  static SpanId getParentSpanId(Span* span) { return span->span_impl_for_test()->parent_span_id(); }
  static exporter::SpanData toSpanData(Span* span) {
    return span->span_impl_for_test()->ToSpanData();
  }
};

} // namespace trace
} // namespace opencensus

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {
namespace Exporters {

namespace {

using opencensus::trace::AlwaysSampler;
using opencensus::trace::Span;
using opencensus::trace::SpanTestPeer;
using opencensus::trace::exporter::SpanData;

class ZipkinSpanExporterHandlerTest : public testing::Test {
public:
  void setupHandler() {
    cm_.thread_local_cluster_.cluster_.info_->name_ = "zipkin";
    ON_CALL(cm_, httpAsyncClientForCluster("zipkin")).WillByDefault(ReturnRef(cm_.async_client_));
    EXPECT_CALL(dispatcher_, createTimer_(_));
    handler_ = std::make_unique<ZipkinSpanExporterHandler>(dispatcher_, cm_);
  }

  std::unique_ptr<ZipkinSpanExporterHandler> handler_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Http::MockAsyncClientRequest> request_{&cm_.async_client_};
};

TEST_F(ZipkinSpanExporterHandlerTest, SendSpans) {
  setupHandler();

  AlwaysSampler sampler;
  auto span = Span::StartSpan("MySpan", /*parent=*/nullptr, {&sampler});
  span.AddAnnotation("Annotation text.", {{"key", "value1"},
                                          {"key", 123},
                                          {"another_key", "another_value"},
                                          {"key", "value2"},
                                          {"bool_key", true}});
  span.AddAttributes({{"key3", "value3"}, {"key4", 123}, {"key5", false}});
  std::vector<opencensus::trace::exporter::SpanData> spans{
      SpanTestPeer::toSpanData(&span),
  };

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Envoy::Http::RequestMessagePtr& request, Envoy::Http::AsyncClient::Callbacks&,
              const Http::AsyncClient::RequestOptions&) -> Envoy::Http::AsyncClient::Request* {
            EXPECT_EQ("POST", request->headers().getMethodValue());
            std::cerr << request->bodyAsString() << "\n";
            return &request_;
          }));
  handler_->send(std::move(spans));
}

} // namespace

} // namespace Exporters
} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy