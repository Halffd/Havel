/* SysModule.cpp - VM-native stdlib module (system information) */
#include "SysModule.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <tlhelp32.h>
#endif

#include "havel-lang/core/Value.hpp"
#ifdef HAVEL_ENABLE_LLVM
#include "havel-lang/compiler/BytecodeOrcJIT.h"
#endif

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

void registerSysModule(VMApi &api) {
  api.registerFunction("sys.platform",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
#if defined(__linux__)
                         return api.makeString("linux");
#elif defined(__APPLE__)
                         return api.makeString("macos");
#elif defined(_WIN32)
                         return api.makeString("windows");
#else
                         return api.makeString("unknown");
#endif
                       });

  api.registerFunction("sys.arch",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
#if defined(__x86_64__) || defined(_M_X64)
                         return api.makeString("x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
                         return api.makeString("aarch64");
#elif defined(__i386__) || defined(_M_IX86)
                         return api.makeString("x86");
#elif defined(__arm__)
                         return api.makeString("arm");
#else
                         return api.makeString("unknown");
#endif
                       });

  api.registerFunction("sys.version",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         return api.makeString("0.1.0");
                       });

  api.registerFunction("sys.argv",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
  auto arr = api.makeArray();
  (void)arr;
  return arr;
                       });

  api.registerFunction("sys.env",
                       [&api](const std::vector<Value> &args) {
                         if (args.empty())
                           return Value::makeNull();
                         std::string name = api.resolveString(args[0]);
                         const char *val = std::getenv(name.c_str());
                         if (!val)
                           return Value::makeNull();
                         return api.makeString(val);
                       });

  api.registerFunction("sys.envAll",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
  auto obj = api.makeObject();
#ifndef _WIN32
  extern char **environ;
  if (::environ) {
    for (int i = 0; ::environ[i]; i++) {
      std::string entry(::environ[i]);
      auto eq = entry.find('=');
      if (eq != std::string::npos) {
        std::string key = entry.substr(0, eq);
        std::string val = entry.substr(eq + 1);
        api.setField(obj, key, api.makeString(val));
      }
    }
  }
#endif
  return obj;
});

  api.registerFunction("sys.cwd",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
  return api.makeString(
      std::filesystem::current_path().string());
});

  api.registerFunction("sys.pid",
                       [](const std::vector<Value> &args) {
                         (void)args;
#ifndef _WIN32
                         return Value(static_cast<int64_t>(getpid()));
#else
                         return Value(static_cast<int64_t>(GetCurrentProcessId()));
#endif
                       });

  api.registerFunction("sys.ppid",
                       [](const std::vector<Value> &args) {
                         (void)args;
#ifndef _WIN32
                         return Value(static_cast<int64_t>(getppid()));
#else
                         // Best-effort parent PID on Windows via ToolHelp snapshot.
                         DWORD current = GetCurrentProcessId();
                         DWORD parent = 0;
                         HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                         if (snapshot != INVALID_HANDLE_VALUE) {
                           PROCESSENTRY32 pe{};
                           pe.dwSize = sizeof(pe);
                           if (Process32First(snapshot, &pe)) {
                             do {
                               if (pe.th32ProcessID == current) {
                                 parent = pe.th32ParentProcessID;
                                 break;
                               }
                             } while (Process32Next(snapshot, &pe));
                           }
                           CloseHandle(snapshot);
                         }
                         return Value(static_cast<int64_t>(parent));
#endif
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
#ifndef _WIN32
                         char buf[256];
                         if (gethostname(buf, sizeof(buf)) != 0)
                           buf[0] = '\0';
                         return api.makeString(std::string(buf));
#else
                         char buf[256];
                         DWORD size = static_cast<DWORD>(sizeof(buf));
                         if (!GetComputerNameA(buf, &size)) {
                           buf[0] = '\0';
                         }
                         return api.makeString(std::string(buf));
#endif
                       });

  api.registerFunction("sys.username",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         const char *user = std::getenv("USER");
                         if (!user)
                           user = std::getenv("LOGNAME");
#ifndef _WIN32
                         if (!user) {
                           char *login = getlogin();
                           if (login)
                             user = login;
                         }
#else
                         if (!user)
                           user = std::getenv("USERNAME");
#endif
  return api.makeString(user ? std::string(user) : "");
});

  api.registerFunction("sys.home",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         const char *home = std::getenv("HOME");
                         if (!home)
                           home = std::getenv("USERPROFILE");
  return api.makeString(home ? std::string(home) : "");
});

  api.registerFunction("sys.tmpdir",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
#ifdef _WIN32
                         const char *tmp = std::getenv("TEMP");
                         if (!tmp)
                           tmp = std::getenv("TMP");
                         return api.makeString(tmp ? std::string(tmp) : "C:\\Windows\\Temp");
#else
                         return api.makeString("/tmp");
#endif
                       });

  api.registerFunction("sys.shell",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
#ifdef _WIN32
                         const char *sh = std::getenv("ComSpec");
                         if (!sh)
                           sh = "cmd.exe";
#else
                         const char *sh = std::getenv("SHELL");
#endif
                         return api.makeString(sh ? std::string(sh) : "");
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
#endif
                         return Value(uptime_secs);
                       });

  api.registerFunction("jit.last_error",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
#ifdef HAVEL_ENABLE_LLVM
                         return api.makeString(havel::compiler::BytecodeOrcJIT::lastError());
#else
                         return api.makeString("");
#endif
                       });

  api.registerFunction("jit.clear_error",
                       [](const std::vector<Value> &args) {
                         (void)args;
#ifdef HAVEL_ENABLE_LLVM
                         havel::compiler::BytecodeOrcJIT::clearLastError();
#endif
                         return Value::makeNull();
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

  auto jitObj = api.makeObject();
  api.setField(jitObj, "last_error", api.makeFunctionRef("jit.last_error"));
  api.setField(jitObj, "clear_error", api.makeFunctionRef("jit.clear_error"));
  api.setGlobal("jit", jitObj);
}

} // namespace havel::stdlib
