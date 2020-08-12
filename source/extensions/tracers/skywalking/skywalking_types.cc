#include "extensions/tracers/skywalking/skywalking_types.h"

#include "common/common/base64.h"
#include "common/common/utility.h"
#include "common/common/hex.h"
#include "common/common/fmt.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {

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
  if (is_new_) {
    trace_id_ = generateId(random_generator);
    trace_segment_id_ = generateId(random_generator);
  }
}

bool SpanContext::extract(Http::RequestHeaderMap& request_headers) {
  auto propagation_header = request_headers.get(propagationHeader());
  if (propagation_header == nullptr) {
    // No propagation_header means a valid span context, but an empty one.
    return true;
  }

  const auto parts =
      StringUtil::splitToken(propagation_header->value().getStringView(), "-", false, true);

  if (parts.size() != 8) {
    return false;
  }

  // TODO(dio): Per part validation.
  sampled_ = parts[0] == "0" ? 0 : 1;
  trace_id_ = base64Decode(parts[1]);
  trace_segment_id_ = base64Decode(parts[2]);
  parent_span_id_ = base64Decode(parts[3]);
  service_ = base64Decode(parts[4]);
  service_instance_ = base64Decode(parts[5]);
  parent_endpoint_ = base64Decode(parts[6]);
  caller_address_ = base64Decode(parts[7]);

  is_new_ = false;

  return true;
}

void SpanContext::inject(Http::RequestHeaderMap& request_headers) const {
  // Reference on standard header value:
  // https://github.com/apache/skywalking/blob/6fe2041b470113e626cb3f41e3789261d31f2548/docs/en/protocols/Skywalking-Cross-Process-Propagation-Headers-Protocol-v3.md#standard-header-item.
  const auto value =
      absl::StrCat(sampled_, "-", base64Encode(trace_id_), "-", base64Encode(trace_segment_id_),
                   "-", base64Encode(parent_span_id_), "-", base64Encode(service_), "-",
                   base64Encode(service_instance_), "-", base64Encode(parent_endpoint_), "-",
                   base64Encode(caller_address_));
  request_headers.setReferenceKey(propagationHeader(), value);
}

void SpanObject::finish() { end_time_ = DateUtil::nowToMilliseconds(time_source_); }

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy