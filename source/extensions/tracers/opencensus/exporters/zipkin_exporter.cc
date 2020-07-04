#include "extensions/tracers/opencensus/exporters/zipkin_exporter.h"

#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "absl/strings/str_replace.h"
#include "opencensus/trace/exporter/attribute_value.h"

constexpr char ANNOTATIONS[] = "annotations";
constexpr char DURATION[] = "duration";
constexpr char TIMESTAMP[] = "timestamp";
constexpr char VALUE[] = "value";

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {
namespace Exporters {

namespace {

// Copied from opencensus-cpp.
std::string attributeValueToString(const ::opencensus::trace::exporter::AttributeValue& value) {
  switch (value.type()) {
  case ::opencensus::trace::AttributeValueRef::Type::kString:
    return value.string_value();
    break;
  case ::opencensus::trace::AttributeValueRef::Type::kBool:
    return value.bool_value() ? "true" : "false";
    break;
  case ::opencensus::trace::AttributeValueRef::Type::kInt:
    return std::to_string(value.int_value());
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

// Copied from opencensus-cpp.
std::string serializeAnnotation(const ::opencensus::trace::exporter::Annotation& annotation) {
  std::string serialized(annotation.description());
  if (!annotation.attributes().empty()) {
    absl::StrAppend(&serialized, " (");
    size_t count = 0;
    for (const auto& attribute : annotation.attributes()) {
      absl::StrAppend(&serialized, attribute.first, ":", attributeValueToString(attribute.second));

      if (++count < annotation.attributes().size()) {
        absl::StrAppend(&serialized, ", ");
      }
    }
    absl::StrAppend(&serialized, ")");
  }
  return serialized;
}

} // namespace

void ZipkinSpanExporterHandler::Export(
    const std::vector<::opencensus::trace::exporter::SpanData>& spans) {
  Thread::LockGuard write_lock(write_lock_);
  spans_.insert(spans_.end(), spans.begin(), spans.end());
}

void ZipkinSpanExporterHandler::flush() {
  std::vector<::opencensus::trace::exporter::SpanData> spans_to_be_sent;

  {
    Thread::LockGuard write_lock(write_lock_);
    if (!spans_.empty()) {
      spans_to_be_sent.reserve(spans_.size());
      spans_to_be_sent = std::move(spans_);
      spans_.clear();
    }
  }

  if (!spans_to_be_sent.empty()) {
    send(std::move(spans_to_be_sent));
  }
  enableTimer();
}

void ZipkinSpanExporterHandler::send(std::vector<::opencensus::trace::exporter::SpanData>&& spans) {
  Http::RequestMessagePtr message = std::make_unique<Http::RequestMessageImpl>();
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setPath(collector_path_);
  message->headers().setHost(collector_cluster_);
  message->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

  Buffer::InstancePtr body = std::make_unique<Buffer::OwnedImpl>();
  const auto& serialized_spans = serializeSpans(std::move(spans));
  std::cerr << serialized_spans << "\n";
  body->add(serialized_spans);
  message->body() = std::move(body);

  cm_.httpAsyncClientForCluster(collector_cluster_)
      .send(std::move(message), *this,
            Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5000)));
}

const ProtobufWkt::Struct ZipkinSpanExporterHandler::translateSpanData(
    const ::opencensus::trace::exporter::SpanData& span_data,
    Zipkin::Utility::Replacements& replacements) const {
  ProtobufWkt::Struct span;
  auto* fields = span.mutable_fields();
  (*fields)["id"] = ValueUtil::stringValue(span_data.context().span_id().ToHex());
  (*fields)["name"] = ValueUtil::stringValue(std::string(span_data.name()));
  (*fields)["traceId"] = ValueUtil::stringValue(span_data.context().trace_id().ToHex());

  if (span_data.parent_span_id().IsValid()) {
    (*fields)["parentId"] = ValueUtil::stringValue(span_data.parent_span_id().ToHex());
  }

  if (!span_data.annotations().events().empty()) {
    std::vector<ProtobufWkt::Value> entries;
    for (const auto& annotation : span_data.annotations().events()) {
      ProtobufWkt::Struct entry;
      auto* entry_fields = entry.mutable_fields();
      (*entry_fields)[TIMESTAMP] = Zipkin::Utility::uint64Value(
          absl::ToUnixMicros(annotation.timestamp()), TIMESTAMP, replacements);
      (*entry_fields)[VALUE] = ValueUtil::stringValue(serializeAnnotation(annotation.event()));
      entries.push_back(ValueUtil::structValue(entry));
    }
    (*fields)[ANNOTATIONS] = ValueUtil::listValue(entries);
  }

  if (!span_data.message_events().events().empty()) {
    std::vector<ProtobufWkt::Value> entries;
    for (const auto& annotation : span_data.annotations().events()) {
      ProtobufWkt::Struct entry;
      auto* entry_fields = entry.mutable_fields();
      (*entry_fields)[TIMESTAMP] = Zipkin::Utility::uint64Value(
          absl::ToUnixMicros(annotation.timestamp()), TIMESTAMP, replacements);
      (*entry_fields)[VALUE] = ValueUtil::stringValue(serializeAnnotation(annotation.event()));
      entries.push_back(ValueUtil::structValue(entry));
    }
    (*fields)[ANNOTATIONS] = ValueUtil::listValue(entries);
  }

  if (!span_data.attributes().empty()) {
    ProtobufWkt::Struct object;
    for (const auto& attribute : span_data.attributes()) {
      auto* entry_fields = object.mutable_fields();
      (*entry_fields)[attribute.first] =
          ValueUtil::stringValue(attributeValueToString(attribute.second));
    }
    (*fields)["tags"] = ValueUtil::structValue(object);
  }

  (*fields)["localEndpoint"] = local_endpoint_;
  (*fields)[TIMESTAMP] = Zipkin::Utility::uint64Value(absl::ToUnixMicros(span_data.start_time()),
                                                      TIMESTAMP, replacements);
  (*fields)[DURATION] = Zipkin::Utility::uint64Value(
      absl::ToInt64Microseconds(span_data.end_time() - span_data.start_time()), DURATION,
      replacements);

  return span;
}

std::string ZipkinSpanExporterHandler::serializeSpans(
    std::vector<::opencensus::trace::exporter::SpanData>&& spans) const {
  Zipkin::Utility::Replacements replacements;
  const auto& replacement_values = replacements;
  const std::string serialized_elements = absl::StrJoin(
      spans, ",",
      [this, &replacements](std::string* out,
                            const ::opencensus::trace::exporter::SpanData& span_data) {
        absl::StrAppend(
            out, MessageUtil::getJsonStringFromMessage(translateSpanData(span_data, replacements),
                                                       /*pretty_print=*/false,
                                                       /*always_print_primitive_fields=*/true));
      });
  return absl::StrCat("[", absl::StrReplaceAll(serialized_elements, replacement_values), "]");
}

const ProtobufWkt::Value
ZipkinSpanExporterHandler::createLocalEndpoint(const LocalInfo::LocalInfo& local_info) const {
  ProtobufWkt::Struct local_endpoint;
  auto* fields = local_endpoint.mutable_fields();
  (*fields)["serviceName"] = ValueUtil::stringValue(local_info.clusterName());

  (*fields)[local_info.address()->ip()->version() == Network::Address::IpVersion::v4 ? "ipv4"
                                                                                     : "ipv6"] =
      ValueUtil::stringValue(local_info.address()->ip()->addressAsString());
  (*fields)["port"] = ValueUtil::numberValue(local_info.address()->ip()->port());
  return ValueUtil::structValue(local_endpoint);
}

} // namespace Exporters
} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
}; // namespace Envoy