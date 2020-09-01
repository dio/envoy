#include "extensions/tracers/skywalking/propagation.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

void SpanContext::ForeachBaggageItem(
    std::function<bool(const std::string&, const std::string&)>) const {}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
