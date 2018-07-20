#pragma once

#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TextProtocolInspector {

/**
 * Global configuration for Text Protocol inspector.
 */
class Config {
public:
  Config(Stats::Scope& scope);
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * Text protocol inspector listener filter.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr config);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

private:
  void onRead();
  void onTimeout();
  void done(bool success);

  ConfigSharedPtr config_;
  Network::ListenerFilterCallbacks* cb_;
  Event::FileEventPtr file_event_;
  Event::TimerPtr timer_;

  uint64_t read_{0};
  static thread_local uint8_t buf_[64 * 1024];

  friend class Config;
};

} // namespace TextProtocolInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
