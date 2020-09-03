#include "extensions/tracers/skywalking/skywalking_types.h"

#include "common/common/base64.h"
#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {

// The standard header name is "sw8", as mentioned in:
// https://github.com/apache/skywalking/blob/6fe2041b470113e626cb3f41e3789261d31f2548/docs/en/protocols/Skywalking-Cross-Process-Propagation-Headers-Protocol-v3.md#standard-header-item.
const Http::LowerCaseString& propagationHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "sw8");
}

std::string generateId(Random::RandomGenerator& random_generator) {
  return absl::StrCat(Hex::uint64ToHex(random_generator.random()),
                      Hex::uint64ToHex(random_generator.random()));
}

std::string base64Encode(absl::string_view input) {
  return Base64::encode(input.data(), input.length());
}

std::string base64Decode(absl::string_view input) { return Base64::decode(std::string(input)); }

} // namespace

void SpanContext::initialize(Random::RandomGenerator& random_generator) {
  if (trace_id_.empty()) {
    trace_id_ = generateId(random_generator);
  }
  trace_segment_id_ = generateId(random_generator);
}

const SpanContextResult SpanContext::extract(Http::RequestHeaderMap& request_headers,
                                             const Tracing::Decision tracing_decision) {
  auto propagation_header = request_headers.get(propagationHeader());
  SpanContextResult result;
  result.to_trace_ = tracing_decision.traced;
  if (propagation_header == nullptr) {
    // No propagation_header means a valid span context, but an empty one.
    return result;
  }

  const auto parts =
      StringUtil::splitToken(propagation_header->value().getStringView(), "-", false, true);
  // Reference:
  // https://github.com/apache/skywalking/blob/6fe2041b470113e626cb3f41e3789261d31f2548/docs/en/protocols/Skywalking-Cross-Process-Propagation-Headers-Protocol-v3.md#values.
  if (parts.size() != 8) {
    result.to_trace_ = false;
    return result;
  }

  SpanContext span_context(parts[0] == "1" ? 1 : 0);

  // TODO(dio): Per part validation.
  span_context.setTraceId(base64Decode(parts[1]));
  span_context.setTraceSegmentId(base64Decode(parts[2]));
  span_context.setParentSpanId(std::stoi(std::string(parts[3])));
  span_context.setService(base64Decode(parts[4]));
  span_context.setServiceInstance(base64Decode(parts[5]));
  span_context.setParentEndpoint(base64Decode(parts[6]));
  span_context.setNetworkAddressUsedAtPeer(base64Decode(parts[7]));

  result.context_ = absl::optional<SpanContext>(span_context);
  return result;
}

void SpanContext::inject(Http::RequestHeaderMap& request_headers) const {
  // Reference:
  // https://github.com/apache/skywalking/blob/6fe2041b470113e626cb3f41e3789261d31f2548/docs/en/protocols/Skywalking-Cross-Process-Propagation-Headers-Protocol-v3.md#standard-header-item.
  const auto value = absl::StrCat(
      sampled_, "-", base64Encode(trace_id_), "-", base64Encode(trace_segment_id_), "-",
      parent_span_id_, "-", base64Encode(service_), "-", base64Encode(service_instance_), "-",
      base64Encode(parent_endpoint_), "-", base64Encode(network_address_used_at_peer_));
  request_headers.setReferenceKey(propagationHeader(), value);
}

void SpanObject::finish() { end_time_ = DateUtil::nowToMilliseconds(time_source_); }

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
