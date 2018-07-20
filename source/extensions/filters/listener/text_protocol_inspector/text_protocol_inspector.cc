#include "extensions/filters/listener/text_protocol_inspector/text_protocol_inspector.h"

#include <arpa/inet.h>

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/utility.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TextProtocolInspector {

Config::Config(Stats::Scope&) {}

Filter::Filter(const ConfigSharedPtr config) : config_(config) {}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  Network::ConnectionSocket& socket = cb.socket();
  ASSERT(file_event_ == nullptr);

  file_event_ = cb.dispatcher().createFileEvent(
      socket.fd(),
      [this](uint32_t events) {
        if (events & Event::FileReadyType::Closed) {
          done(false);
          return;
        }

        ASSERT(events == Event::FileReadyType::Read);
        onRead();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Closed);

  timer_ = cb.dispatcher().createTimer([this]() -> void { onTimeout(); });
  timer_->enableTimer(std::chrono::milliseconds(15000));

  cb_ = &cb;
  return Network::FilterStatus::StopIteration;
}

thread_local uint8_t Filter::buf_[64 * 1024];

void Filter::onRead() {
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  ssize_t n = os_syscalls.recv(cb_->socket().fd(), buf_, 64 * 1024, MSG_PEEK);
  const int error = errno; // Latch errno right after the recv call.

  if (n == -1 && error == EAGAIN) {
    return;
  } else if (n < 0) {
    done(false);
    return;
  }

  // Because we're doing a MSG_PEEK, data we've seen before gets returned every time, so
  // skip over what we've already processed.
  if (static_cast<uint64_t>(n) > read_) {
    const uint8_t* data = buf_ + read_;
    const size_t len = n - read_;
    read_ = n;

    std::vector<absl::string_view> protocols;
    if (StringUtil::caseFindToken(absl::string_view(reinterpret_cast<const char*>(data), len),
                                  " \n", "HTTP/1.1")) {
      protocols.emplace_back("http/1.1");
    }
    cb_->socket().setRequestedApplicationProtocols(protocols);
    done(true);
  }
}

void Filter::onTimeout() { done(false); }

void Filter::done(bool success) {
  timer_.reset();
  file_event_.reset();
  cb_->continueFilterChain(success);
}

} // namespace TextProtocolInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
