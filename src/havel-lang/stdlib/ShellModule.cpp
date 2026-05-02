/* ShellModule.cpp - VM-native stdlib module (shell/process operations) */
#include "ShellModule.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "havel-lang/core/Value.hpp"
#include "havel-lang/compiler/vm/VM.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace fs = std::filesystem;

namespace havel::stdlib {

static std::string valueToString(const Value &v, VMApi &api) {
  return api.vm.resolveStringKey(v);
}

static Value makeStringId(const std::string &s, VMApi &api) {
  auto strRef = api.vm.getHeap().allocateString(s);
  return Value::makeStringId(strRef.id);
}

void registerShellModule(VMApi &api) {
  api.registerFunction("shell.run",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           throw std::runtime_error(
                               "shell.run() requires command string");
                         std::string cmd = valueToString(args[0], api);
                         int ret = std::system(cmd.c_str());
                         return Value(static_cast<int64_t>(ret));
                       });

  api.registerFunction("shell.exec",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           throw std::runtime_error(
                               "shell.exec() requires command string");
                         std::string cmd = valueToString(args[0], api);
#ifndef _WIN32
                         // Simple popen wrapper for capturing output
                         FILE* pipe = popen(cmd.c_str(), "r");
                         if (!pipe) return Value::makeNull();
                         
                         char buffer[128];
                         std::string result = "";
                         while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                             result += buffer;
                         }
                         pclose(pipe);
                         return makeStringId(result, api);
#else
                         // Windows version using _popen
                         FILE* pipe = _popen(cmd.c_str(), "r");
                         if (!pipe) return Value::makeNull();
                         
                         char buffer[128];
                         std::string result = "";
                         while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                             result += buffer;
                         }
                         _pclose(pipe);
                         return makeStringId(result, api);
#endif
                       });

  auto shellObj = api.makeObject();
  api.setField(shellObj, "run", api.makeFunctionRef("shell.run"));
  api.setField(shellObj, "exec", api.makeFunctionRef("shell.exec"));
  api.setGlobal("shell", shellObj);
}

} // namespace havel::stdlib
