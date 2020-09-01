#pragma once

#include "envoy/common/pure.h"
#include "opentracing/span.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class SpanContext : public opentracing::SpanContext {
public:
  uint64_t spanId() const {
    return span_id_;
  }

  void
  ForeachBaggageItem(std::function<bool(const std::string&, const std::string&)> f) const override;

private:
  uint64_t span_id_{0};
};
} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
