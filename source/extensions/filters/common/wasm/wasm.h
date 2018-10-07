#pragma once

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm-c-api/wasm.hh"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Wasm {
class Program {
public:
  Program(const std::string& filename) : filename_{filename} {
    engine_ = wasm::Engine::make();
  }

  // A function to be called from Wasm code.
  static auto hello_callback(const wasm::vec<wasm::Val>&) -> wasm::Result {
    std::cout << "Calling back..." << std::endl;
    std::cout << "> Hello world!" << std::endl;
    return wasm::Result();
  }

  void run() {
    // Initialize.
    std::cout << "Initializing..." << std::endl;
    auto store_ = wasm::Store::make(engine_.get());
    auto store = store_.get();

    // Load binary.
    std::cout << "Loading binary..." << std::endl;
    std::ifstream file(filename_);
    file.seekg(0, std::ios_base::end);
    auto file_size = file.tellg();
    file.seekg(0);
    auto binary = wasm::vec<byte_t>::make_uninitialized(file_size);
    file.read(binary.get(), file_size);
    file.close();
    if (file.fail()) {
      std::cout << "> Error loading module!" << std::endl;
      return;
    }

    // Compile.
    std::cout << "Compiling module..." << std::endl;
    auto module = wasm::Module::make(store, binary);
    if (!module) {
      std::cout << "> Error compiling module!" << std::endl;
      return;
    }

    // Create external print functions.
    std::cout << "Creating callback..." << std::endl;
    auto hello_type =
        wasm::FuncType::make(wasm::vec<wasm::ValType*>::make(), wasm::vec<wasm::ValType*>::make());
    auto hello_func = wasm::Func::make(store, hello_type.get(), Program::hello_callback);

    // Instantiate.
    std::cout << "Instantiating module..." << std::endl;
    auto imports = wasm::vec<wasm::Extern*>::make(hello_func);
    auto instance = wasm::Instance::make(store, module.get(), imports);
    if (!instance) {
      std::cout << "> Error instantiating module!" << std::endl;
      return;
    }

    // Extract export.
    std::cout << "Extracting export..." << std::endl;
    auto exports = instance->exports();
    if (exports.size() == 0 || exports[0]->kind() != wasm::EXTERN_FUNC || !exports[0]->func()) {
      std::cout << "> Error accessing export!" << std::endl;
      return;
    }
    auto run_func = exports[0]->func();

    // Call.
    std::cout << "Calling export..." << std::endl;
    run_func->call();

    // Shut down.
    std::cout << "Shutting down..." << std::endl;
  }

private:
  const std::string filename_;
  wasm::own<wasm::Engine *> engine_;
};
} // namespace Wasm
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
