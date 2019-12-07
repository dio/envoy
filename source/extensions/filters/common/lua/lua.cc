#include "extensions/filters/common/lua/lua.h"

#include <memory>
#include <string>

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

ThreadLocalState::ThreadLocalState(const std::vector<LuaCode>& codes,
                                   ThreadLocal::SlotAllocator& tls) {
  for (const auto& code : codes) {
    tls_slot_map_[code.name] = tls.allocateSlot();
    current_global_slot_map_[code.name] = 0;

    // Firstly, verify that the supplied code can be parsed.
    CSmartPtr<lua_State, lua_close> state(lua_open());
    luaL_openlibs(state.get());

    if (0 != luaL_dostring(state.get(), code.code.c_str())) {
      throw LuaException(fmt::format("script load error: {}", lua_tostring(state.get(), -1)));
    }

    // Now initialize on all threads.
    tls_slot_map_.at(code.name)->set([code](Event::Dispatcher&) {
      return ThreadLocal::ThreadLocalObjectSharedPtr{new LuaThreadLocal(code.code)};
    });
  }
}

int ThreadLocalState::getGlobalRef(const std::string& name, uint64_t slot) {
  LuaThreadLocal& tls = tls_slot_map_.at(name)->getTyped<LuaThreadLocal>();
  ASSERT(tls.global_slots_.size() > slot);
  return tls.global_slots_[slot];
}

uint64_t ThreadLocalState::registerGlobal(const std::string& name, const std::string& global) {
  tls_slot_map_.at(name)->runOnAllThreads([this, name, global]() {
    LuaThreadLocal& tls = tls_slot_map_.at(name)->getTyped<LuaThreadLocal>();
    lua_getglobal(tls.state_.get(), global.c_str());
    if (lua_isfunction(tls.state_.get(), -1)) {
      tls.global_slots_.push_back(luaL_ref(tls.state_.get(), LUA_REGISTRYINDEX));
    } else {
      ENVOY_LOG(debug, "definition for '{}' not found in script", global);
      lua_pop(tls.state_.get(), 1);
      tls.global_slots_.push_back(LUA_REFNIL);
    }
  });

  return current_global_slot_map_.at(name)++;
}

CoroutinePtr ThreadLocalState::createCoroutine(const std::string& name) {
  lua_State* state = tls_slot_map_.at(name)->getTyped<LuaThreadLocal>().state_.get();
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
