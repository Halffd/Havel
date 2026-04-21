/* ShellModule.cpp - VM-native stdlib module (shell/process operations) */
#include "ShellModule.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

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
                         (void)api;
                         return Value(static_cast<int64_t>(ret));
                       });

  api.registerFunction("shell.exec",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           throw std::runtime_error(
                               "shell.exec() requires command string");
                         std::string cmd = valueToString(args[0], api);

                         std::string stdout_str;
                         FILE *pipe = popen(cmd.c_str(), "r");
                         if (!pipe) {
                           auto resultRef = api.vm.createHostObject();
                           api.vm.setHostObjectField(resultRef, "stdout",
                                                     makeStringId("", api));
                           api.vm.setHostObjectField(resultRef, "stderr",
                                                     makeStringId("", api));
                           api.vm.setHostObjectField(
                               resultRef, "exitCode",
                               Value::makeInt(static_cast<int64_t>(-1)));
                           return Value::makeObjectId(resultRef.id);
                         }

                         char buf[4096];
                         while (fgets(buf, sizeof(buf), pipe)) {
                           stdout_str += buf;
                         }
                         int exitCode = pclose(pipe);

                         auto resultRef = api.vm.createHostObject();
                         api.vm.setHostObjectField(resultRef, "stdout",
                                                   makeStringId(stdout_str, api));
                         api.vm.setHostObjectField(resultRef, "stderr",
                                                   makeStringId("", api));
                         api.vm.setHostObjectField(
                             resultRef, "exitCode",
                             Value::makeInt(static_cast<int64_t>(exitCode)));
                         return Value::makeObjectId(resultRef.id);
                       });

  api.registerFunction("shell.which",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           return Value::makeNull();
                         std::string name = valueToString(args[0], api);
                         const char *pathEnv = std::getenv("PATH");
                         if (!pathEnv)
                           return Value::makeNull();

                         std::istringstream ss(pathEnv);
                         std::string dir;
                         while (std::getline(ss, dir, ':')) {
                           fs::path candidate = fs::path(dir) / name;
                           if (fs::exists(candidate) &&
                               fs::is_regular_file(candidate)) {
                             return makeStringId(candidate.string(), api);
                           }
                         }
                         return Value::makeNull();
                       });

  api.registerFunction("shell.env",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           return Value::makeNull();
                         std::string name = valueToString(args[0], api);
                         if (args.size() >= 2) {
                           std::string val = valueToString(args[1], api);
                           int ret = setenv(name.c_str(), val.c_str(), 1);
                           return Value::makeBool(ret == 0);
                         }
                         const char *val = std::getenv(name.c_str());
                         if (!val)
                           return Value::makeNull();
                         return makeStringId(val, api);
                       });

  api.registerFunction("shell.getcwd",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         return makeStringId(
                             fs::current_path().string(), api);
                       });

  api.registerFunction("shell.cd",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           return Value::makeBool(false);
                         std::string path = valueToString(args[0], api);
                         return Value::makeBool(chdir(path.c_str()) == 0);
                       });

  api.registerFunction("shell.escape",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           return makeStringId("''", api);
                         std::string input = valueToString(args[0], api);
                         std::string escaped = "'";
                         for (char c : input) {
                           if (c == '\'') {
                             escaped += "'\\''";
                           } else {
                             escaped += c;
                           }
                         }
                         escaped += "'";
                         return makeStringId(escaped, api);
                       });

  auto shellObj = api.makeObject();
  api.setField(shellObj, "run", api.makeFunctionRef("shell.run"));
  api.setField(shellObj, "exec", api.makeFunctionRef("shell.exec"));
  api.setField(shellObj, "which", api.makeFunctionRef("shell.which"));
  api.setField(shellObj, "env", api.makeFunctionRef("shell.env"));
  api.setField(shellObj, "getcwd", api.makeFunctionRef("shell.getcwd"));
  api.setField(shellObj, "cd", api.makeFunctionRef("shell.cd"));
  api.setField(shellObj, "escape", api.makeFunctionRef("shell.escape"));
  api.setGlobal("shell", shellObj);
}

} // namespace havel::stdlib
