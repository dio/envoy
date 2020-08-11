#pragma once

#include <stdint.h>
#include <string>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

struct SpanObject {
  int32_t span_id_;
};

struct SegmentContext {
  std::string trace_id_;
};

struct SpanObjectSegment {
  SegmentContext segment_context_;
  SpanObject span_object_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy