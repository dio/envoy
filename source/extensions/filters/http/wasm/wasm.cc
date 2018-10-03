#include "extensions/filters/http/wasm/wasm.h"

#include "absl/strings/match.h"
#include "common/http/utility.h"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "wabt/binary-reader.h"
#include "wabt/cast.h"
#include "wabt/error-formatter.h"
#include "wabt/feature.h"
#include "wabt/interp/binary-reader-interp.h"
#include "wabt/interp/interp.h"
#include "wabt/literal.h"
#include "wabt/option-parser.h"
#include "wabt/resolve-names.h"
#include "wabt/stream.h"
#include "wabt/validator.h"
#include "wabt/wast-lexer.h"
#include "wabt/wast-parser.h"

using namespace wabt;
using namespace wabt::interp;

static int s_verbose;
static Thread::Options s_thread_options;
static Stream* s_trace_stream;
static bool s_host_print;
static Features s_features;

static std::unique_ptr<FileStream> s_log_stream;
static std::unique_ptr<FileStream> s_stdout_stream;

enum class RunVerbosity {
  Quiet = 0,
  Verbose = 1,
};

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

static const std::string& PREFIX() { CONSTRUCT_ON_FIRST_USE(std::string, "Basic "); }

static void RunAllExports(interp::Module* module, Executor* executor, RunVerbosity verbose) {
  TypedValues args;
  TypedValue a(Type::I32);
  a.set_i32(1);
  args.push_back(a);
  TypedValues results;
  for (const interp::Export& export_ : module->exports) {
    if (export_.kind != ExternalKind::Func) {
      continue;
    }
    ExecResult exec_result = executor->RunExport(&export_, args);
    if (verbose == RunVerbosity::Verbose) {
      std::cerr << "Size: " << exec_result.values.size() << "\n";
      // WriteCall(s_stdout_stream.get(), string_view(), export_.name, args, exec_result.values,
      //          exec_result.result);
    }
  }
}

static wabt::Result ReadModule(const char* module_filename, Environment* env, Errors* errors,
                               DefinedModule** out_module) {
  wabt::Result result;
  std::vector<uint8_t> file_data;

  *out_module = nullptr;

  result = ReadFile(module_filename, &file_data);
  if (Succeeded(result)) {
    const bool kReadDebugNames = true;
    const bool kStopOnFirstError = true;
    const bool kFailOnCustomSectionError = true;
    ReadBinaryOptions options(s_features, s_log_stream.get(), kReadDebugNames, kStopOnFirstError,
                              kFailOnCustomSectionError);
    result = ReadBinaryInterp(env, file_data.data(), file_data.size(), options, errors, out_module);

    if (Succeeded(result)) {
      if (s_verbose) {
        env->DisassembleModule(s_stdout_stream.get(), *out_module);
      }
    }
  }
  return result;
}

static interp::Result PrintCallback(const HostFunc* func, const interp::FuncSignature*,
                                    const TypedValues& args, TypedValues& results) {
  printf("called host ");
  WriteCall(s_stdout_stream.get(), func->module_name, func->field_name, args, results,
            interp::Result::Ok);
  return interp::Result::Ok;
}

static void InitEnvironment(Environment* env) {
  if (s_host_print) {
    HostModule* host_module = env->AppendHostModule("host");
    host_module->on_unknown_func_export = [](Environment*, HostModule* host_module,
                                             string_view name, Index sig_index) -> Index {
      if (name != "print") {
        return kInvalidIndex;
      }

      std::pair<HostFunc*, Index> pair =
          host_module->AppendFuncExport(name, sig_index, PrintCallback);
      return pair.second;
    };
  }
}

static wabt::Result ReadAndRunModule(const char* module_filename) {
  wabt::Result result;
  Environment env;
  InitEnvironment(&env);

  Errors errors;
  DefinedModule* module = nullptr;
  result = ReadModule(module_filename, &env, &errors, &module);

  FormatErrorsToFile(errors, Location::Type::Binary);
  if (Succeeded(result)) {
    Executor executor(&env, s_trace_stream, s_thread_options);
    ExecResult exec_result = executor.RunStartFunction(module);
    if (exec_result.result == interp::Result::Ok) {
      // if (s_run_all_exports) {
      RunAllExports(module, &executor, RunVerbosity::Verbose);
    } else {
      std::cerr << "error\n";
    }
  }
  return result;
}

Http::FilterHeadersStatus BasicAuthFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  if (!authenticated(headers)) {
    ReadAndRunModule("/Users/diorahman/Downloads/program.wasm");
    std::cerr << "Yeah!\n";
    Http::Utility::sendLocalReply(
        false,
        [&](Http::HeaderMapPtr&& headers, bool end_stream) -> void {
          headers->addReferenceKey(config_->www_authenticate_, config_->realm_);
          decoder_callbacks_->encodeHeaders(std::move(headers), end_stream);
        },
        [&](Buffer::Instance& data, bool end_stream) -> void {
          decoder_callbacks_->encodeData(data, end_stream);
        },
        false, Http::Code::Unauthorized, config_->message_);
    return Http::FilterHeadersStatus::StopIteration;
  }
  return Http::FilterHeadersStatus::Continue;
}

bool BasicAuthFilter::authenticated(const Http::HeaderMap& headers) {
  if (!headers.Authorization()) {
    return false;
  }

  absl::string_view value(headers.Authorization()->value().c_str());
  if (!absl::StartsWith(value, PREFIX())) {
    return false;
  }

  absl::string_view encoded(value.substr(PREFIX().size(), value.size() - PREFIX().size()));
  return config_->encoded_.size() == encoded.size() && absl::StartsWith(config_->encoded_, encoded);
}

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy