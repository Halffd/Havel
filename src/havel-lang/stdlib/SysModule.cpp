/* SysModule.cpp - VM-native stdlib module (system information) */
#include "SysModule.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#include <windows.h>
#define getpid _getpid
#define getppid() (0)
#endif

#if defined(_WIN32) && !defined(environ)
#define environ _environ
#endif

#include "havel-lang/core/Value.hpp"
#include "havel-lang/compiler/vm/VM.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static std::string valueToString(const Value &v, VMApi &api) {
  return api.vm.resolveStringKey(v);
}

static Value makeStringId(const std::string &s, VMApi &api) {
  auto strRef = api.vm.getHeap().allocateString(s);
  return Value::makeStringId(strRef.id);
}

void registerSysModule(VMApi &api) {
  api.registerFunction("sys.platform",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
#if defined(__linux__)
                         return makeStringId("linux", api);
#elif defined(__APPLE__)
                         return makeStringId("macos", api);
#elif defined(_WIN32)
                         return makeStringId("windows", api);
#else
                         return makeStringId("unknown", api);
#endif
                       });

  api.registerFunction("sys.arch",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
#if defined(__x86_64__) || defined(_M_X64)
                         return makeStringId("x86_64", api);
#elif defined(__aarch64__) || defined(_M_ARM64)
                         return makeStringId("aarch64", api);
#elif defined(__i386__) || defined(_M_IX86)
                         return makeStringId("x86", api);
#elif defined(__arm__)
                         return makeStringId("arm", api);
#else
                         return makeStringId("unknown", api);
#endif
                       });

  api.registerFunction("sys.version",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         return makeStringId("0.1.0", api);
                       });

  api.registerFunction("sys.argv",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         auto arrRef = api.vm.createHostArray();
                         (void)arrRef;
                         return Value::makeArrayId(arrRef.id);
                       });

  api.registerFunction("sys.env",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           return Value::makeNull();
                         std::string name = valueToString(args[0], api);
                         const char *val = std::getenv(name.c_str());
                         if (!val)
                           return Value::makeNull();
                         return makeStringId(val, api);
                       });

  api.registerFunction("sys.envAll",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         auto objRef = api.vm.createHostObject();
#ifndef _WIN32
                         extern char **environ;
                         if (environ) {
                           for (int i = 0; environ[i]; i++) {
                             std::string entry(environ[i]);
                             auto eq = entry.find('=');
                             if (eq != std::string::npos) {
                               std::string key = entry.substr(0, eq);
                               std::string val = entry.substr(eq + 1);
                               api.vm.setHostObjectField(
                                  objRef, key, makeStringId(val, api));
                             }
                           }
                         }
#endif
                         return Value::makeObjectId(objRef.id);
                       });

  api.registerFunction("sys.cwd",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         return makeStringId(
                             std::filesystem::current_path().string(), api);
                       });

  api.registerFunction("sys.pid",
                       [](const std::vector<Value> &args) {
                         (void)args;
                         return Value(static_cast<int64_t>(getpid()));
                       });

  api.registerFunction("sys.ppid",
                       [](const std::vector<Value> &args) {
                         (void)args;
                         return Value(static_cast<int64_t>(getppid()));
                       });

  api.registerFunction("sys.exit",
                       [](const std::vector<Value> &args) -> Value {
                         int code = 0;
                         if (!args.empty()) {
                           if (args[0].isInt())
                             code = static_cast<int>(args[0].asInt());
                           else if (args[0].isDouble())
                             code = static_cast<int>(args[0].asDouble());
                         }
                         std::exit(code);
                         return Value::makeNull();
                       });

  api.registerFunction("sys.hostname",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         char buf[256] = {0};
#ifndef _WIN32
                         if (gethostname(buf, sizeof(buf)) != 0)
                           buf[0] = '\0';
#else
                         DWORD size = sizeof(buf);
                         if (!GetComputerNameA(buf, &size))
                             buf[0] = '\0';
#endif
                         return makeStringId(std::string(buf), api);
                       });

  api.registerFunction("sys.username",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         const char *user = std::getenv("USER");
                         if (!user)
                           user = std::getenv("LOGNAME");
                         if (!user)
                             user = std::getenv("USERNAME"); // Windows
#ifndef _WIN32
                         if (!user) {
                           char *login = getlogin();
                           if (login)
                             user = login;
                         }
#endif
                         return makeStringId(user ? std::string(user) : "",
                                             api);
                       });

  api.registerFunction("sys.home",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         const char *home = std::getenv("HOME");
                         if (!home)
                             home = std::getenv("USERPROFILE"); // Windows
                         return makeStringId(home ? std::string(home) : "",
                                             api);
                       });

  api.registerFunction("sys.tmpdir",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
#if defined(_WIN32)
                         const char *tmp = std::getenv("TEMP");
                         return makeStringId(tmp ? std::string(tmp) : "C:\\Temp",
                                             api);
#else
                         return makeStringId("/tmp", api);
#endif
                       });

  api.registerFunction("sys.shell",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         const char *sh = std::getenv("SHELL");
                         if (!sh)
                             sh = std::getenv("ComSpec"); // Windows
                         return makeStringId(sh ? std::string(sh) : "", api);
                       });

  api.registerFunction("sys.uptime",
                       [](const std::vector<Value> &args) {
                         (void)args;
                         double uptime_secs = 0.0;
#if defined(__linux__)
                         FILE *f = fopen("/proc/uptime", "r");
                         if (f) {
                           fscanf(f, "%lf", &uptime_secs);
                           fclose(f);
                         }
#elif defined(_WIN32)
                         uptime_secs = GetTickCount64() / 1000.0;
#endif
                         return Value(uptime_secs);
                       });

  auto sysObj = api.makeObject();
  api.setField(sysObj, "platform", api.makeFunctionRef("sys.platform"));
  api.setField(sysObj, "arch", api.makeFunctionRef("sys.arch"));
  api.setField(sysObj, "version", api.makeFunctionRef("sys.version"));
  api.setField(sysObj, "argv", api.makeFunctionRef("sys.argv"));
  api.setField(sysObj, "env", api.makeFunctionRef("sys.env"));
  api.setField(sysObj, "envAll", api.makeFunctionRef("sys.envAll"));
  api.setField(sysObj, "cwd", api.makeFunctionRef("sys.cwd"));
  api.setField(sysObj, "pid", api.makeFunctionRef("sys.pid"));
  api.setField(sysObj, "ppid", api.makeFunctionRef("sys.ppid"));
  api.setField(sysObj, "exit", api.makeFunctionRef("sys.exit"));
  api.setField(sysObj, "hostname", api.makeFunctionRef("sys.hostname"));
  api.setField(sysObj, "username", api.makeFunctionRef("sys.username"));
  api.setField(sysObj, "home", api.makeFunctionRef("sys.home"));
  api.setField(sysObj, "tmpdir", api.makeFunctionRef("sys.tmpdir"));
  api.setField(sysObj, "shell", api.makeFunctionRef("sys.shell"));
  api.setField(sysObj, "uptime", api.makeFunctionRef("sys.uptime"));
  api.setGlobal("sys", sysObj);
}

} // namespace havel::stdlib
