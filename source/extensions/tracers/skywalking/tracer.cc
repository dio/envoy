#include "extensions/tracers/skywalking/tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

Tracer::~Tracer() { reporter_->closeStream(); }

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
