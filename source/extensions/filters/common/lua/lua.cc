#include "extensions/filters/common/lua/lua.h"

#include <memory>

#include "envoy/common/exception.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

Coroutine::Coroutine(const std::pair<lua_State*, lua_State*>& new_thread_state)
    : coroutine_state_(new_thread_state, false) {}

void Coroutine::start(int function_ref, int num_args, const std::function<void()>& yield_callback) {
  ASSERT(state_ == State::NotStarted);

  state_ = State::Yielded;
  lua_rawgeti(coroutine_state_.get(), LUA_REGISTRYINDEX, function_ref);
  ASSERT(lua_isfunction(coroutine_state_.get(), -1));

  // The function needs to come before the arguments but the arguments are already on the stack,
  // so we need to move it into position.
  lua_insert(coroutine_state_.get(), -(num_args + 1));
  resume(num_args, yield_callback);
}

void Coroutine::resume(int num_args, const std::function<void()>& yield_callback) {
  ASSERT(state_ == State::Yielded);
  int rc = lua_resume(coroutine_state_.get(), num_args);

  if (0 == rc) {
    state_ = State::Finished;
    ENVOY_LOG(debug, "coroutine finished");
  } else if (LUA_YIELD == rc) {
    state_ = State::Yielded;
    ENVOY_LOG(debug, "coroutine yielded");
    yield_callback();
  } else {
    state_ = State::Finished;
    const char* error = lua_tostring(coroutine_state_.get(), -1);
    throw LuaException(error);
  }
}

ThreadLocalState::ThreadLocalState(const std::string& code, ThreadLocal::SlotAllocator& tls,
                                   bool all_threads)
    : tls_slot_(all_threads ? tls.allocateSlot() : nullptr) {

  // First verify that the supplied code can be parsed.
  CSmartPtr<lua_State, lua_close> state(lua_open());
  luaL_openlibs(state.get());

  if (0 != luaL_dostring(state.get(), code.c_str())) {
    throw LuaException(fmt::format("script load error: {}", lua_tostring(state.get(), -1)));
  }

  if (all_threads) {
    // Now initialize on all threads.
    tls_slot_->set([code](Event::Dispatcher&) {
      return ThreadLocal::ThreadLocalObjectSharedPtr{new LuaThreadLocal(code)};
    });
  } else {
    local_slot_.reset(new LuaThreadLocal(code));
  }
}

int ThreadLocalState::getGlobalRef(uint64_t slot) const {
  auto& tls = local_slot_ != nullptr ? *local_slot_ : tls_slot_->getTyped<LuaThreadLocal>();
  ASSERT(tls.global_slots_.size() > slot);
  return tls.global_slots_[slot];
}

uint64_t ThreadLocalState::registerGlobal(const std::string& global) {
  if (local_slot_ != nullptr) {
    checkAndRegisterGlobal(*local_slot_, global);
  } else {
    tls_slot_->runOnAllThreads([this, global]() {
      auto& tls = tls_slot_->getTyped<LuaThreadLocal>();
      checkAndRegisterGlobal(tls, global);
    });
  }

  return current_global_slot_++;
}

void ThreadLocalState::checkAndRegisterGlobal(LuaThreadLocal& tls, const std::string& global) {
  lua_getglobal(tls.state_.get(), global.c_str());
  if (lua_isfunction(tls.state_.get(), -1)) {
    tls.global_slots_.push_back(luaL_ref(tls.state_.get(), LUA_REGISTRYINDEX));
  } else {
    ENVOY_LOG(debug, "definition for '{}' not found in script", global);
    lua_pop(tls.state_.get(), 1);
    tls.global_slots_.push_back(LUA_REFNIL);
  }
}

CoroutinePtr ThreadLocalState::createCoroutine() const {
  lua_State* state = local_slot_ == nullptr ? tls_slot_->getTyped<LuaThreadLocal>().state_.get()
                                            : local_slot_->state_.get();
  return std::make_unique<Coroutine>(std::make_pair(lua_newthread(state), state));
}

ThreadLocalState::LuaThreadLocal::LuaThreadLocal(const std::string& code) : state_(lua_open()) {
  luaL_openlibs(state_.get());
  int rc = luaL_dostring(state_.get(), code.c_str());
  ASSERT(rc == 0);
}

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
