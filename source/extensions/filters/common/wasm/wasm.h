#pragma once

#include <cinttypes>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "envoy/common/exception.h"

#include "wasm-c-api/wasm.hh"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Wasm {
class Program {
public:
  Program(const std::string& filename) : binary_{load(filename)} { engine_ = wasm::Engine::make(); }

  static auto hello_callback(const wasm::vec<wasm::Val>&) -> wasm::Result {
    std::cout << "Calling back..." << std::endl;
    std::cout << "> Hello world!" << std::endl;
    return wasm::Result();
  }

  void run() {
    auto store = wasm::Store::make(engine_.get());
    auto module = wasm::Module::make(store.get(), binary_);
    if (!module) {
      throw EnvoyException("Failed to compile the module");
    }

    auto hello_type =
        wasm::FuncType::make(wasm::vec<wasm::ValType*>::make(), wasm::vec<wasm::ValType*>::make());
    auto hello_func = wasm::Func::make(store.get(), hello_type.get(), Program::hello_callback);

    auto imports = wasm::vec<wasm::Extern*>::make(hello_func);
    auto instance = wasm::Instance::make(store.get(), module.get(), imports);
    if (!instance) {
      throw EnvoyException("Failed to instantiate the module");
    }

    auto exports = instance->exports();
    if (exports.size() == 0 || exports[0]->kind() != wasm::EXTERN_FUNC || !exports[0]->func()) {
      throw EnvoyException("Failed to access export");
    }
    auto run_func = exports[0]->func();
    run_func->call();
  }

private:
  wasm::Name load(const std::string& filename) {
    std::ifstream file(filename);
    file.seekg(0, std::ios_base::end);
    auto file_size = file.tellg();
    file.seekg(0);
    auto binary = wasm::vec<byte_t>::make_uninitialized(file_size);
    file.read(binary.get(), file_size);
    file.close();
    if (file.fail()) {
      throw EnvoyException("Failed to load the module");
    }
    return binary;
  }

  const wasm::Name binary_;
  wasm::own<wasm::Engine*> engine_;
};
} // namespace Wasm
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
