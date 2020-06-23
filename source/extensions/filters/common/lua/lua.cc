#include "extensions/filters/common/lua/lua.h"

#include <lua.h>
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
  /*if (name.empty()) {
    lua_rawgeti(coroutine_state_.get(), LUA_REGISTRYINDEX, function_ref);
  } else {
    lua_rawgeti(coroutine_state_.get(), LUA_REGISTRYINDEX, function_ref);
  }*/

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

ThreadLocalState::ThreadLocalState(const std::string& code, ThreadLocal::SlotAllocator& tls)
    : tls_slot_(tls.allocateSlot()) {

  // First verify that the supplied code can be parsed.
  CSmartPtr<lua_State, lua_close> state(lua_open());
  ASSERT(state.get() != nullptr, "unable to create new lua state object");
  luaL_openlibs(state.get());

  if (0 != luaL_dostring(state.get(), code.c_str())) {
    throw LuaException(fmt::format("script load error: {}", lua_tostring(state.get(), -1)));
  }

  // Now initialize on all threads.
  tls_slot_->set([code](Event::Dispatcher&) {
    return ThreadLocal::ThreadLocalObjectSharedPtr{new LuaThreadLocal(code)};
  });
}

int ThreadLocalState::getGlobalRef(uint64_t slot) {
  LuaThreadLocal& tls = tls_slot_->getTyped<LuaThreadLocal>();
  ASSERT(tls.global_slots_.size() > slot);
  return tls.global_slots_[slot];
}

uint64_t ThreadLocalState::registerGlobal(const std::string& global) {
  tls_slot_->runOnAllThreads([this, global]() {
    LuaThreadLocal& tls = tls_slot_->getTyped<LuaThreadLocal>();
    lua_getglobal(tls.state_.get(), global.c_str());
    if (lua_isfunction(tls.state_.get(), -1)) {
      int ref = luaL_ref(tls.state_.get(), LUA_REGISTRYINDEX);
      tls.global_slots_.push_back(ref);
      tls.function_refs_.insert_or_assign(global, ref);
    } else {
      ENVOY_LOG(debug, "definition for '{}' not found in script", global);
      lua_pop(tls.state_.get(), 1);
      tls.global_slots_.push_back(LUA_REFNIL);
    }
  });

  return current_global_slot_++;
}

CoroutinePtr ThreadLocalState::createCoroutine() {
  lua_State* state = tls_slot_->getTyped<LuaThreadLocal>().state_.get();
  return std::make_unique<Coroutine>(std::make_pair(lua_newthread(state), state));
}

ThreadLocalState::LuaThreadLocal::LuaThreadLocal(const std::string& code) : state_(lua_open()) {
  ASSERT(state_.get() != nullptr, "unable to create new lua state object");
  luaL_openlibs(state_.get());
  int rc = luaL_dostring(state_.get(), code.c_str());
  ASSERT(rc == 0);
}

int ThreadLocalState::updateCode(const std::string& code, const std::vector<std::string>& names,
                                 const std::string& signature, const std::string& function_name) {
  auto& slot = tls_slot_->getTyped<LuaThreadLocal>();
  auto* state = slot.state_.get();

  auto key = absl::StrCat(function_name, signature);
  auto it = slot.function_refs_.find(key);

  if (it == slot.function_refs_.end()) {

    if (0 != luaL_dostring(state, code.c_str())) {
      throw LuaException(fmt::format("script load error: {}", lua_tostring(state, -1)));
    }

    int current_ref = 0;
    for (const auto& name : names) {
      lua_getglobal(state, name.c_str());
      lua_isfunction(state, -1);
      int ref = luaL_ref(state, LUA_REGISTRYINDEX);
      slot.function_refs_.insert_or_assign(key, ref);
      if (name == function_name) {
        current_ref = ref;
      }
    }

    ASSERT(current_ref > 0);
    return current_ref;
  }

  return it->second;
}

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
