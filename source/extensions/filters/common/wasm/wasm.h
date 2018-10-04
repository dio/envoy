#pragma once

#include <iostream>
#include <vector>

#include "wabt/binary-reader.h"
#include "wabt/interp/binary-reader-interp.h"
#include "wabt/interp/interp.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Wasm {
class Program {
public:
  Program(const std::string& filename) : filename_{filename} {}

  void readAndRunModule() {
    wabt::Result result = readModule(filename_.c_str());
    if (wabt::Succeeded(result)) {
      wabt::interp::Executor executor(&env_, s_trace_stream_, s_thread_options_);
      wabt::interp::ExecResult exec_result = executor.RunStartFunction(module_);
      if (exec_result.result == wabt::interp::Result::Ok) {
        // Run all exported functions.
        runExports(&executor);
      }
    }
  }

private:
  wabt::Result readModule(const char* module_filename) {
    std::vector<uint8_t> file_data;
    wabt::Result result = wabt::ReadFile(module_filename, &file_data);
    if (wabt::Succeeded(result)) {
      wabt::ReadBinaryOptions options;
      result = wabt::ReadBinaryInterp(&env_, file_data.data(), file_data.size(), options, &errors_,
                                      &module_);
    }
    return result;
  }

  void runExports(wabt::interp::Executor* executor) {
    for (const wabt::interp::Export& export_ : module_->exports) {
      if (export_.kind != wabt::ExternalKind::Func) {
        continue;
      }
      // Example on running the program.wasm.
      wabt::interp::TypedValues args;
      wabt::interp::TypedValue arg(wabt::Type::I32);
      arg.set_i32(1);
      args.push_back(arg);
      wabt::interp::ExecResult exec_result = executor->RunExport(&export_, args);
      std::cerr << exec_result.values.size() << "\n";
    }
  }

  wabt::interp::Environment env_;
  wabt::interp::DefinedModule* module_ = nullptr;
  wabt::Errors errors_;
  wabt::Stream* s_trace_stream_ = nullptr;
  wabt::interp::Thread::Options s_thread_options_;
  const std::string filename_;
};
} // namespace Wasm
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
