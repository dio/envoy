#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class Tracer {};

using TracerPtr = std::unique_ptr<Tracer>;

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
