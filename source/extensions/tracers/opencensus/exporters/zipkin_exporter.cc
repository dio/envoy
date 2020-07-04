#include "extensions/tracers/opencensus/exporters/zipkin_exporter.h"

#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

#include "opencensus/trace/exporter/attribute_value.h"

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

/*
[
  {
    "id": "352bff9a74ca9ad2",
    "traceId": "5af7183fb1d4cf5f",
    "parentId": "6b221d5bc9e6496c",
    "name": "get /api",
    "timestamp": 1556604172355737,
    "duration": 1431,
    "kind": "SERVER",
    "localEndpoint": {
      "serviceName": "backend",
      "ipv4": "192.168.99.1",
      "port": 3306
    },
    "remoteEndpoint": {
      "ipv4": "172.19.0.2",
      "port": 58648
    },
    "annotations": [
      {
        "timestamp":  1556604172355737,
        "value": "ok"
      }
    ]
    "tags": {
      "http.method": "GET",
      "http.path": "/api"
    }
  }
]
*/

ProtobufWkt::Struct translateSpanData(const ::opencensus::trace::exporter::SpanData& span_data) {
  ProtobufWkt::Struct span;
  auto* fields = span.mutable_fields();
  (*fields)["id"] = ValueUtil::stringValue(span_data.context().span_id().ToHex());
  (*fields)["name"] = ValueUtil::stringValue(std::string(span_data.name()));
  (*fields)["traceId"] = ValueUtil::stringValue(span_data.context().trace_id().ToHex());

  if (span_data.parent_span_id().IsValid()) {
    (*fields)["parentId"] = ValueUtil::stringValue(span_data.parent_span_id().ToHex());
  }

  // https://github.com/census-instrumentation/opencensus-proto/blob/99162e4df59df7e6f54a8a33b80f0020627d8405/src/opencensus/proto/trace/v1/trace.proto#L187.
  if (!span_data.annotations().events().empty()) {
    std::vector<ProtobufWkt::Value> entries;
    for (const auto& annotation : span_data.annotations().events()) {
      ProtobufWkt::Struct entry;
      auto* entry_fields = entry.mutable_fields();

      // (*fields)["timestamp"] = ValueUtil::stringValue(span.context().span_id().ToHex()));
      // absl::ToUnixMicros(annotation.timestamp())

      (*entry_fields)["value"] = ValueUtil::stringValue(serializeAnnotation(annotation.event()));
      entries.push_back(ValueUtil::structValue(entry));
    }
    (*fields)["annotations"] = ValueUtil::listValue(entries);
  }

  if (!span_data.message_events().events().empty()) {
    std::vector<ProtobufWkt::Value> entries;
    for (const auto& annotation : span_data.annotations().events()) {
      ProtobufWkt::Struct entry;
      auto* entry_fields = entry.mutable_fields();

      // (*fields)["timestamp"] = ValueUtil::stringValue(span.context().span_id().ToHex()));
      // absl::ToUnixMicros(annotation.timestamp())

      (*entry_fields)["value"] = ValueUtil::stringValue(serializeAnnotation(annotation.event()));
      entries.push_back(ValueUtil::structValue(entry));
    }
    (*fields)["annotations"] = ValueUtil::listValue(entries);
  }

  if (!span_data.attributes().empty()) {
    ProtobufWkt::Struct object;
    for (const auto& attribute : span_data.attributes()) {
      auto* entry_fields = object.mutable_fields();

      // (*fields)["timestamp"] = ValueUtil::stringValue(span.context().span_id().ToHex()));
      // absl::ToUnixMicros(annotation.timestamp())

      (*entry_fields)[attribute.first] =
          ValueUtil::stringValue(attributeValueToString(attribute.second));
    }
    (*fields)["tags"] = ValueUtil::structValue(object);
  }

  /*

  // Write localEndpoint. OpenCensus does not support this by default.
  writer->Key("localEndpoint");
  writer->StartObject();
  writer->Key("serviceName");
  writer->String(service.service_name);
  if (service.af_type == ZipkinExporterOptions::AddressFamily::kIpv4) {
    writer->Key("ipv4");
  } else {
    writer->Key("ipv6");
  }
  writer->String(service.ip_address);
  writer->EndObject();

  */

  /*
    writer->Key("timestamp");
    writer->Int64(absl::ToUnixMicros(span.start_time()));

    writer->Key("duration");
    writer->Int64(absl::ToInt64Microseconds(span.end_time() - span.start_time()));
  */

  std::cerr << MessageUtil::getJsonStringFromMessage(span, false, true) << "\n";
  return span;
}

std::string serializeSpans(std::vector<::opencensus::trace::exporter::SpanData>&& spans) {
  for (const auto& span_data : spans) {
    translateSpanData(span_data);
  }
  return "";
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

  send(std::move(spans_to_be_sent));
  enableTimer();
}

void ZipkinSpanExporterHandler::send(std::vector<::opencensus::trace::exporter::SpanData>&& spans) {
  Http::RequestMessagePtr message = std::make_unique<Http::RequestMessageImpl>();
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setPath(collector_path_);
  message->headers().setHost(collector_cluster_);
  message->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

  Buffer::InstancePtr body = std::make_unique<Buffer::OwnedImpl>();
  body->add(serializeSpans(std::move(spans)));
  message->body() = std::move(body);

  cm_.httpAsyncClientForCluster(collector_cluster_)
      .send(std::move(message), *this,
            Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5000)));
}

} // namespace Exporters
} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
}; // namespace Envoy