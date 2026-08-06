/* ShellModule.cpp - VM-native stdlib module (shell/process operations)
   Multi-platform: Linux, macOS, BSD, Windows */
#include "ShellModule.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include "utils/ExitHandler.hpp"

#ifndef _WIN32
  #include <unistd.h>
  #include <sys/types.h>
  #include <pwd.h>
#else
  #include <direct.h>      // _chdir
  #include <windows.h>     // SetEnvironmentVariableA, GetComputerName, ...
  #include <io.h>          // _isatty
#endif

#include "havel-lang/core/Value.hpp"
#include "havel-lang/runtime/concurrency/Fiber.hpp"
#include "core/process/Launcher.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace fs = std::filesystem;

namespace havel::stdlib {

// ============================================================================
// Helper: merge exports from a module into a target object
// ============================================================================

static void mergeExports(const VMApi &api, Value targetObj, Value exports) {
  auto &vm = api.vm();
  if (!exports.isObjectId()) return;
  auto *obj = vm.getHeap().object(exports.asObjectId());
  if (!obj) return;
  for (const auto& [name, value] : *obj) {
    if (name.empty() || name[0] == '_') continue;
    api.setField(targetObj, name, value);
  }
}

// ============================================================================
// Path validation to prevent path traversal
// ============================================================================

static bool isPathAllowed(const std::string& path) {
    // Resolve the path to canonical form
    std::error_code ec;
    fs::path resolved = fs::canonical(path, ec);
    if (ec) {
        // If canonical fails (e.g., path doesn't exist), try to resolve parent
        resolved = fs::absolute(path, ec);
        if (ec) return false;
    }
    
    // Get allowed base directories
    std::vector<fs::path> allowedBases;
    
    // Current working directory
    allowedBases.push_back(fs::current_path(ec));
    if (ec) allowedBases.clear();
    
    // Home directory
    const char* home = std::getenv("HOME");
    if (home) allowedBases.push_back(fs::path(home));
    
    // Temp directory
    allowedBases.push_back(fs::temp_directory_path());
    
    // Check if resolved path is within any allowed base
    for (const auto& base : allowedBases) {
        fs::path canonicalBase = fs::canonical(base, ec);
        if (ec) continue;
        
        auto baseStr = canonicalBase.string();
        auto resolvedStr = resolved.string();
        
        if (resolvedStr.rfind(baseStr, 0) == 0) {
            return true;
        }
    }
    
    return false;
}

// ============================================================================
// Command validation to prevent command injection
// ============================================================================

// Whitelist of allowed commands for shell execution
static const std::vector<std::string> ALLOWED_SHELL_COMMANDS = {
    "ls", "cat", "echo", "grep", "find", "wc", "head", "tail",
    "mkdir", "rmdir", "cp", "mv", "rm", "touch", "stat",
    "ps", "df", "du", "free", "uptime", "whoami", "id",
    "date", "cal", "sleep", "sort", "uniq", "cut", "tr",
    "awk", "sed", "tee", "xargs", "which", "whereis",
    "git", "cargo", "npm", "make", "cmake", "clang", "gcc",
    "python3", "python", "node", "deno", "bun",
    "ssh", "scp", "rsync", "curl", "wget", "ping", "dig",
    "tar", "gzip", "gunzip", "zip", "unzip", "bzip2", "bunzip2"
};

static bool isCommandAllowed(const std::string& command) {
    // Extract the first word (command name)
    std::string cmd;
    size_t pos = command.find_first_not_of(" \t");
    if (pos != std::string::npos) {
        size_t end = command.find_first_of(" \t", pos);
        cmd = command.substr(pos, end - pos);
    }
    
    // Get basename of command (in case full path provided)
    size_t slashPos = cmd.rfind('/');
    if (slashPos != std::string::npos) {
        cmd = cmd.substr(slashPos + 1);
    }
    
    for (const auto& allowed : ALLOWED_SHELL_COMMANDS) {
        if (allowed == cmd) return true;
    }
    return false;
}

static bool isSafeCommand(const std::string& command) {
    // Reject commands with dangerous shell metacharacters
    // that could be used for command injection
    static const std::vector<std::string> DANGEROUS_PATTERNS = {
        ";", "&&", "||", "|", "`", "$(", "${", ">", "<", ">>",
        "2>", "&>", "exec", "eval", "source", ". "
    };
    
    for (const auto& pattern : DANGEROUS_PATTERNS) {
        if (command.find(pattern) != std::string::npos) {
            return false;
        }
    }
    return true;
}

static bool validateShellCommand(const std::string& command) {
    // Allow empty command (will fail later with appropriate error)
    if (command.empty()) return false;
    
    // Check for dangerous patterns first
    if (!isSafeCommand(command)) return false;
    
    // Check if command is in allowlist
    if (!isCommandAllowed(command)) return false;
    
    return true;
}

// Return a string describing the current platform
static std::string getPlatform() {
#ifdef _WIN32
  return "windows";
#elif __APPLE__
  #if TARGET_OS_MAC
    return "macos";
  #else
    return "apple_other";
  #endif
#elif __linux__
  return "linux";
#elif __FreeBSD__
  return "freebsd";
#elif __OpenBSD__
  return "openbsd";
#elif __NetBSD__
  return "netbsd";
#elif __unix__
  return "unix";
#else
  return "unknown";
#endif
}

// Enumerate environment variables into an object (name → value)
static Value listEnvironment(const VMApi &api) {
  auto envObj = api.makeObject();

#ifdef _WIN32
  char *envBlock = GetEnvironmentStringsA();
  if (envBlock) {
    for (char *cur = envBlock; *cur; ) {
      std::string entry(cur);
      auto eqPos = entry.find('=');
      if (eqPos != std::string::npos) {
        std::string key = entry.substr(0, eqPos);
        std::string val = entry.substr(eqPos + 1);
        api.setField(envObj, key, api.makeString(val));
      }
      cur += entry.size() + 1;  // next null‑terminated entry
    }
    FreeEnvironmentStringsA(envBlock);
  }
#else
        extern char **environ;
        if (::environ) {
          for (char **cur = ::environ; *cur; ++cur) {
      std::string entry(*cur);
      auto eqPos = entry.find('=');
      if (eqPos != std::string::npos) {
        std::string key = entry.substr(0, eqPos);
        std::string val = entry.substr(eqPos + 1);
        api.setField(envObj, key, api.makeString(val));
      }
    }
  }
#endif
  return envObj;
}

// Simple argument splitter (handles double quotes, single quotes, backslash escapes)
static std::vector<std::string> splitArgs(const std::string& cmd) {
  std::vector<std::string> args;
  std::string current;
  bool inDQuote = false, inSQuote = false;
  bool escape = false;

  for (size_t i = 0; i < cmd.size(); ++i) {
    char c = cmd[i];
    if (escape) {
      current += c;
      escape = false;
      continue;
    }

    if (c == '\\' && !inSQuote) {   // backslash only escapes outside single quotes
      escape = true;
      continue;
    }

    if (c == '"' && !inSQuote) {
      inDQuote = !inDQuote;
      continue;
    }

    if (c == '\'' && !inDQuote) {
      inSQuote = !inSQuote;
      continue;
    }

    if (!inDQuote && !inSQuote && std::isspace(c)) {
      if (!current.empty()) {
        args.push_back(current);
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if (!current.empty()) args.push_back(current);
  return args;
}

// Register all functions and create the global "shell" object
void registerShellModule(const VMApi &api) {
// ----------------------------------------------------------------------
// shell.run – execute command (non-blocking, returns pid on success)
// ----------------------------------------------------------------------
api.registerFunction("shell.run",
  [api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("shell.run() requires a command string");
    std::string cmd = api.resolveString(args[0]);
    
    // Validate command to prevent command injection
    if (!validateShellCommand(cmd)) {
      throw std::runtime_error("shell.run(): Command not allowed or contains dangerous patterns");
    }
    
    LaunchParams params;
    params.method = Method::Shell;
    params.detachFromParent = true;
    auto result = Launcher::run(cmd, params);
    return Value(static_cast<int64_t>(result.success ? result.pid : -1));
  });

// ----------------------------------------------------------------------
// shell.exec – capture stdout of command (returns object {stdout, stderr, exitCode})
// ----------------------------------------------------------------------
api.registerFunction("shell.exec",
  [api](const std::vector<Value> &args) {
    if (api.vm().getScheduler()) {
      api.vm().getScheduler()->yieldCurrentAndCheckTimers();
    }
    if (args.empty())
      throw std::runtime_error("shell.exec() requires a command string");
    std::string cmd = api.resolveString(args[0]);
    
    // Validate command to prevent command injection
    if (!validateShellCommand(cmd)) {
      auto result = api.makeObject();
      api.setField(result, "stdout", api.makeString(""));
      api.setField(result, "stderr", api.makeString("Command not allowed or contains dangerous patterns"));
      api.setField(result, "ok", api.makeBool(false));
      api.setField(result, "exitCode", Value::makeInt(126));
      return result;
    }
    
    auto presult = Launcher::runShell(cmd);

      auto result = api.makeObject();
      api.setField(result, "stdout", api.makeString(presult.stdout));
      api.setField(result, "stderr", api.makeString(presult.stderr));
      api.setField(result, "ok", api.makeBool(presult.success));
      api.setField(result, "exitCode",
          Value::makeInt(static_cast<int64_t>(presult.exitCode)));
      return result;
    });

  // ----------------------------------------------------------------------
  // shell.which – locate executable in PATH
  // ----------------------------------------------------------------------
  api.registerFunction("shell.which",
    [api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeNull();
      std::string name = api.resolveString(args[0]);
      const char *pathEnv = std::getenv("PATH");
      if (!pathEnv)
        return Value::makeNull();

      std::istringstream ss(pathEnv);
      std::string dir;
#ifdef _WIN32
      const char pathSep = ';';
      const std::vector<std::string> exts{".exe", ".bat", ".cmd", ".com"};
#else
      const char pathSep = ':';
#endif

      while (std::getline(ss, dir, pathSep)) {
        if (dir.empty()) continue;
        fs::path candidate = fs::path(dir) / name;

#ifdef _WIN32
        if (fs::exists(candidate) && fs::is_regular_file(candidate))
          return api.makeString(candidate.string());
        for (const auto &ext : exts) {
          fs::path withExt = candidate; withExt += ext;
          if (fs::exists(withExt) && fs::is_regular_file(withExt))
            return api.makeString(withExt.string());
        }
#else
        if (fs::exists(candidate) && fs::is_regular_file(candidate))
          return api.makeString(candidate.string());
#endif
      }
      return Value::makeNull();
    });

  // ----------------------------------------------------------------------
  // shell.env – get / set a single environment variable
  // ----------------------------------------------------------------------
  api.registerFunction("shell.env",
    [api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeNull();
      std::string name = api.resolveString(args[0]);

      if (args.size() >= 2) {  // set
        std::string val = api.resolveString(args[1]);
#ifdef _WIN32
        BOOL ok = SetEnvironmentVariableA(name.c_str(), val.c_str());
        return Value::makeBool(ok != 0);
#else
        int ret = setenv(name.c_str(), val.c_str(), 1);
        return Value::makeBool(ret == 0);
#endif
      }

      // get
      const char *val = std::getenv(name.c_str());
      if (!val)
        return Value::makeNull();
      return api.makeString(val);
    });

  // ----------------------------------------------------------------------
  // shell.cwd – current working directory
  // ----------------------------------------------------------------------
  api.registerFunction("shell.cwd",
    [api](const std::vector<Value>&) {
      return api.makeString(fs::current_path().string());
    });

  // ----------------------------------------------------------------------
  // shell.getenv – get environment variable (readonly, returns null if missing)
  // ----------------------------------------------------------------------
  api.registerFunction("shell.getenv",
    [api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeNull();
      std::string name = api.resolveString(args[0]);
      const char *val = std::getenv(name.c_str());
      if (!val)
        return Value::makeNull();
      return api.makeString(val);
    });

  // ----------------------------------------------------------------------
  // shell.cd – change directory
  // ----------------------------------------------------------------------
  api.registerFunction("shell.cd",
    [api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeBool(false);
      std::string path = api.resolveString(args[0]);
#ifdef _WIN32
      return Value::makeBool(_chdir(path.c_str()) == 0);
#else
      return Value::makeBool(chdir(path.c_str()) == 0);
#endif
    });

  // ----------------------------------------------------------------------
  // shell.escape – shell‑safe quoting
  // ----------------------------------------------------------------------
  api.registerFunction("shell.escape",
    [api](const std::vector<Value> &args) {
      if (args.empty())
#ifdef _WIN32
        return api.makeString("\"\"");
#else
        return api.makeString("''");
#endif
      std::string input = api.resolveString(args[0]);
      std::string escaped;

#ifdef _WIN32
      // Windows cmd: double quotes, double inner quotes
      escaped += '"';
      for (char c : input) {
        if (c == '"')
          escaped += "\"\"";
        else
          escaped += c;
      }
      escaped += '"';
#else
      // POSIX: single quotes, escape single quotes as '\''
      escaped = "'";
      for (char c : input) {
        if (c == '\'')
          escaped += "'\\''";
        else
          escaped += c;
      }
      escaped += "'";
#endif
      return api.makeString(escaped);
    });

  // ----------------------------------------------------------------------
  // shell.platform – returns OS identifier (e.g. "linux", "windows", "macos")
  // ----------------------------------------------------------------------
api.registerFunction("shell.platform",
[&api](const std::vector<Value>&) {
return api.makeString(getPlatform());
    });

  // ----------------------------------------------------------------------
  // shell.pid – current process ID
  // ----------------------------------------------------------------------
  api.registerFunction("shell.pid",
      [](const std::vector<Value>&) {
#ifdef _WIN32
      return Value::makeInt(static_cast<int64_t>(GetCurrentProcessId()));
#else
      return Value::makeInt(static_cast<int64_t>(getpid()));
#endif
    });

  // ----------------------------------------------------------------------
  // shell.home – user home directory path
  // ----------------------------------------------------------------------
  api.registerFunction("shell.home",
    [api](const std::vector<Value>&) {
      std::string home;
#ifdef _WIN32
      const char *drive = std::getenv("HOMEDRIVE");
      const char *path  = std::getenv("HOMEPATH");
      if (drive && path)
        home = std::string(drive) + path;
      else
        home = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
#else
      const char *h = std::getenv("HOME");
      if (h) home = h;
      else {
        // fallback using getpwuid
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
      }
#endif
      return home.empty() ? Value::makeNull() : api.makeString(home);
    });

  // ----------------------------------------------------------------------
  // shell.tmpdir – system temporary directory
  // ----------------------------------------------------------------------
  api.registerFunction("shell.tmpdir",
    [api](const std::vector<Value>&) {
#ifdef _WIN32
      char buf[MAX_PATH];
      DWORD len = GetTempPathA(MAX_PATH, buf);
      if (len > 0 && len < MAX_PATH) {
        std::string tmp(buf, len);
        // strip trailing backslash if present
        if (!tmp.empty() && tmp.back() == '\\') tmp.pop_back();
        return api.makeString(tmp);
      }
      return api.makeString(fs::temp_directory_path().string());
#else
      const char *tmp = std::getenv("TMPDIR");
      if (!tmp) tmp = std::getenv("TEMP");
      if (!tmp) tmp = std::getenv("TMP");
      if (!tmp) tmp = "/tmp";
      return api.makeString(tmp);
#endif
    });

  // ----------------------------------------------------------------------
  // shell.hostname – system host name
  // ----------------------------------------------------------------------
  api.registerFunction("shell.hostname",
    [api](const std::vector<Value>&) {
      char buf[256];
#ifdef _WIN32
      DWORD size = sizeof(buf);
      if (GetComputerNameA(buf, &size))
        return api.makeString(std::string(buf));
#else
      if (gethostname(buf, sizeof(buf)) == 0)
        return api.makeString(std::string(buf));
#endif
      return Value::makeNull();
    });

  // ----------------------------------------------------------------------
  // shell.user – current user name
  // ----------------------------------------------------------------------
  api.registerFunction("shell.user",
    [api](const std::vector<Value>&) {
#ifdef _WIN32
      char buf[256];
      DWORD size = sizeof(buf);
      if (GetUserNameA(buf, &size))
        return api.makeString(std::string(buf));
      return Value::makeNull();
#else
      const char *user = std::getenv("USER");
      if (!user) user = std::getenv("LOGNAME");
      if (!user) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) user = pw->pw_name;
      }
      return user ? api.makeString(std::string(user)) : Value::makeNull();
#endif
    });

  // ----------------------------------------------------------------------
  // shell.shell – path to the default system shell
  // ----------------------------------------------------------------------
  api.registerFunction("shell.shell",
    [api](const std::vector<Value>&) {
      std::string shell;
#ifdef _WIN32
      const char *comspec = std::getenv("ComSpec");
      if (comspec) shell = comspec;
      else {
        const char *sysroot = std::getenv("SystemRoot");
        shell = sysroot ? std::string(sysroot) + "\\System32\\cmd.exe" : "cmd.exe";
      }
#else
      const char *s = std::getenv("SHELL");
      shell = s ? s : "/bin/sh";
#endif
      return api.makeString(shell);
    });

  // ----------------------------------------------------------------------
  // shell.sleep – suspend execution for given seconds (fractional)
  // ----------------------------------------------------------------------
  api.registerFunction("shell.sleep",
  [api](const std::vector<Value> &args) {
  if (args.empty())
  throw std::runtime_error("shell.sleep() requires a number (seconds)");
  double secs = args[0].asNumber();
  int64_t ms = static_cast<int64_t>(secs * 1000.0);

  if (api.isInGoroutine()) {
  api.requestSuspension(static_cast<uint8_t>(havel::compiler::SuspensionReason::SLEEP),
  reinterpret_cast<void*>(static_cast<intptr_t>(ms)));
  return Value::makeNull();
  }

  api.chunkedSleep(ms);
  return Value::makeNull();
  });

  // ----------------------------------------------------------------------
  // shell.read – read line from stdin
  // ----------------------------------------------------------------------
  api.registerFunction("shell.read",
    [api](const std::vector<Value> &) {
      std::string line;
if (!std::getline(std::cin, line))
      return Value::makeNull();
    return api.makeString(line);
    });

  // ----------------------------------------------------------------------
  // shell.ready – non-blocking stdin check
  //   shell.ready() or shell.ready(timeoutMs) -> bool (input available)
  // ----------------------------------------------------------------------
  api.registerFunction("shell.ready",
    [](const std::vector<Value> &args) {
      int timeoutMs = 0;
      if (!args.empty() && args[0].isInt()) {
        timeoutMs = static_cast<int>(args[0].asInt());
        if (timeoutMs < 0) timeoutMs = 0;
      }
      struct pollfd pfd;
      pfd.fd = 0;
      pfd.events = POLLIN;
      pfd.revents = 0;
      int r = ::poll(&pfd, 1, timeoutMs);
      return Value::makeBool(r > 0);
    });

  // ----------------------------------------------------------------------
  // shell.write – write text to stdout (default) or stderr
  //   shell.write(text) or shell.write(text, fd) where fd: 1=stdout, 2=stderr
  // ----------------------------------------------------------------------
  api.registerFunction("shell.write",
    [api](const std::vector<Value> &args) {
      if (args.empty())
        throw std::runtime_error("shell.write() requires a string");
      std::string text = api.resolveString(args[0]);
      FILE *dest = stdout;
      if (args.size() >= 2) {
        int64_t fd = args[1].asInt();
        if (fd == 2) dest = stderr;
      }
      fputs(text.c_str(), dest);
      fflush(dest);
      return Value::makeNull();
    });

  // ----------------------------------------------------------------------
  // shell.isatty – check if a file descriptor is a terminal (0=stdin, 1=stdout, 2=stderr)
  // ----------------------------------------------------------------------
  api.registerFunction("shell.isatty",
      [](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeBool(false);
      int fd = static_cast<int>(args[0].asInt());
      if (fd < 0 || fd > 2) return Value::makeBool(false);
#ifdef _WIN32
      return Value::makeBool(_isatty(fd) != 0);
#else
      return Value::makeBool(isatty(fd) != 0);
#endif
    });

  // ----------------------------------------------------------------------
  // shell.history_path – get path to history file (~/.havel_history)
  // ----------------------------------------------------------------------
  api.registerFunction("shell.history_path",
      [api](const std::vector<Value>&) {
        std::string home;
  #ifdef _WIN32
        const char *drive = std::getenv("HOMEDRIVE");
        const char *path  = std::getenv("HOMEPATH");
        if (drive && path)
          home = std::string(drive) + path;
        else
          home = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
  #else
        const char *h = std::getenv("HOME");
        if (h) home = h;
        else {
          // fallback using getpwuid
          struct passwd *pw = getpwuid(getuid());
          if (pw) home = pw->pw_dir;
        }
  #endif
        if (home.empty()) return Value::makeNull();
        return api.makeString(home + "/.havel_history");
      });

  // ----------------------------------------------------------------------
  // shell.history_read – read history file, return array of lines
  // ----------------------------------------------------------------------
  api.registerFunction("shell.history_read",
      [api](const std::vector<Value> &args) {
        std::string path;
        if (!args.empty()) path = api.resolveString(args[0]);
        else {
          // Get default path: ~/.havel_history
          std::string home;
  #ifdef _WIN32
          const char *drive = std::getenv("HOMEDRIVE");
          const char *hpath  = std::getenv("HOMEPATH");
          if (drive && hpath)
            home = std::string(drive) + hpath;
          else
            home = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
  #else
          const char *h = std::getenv("HOME");
          if (h) home = h;
          else {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
          }
  #endif
          if (home.empty()) return api.makeArray();
          path = home + "/.havel_history";
        }
        std::ifstream f(path);
        if (!f) return api.makeArray();
        auto arr = api.makeArray();
        std::string line;
        while (std::getline(f, line)) {
          if (!line.empty())
            api.push(arr, api.makeString(line));
        }
        return arr;
      });

  // ----------------------------------------------------------------------
  // shell.history_write – write array of strings to history file
  // ----------------------------------------------------------------------
  api.registerFunction("shell.history_write",
      [api](const std::vector<Value> &args) {
        if (args.empty() || !args[0].isArrayId()) {
          throw std::runtime_error("shell.history_write: requires array argument");
        }
        std::string path;
        if (args.size() > 1) path = api.resolveString(args[1]);
        else {
          std::string home;
  #ifdef _WIN32
          const char *drive = std::getenv("HOMEDRIVE");
          const char *hpath  = std::getenv("HOMEPATH");
          if (drive && hpath)
            home = std::string(drive) + hpath;
          else
            home = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
  #else
          const char *h = std::getenv("HOME");
          if (h) home = h;
          else {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
          }
  #endif
          if (home.empty()) return Value::makeNull();
          path = home + "/.havel_history";
        }
        uint32_t arrId = args[0].asArrayId();
        havel::compiler::ArrayRef arrRef{arrId};
        size_t len = api.vm().getHostArrayLength(arrRef);
        std::ofstream f(path);
        if (!f) return Value::makeNull();
        for (size_t i = 0; i < len; ++i) {
          Value elem = api.vm().getHostArrayValue(arrRef, i);
          if (elem.isStringId()) {
            auto s = api.resolveString(elem);
            f << s << "\n";
          }
        }
        return Value::makeNull();
      });

  // ----------------------------------------------------------------------
  // shell.history_add – append a line to history file
  // ----------------------------------------------------------------------
  api.registerFunction("shell.history_add",
      [api](const std::vector<Value> &args) {
        if (args.empty()) return Value::makeNull();
        std::string line = api.resolveString(args[0]);
        std::string path;
        if (args.size() > 1) path = api.resolveString(args[1]);
        else {
          std::string home;
  #ifdef _WIN32
          const char *drive = std::getenv("HOMEDRIVE");
          const char *hpath  = std::getenv("HOMEPATH");
          if (drive && hpath)
            home = std::string(drive) + hpath;
          else
            home = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
  #else
          const char *h = std::getenv("HOME");
          if (h) home = h;
          else {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
          }
  #endif
          if (home.empty()) return Value::makeNull();
          path = home + "/.havel_history";
        }
        std::ofstream f(path, std::ios::app);
        if (!f) return Value::makeNull();
        f << line << "\n";
        return Value::makeNull();
      });

  // ----------------------------------------------------------------------
  // shell.exit – terminate the program with a status code
  // ----------------------------------------------------------------------
  api.registerFunction("shell.exit",
      [](const std::vector<Value> &args) {
      int code = 0;
      if (!args.empty()) code = static_cast<int>(args[0].asInt());
      havel::exit(ExitReason::VmExit, code);
      return Value::makeNull();
    });

  // ----------------------------------------------------------------------
  // shell.splitArgs – split a command string into a list of arguments
  // ----------------------------------------------------------------------
  api.registerFunction("shell.splitArgs",
    [api](const std::vector<Value> &args) {
      if (args.empty())
        return api.makeArray();  // empty array
      std::string cmd = api.resolveString(args[0]);
      auto parts = splitArgs(cmd);
      auto arr = api.makeArray();
      for (const auto &p : parts)
        api.push(arr, api.makeString(p));
      return arr;
    });

  // ----------------------------------------------------------------------
  // Filesystem helpers (cross‑platform via std::filesystem)
  // ----------------------------------------------------------------------

// shell.exists(path)
api.registerFunction("shell.exists",
  [api](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    std::string p = api.resolveString(args[0]);
    if (!isPathAllowed(p)) return Value::makeBool(false);
    return Value::makeBool(fs::exists(p));
  });

// shell.isFile(path)
api.registerFunction("shell.isFile",
  [api](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    std::string p = api.resolveString(args[0]);
    if (!isPathAllowed(p)) return Value::makeBool(false);
    return Value::makeBool(fs::exists(p) && fs::is_regular_file(p));
  });

// shell.isDir(path)
api.registerFunction("shell.isDir",
  [api](const std::vector<Value> &args) {
    if (args.empty()) return Value::makeBool(false);
    std::string p = api.resolveString(args[0]);
    if (!isPathAllowed(p)) return Value::makeBool(false);
    return Value::makeBool(fs::exists(p) && fs::is_directory(p));
  });

// shell.mkdir(path) – create single directory (non‑recursive)
api.registerFunction("shell.mkdir",
  [api](const std::vector<Value> &args) {
    if (args.empty())
      return Value::makeBool(false);
    std::string p = api.resolveString(args[0]);
    if (!isPathAllowed(p)) return Value::makeBool(false);
    std::error_code ec;
    bool ok = fs::create_directory(p, ec);
    return Value::makeBool(ok);
  });

// shell.mkdirs(path) – create directory and all missing parents
api.registerFunction("shell.mkdirs",
  [api](const std::vector<Value> &args) {
    if (args.empty())
      return Value::makeBool(false);
    std::string p = api.resolveString(args[0]);
    if (!isPathAllowed(p)) return Value::makeBool(false);
    std::error_code ec;
    bool ok = fs::create_directories(p, ec);
    return Value::makeBool(ok);
  });

// shell.remove(path) – delete a file or empty directory
api.registerFunction("shell.remove",
  [api](const std::vector<Value> &args) {
    if (args.empty())
      return Value::makeBool(false);
    std::string p = api.resolveString(args[0]);
    if (!isPathAllowed(p)) return Value::makeBool(false);
    std::error_code ec;
    bool ok = fs::remove(p, ec);
    return Value::makeBool(ok);
  });

// shell.removeAll(path) – delete a file or directory recursively
api.registerFunction("shell.removeAll",
  [api](const std::vector<Value> &args) {
    if (args.empty())
      return Value::makeBool(false);
    std::string p = api.resolveString(args[0]);
    if (!isPathAllowed(p)) return Value::makeBool(false);
    std::error_code ec;
    uintmax_t cnt = fs::remove_all(p, ec);
    return Value::makeInt(static_cast<int64_t>(cnt));  // number of deleted items
  });

// shell.copy(src, dst) – copy file; if dst is a directory, file is copied inside it
api.registerFunction("shell.copy",
  [api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("shell.copy() requires source and destination");
    std::string src = api.resolveString(args[0]);
    std::string dst = api.resolveString(args[1]);
    if (!isPathAllowed(src) || !isPathAllowed(dst)) return Value::makeBool(false);
    std::error_code ec;
    fs::copy(src, dst, fs::copy_options::overwrite_existing, ec);
    return Value::makeBool(!ec);
  });

// shell.move(src, dst) – move/rename a file or directory
api.registerFunction("shell.move",
  [api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("shell.move() requires source and destination");
    std::string src = api.resolveString(args[0]);
    std::string dst = api.resolveString(args[1]);
    if (!isPathAllowed(src) || !isPathAllowed(dst)) return Value::makeBool(false);
    std::error_code ec;
    fs::rename(src, dst, ec);
    return Value::makeBool(!ec);
  });

// shell.listDir(path) – returns array of filenames inside directory
api.registerFunction("shell.listDir",
  [api](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("shell.listDir() requires a directory path");
    std::string p = api.resolveString(args[0]);
    if (!isPathAllowed(p)) return api.makeArray();  // empty if not allowed
    auto arr = api.makeArray();
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec))
      return arr;  // empty if not a directory

    for (const auto &entry : fs::directory_iterator(p, ec)) {
      api.push(arr, api.makeString(entry.path().filename().string()));
    }
    return arr;
  });

  // shell.tmpfile() – create a temporary file and return its path
  api.registerFunction("shell.tmpfile",
    [api](const std::vector<Value>&) {
#ifdef _WIN32
      char tmpPath[MAX_PATH];
      if (GetTempPathA(MAX_PATH, tmpPath) == 0) return Value::makeNull();
      char tmpFile[MAX_PATH];
      if (GetTempFileNameA(tmpPath, "hvl", 0, tmpFile) == 0) return Value::makeNull();
      return api.makeString(tmpFile);
#else
      std::string tmpDir = "/tmp";
      const char *env = std::getenv("TMPDIR");
      if (env) tmpDir = env;
      std::string tmpl = tmpDir + "/havel_XXXXXX";
      // Need a writable string for mkstemp
      char *buf = new char[tmpl.size() + 1];
      std::strcpy(buf, tmpl.c_str());
      int fd = mkstemp(buf);
      if (fd == -1) {
        delete[] buf;
        return Value::makeNull();
      }
      ::close(fd);  // we only want the filename
      std::string result(buf);
      delete[] buf;
      return api.makeString(result);
#endif
    });

  // shell.envList() – returns an object containing all environment variables
  api.registerFunction("shell.envList",
    [api](const std::vector<Value>&) {
      return listEnvironment(api);
    });

  // shell.open(path) – open a file/URL with the default system handler
  api.registerFunction("shell.open",
    [api](const std::vector<Value> &args) {
      if (args.empty())
        throw std::runtime_error("shell.open() requires a path or URL");
      std::string path = api.resolveString(args[0]);
      std::string cmd;
#ifdef _WIN32
      // Windows: start "" "<path>"
      cmd = "start \"\" \"" + path + "\"";
#elif __APPLE__
      cmd = "open \"" + path + "\"";
#else
      // Linux/BSD: try xdg-open, fallback to open
      cmd = "xdg-open \"" + path + "\" 2>/dev/null || open \"" + path + "\"";
#endif
      Launcher::runDetached(cmd);
      return Value::makeNull();
    });

  // ----------------------------------------------------------------------
  // Build and expose the global "shell" object
  // ----------------------------------------------------------------------
  auto shellObj = api.makeObject();
  api.setField(shellObj, "run",        api.makeFunctionRef("shell.run"));
  api.setField(shellObj, "exec",       api.makeFunctionRef("shell.exec"));
  api.setField(shellObj, "which",      api.makeFunctionRef("shell.which"));
  api.setField(shellObj, "env",        api.makeFunctionRef("shell.env"));
  api.setField(shellObj, "getenv",     api.makeFunctionRef("shell.getenv"));
  api.setField(shellObj, "cwd",        api.makeFunctionRef("shell.cwd"));
  api.setField(shellObj, "cd",         api.makeFunctionRef("shell.cd"));
  api.setField(shellObj, "escape",     api.makeFunctionRef("shell.escape"));
  api.setField(shellObj, "platform",   api.makeFunctionRef("shell.platform"));
  api.setField(shellObj, "pid",        api.makeFunctionRef("shell.pid"));
  api.setField(shellObj, "home",       api.makeFunctionRef("shell.home"));
  api.setField(shellObj, "tmpdir",     api.makeFunctionRef("shell.tmpdir"));
  api.setField(shellObj, "hostname",   api.makeFunctionRef("shell.hostname"));
  api.setField(shellObj, "user",       api.makeFunctionRef("shell.user"));
  api.setField(shellObj, "shell",      api.makeFunctionRef("shell.shell"));
  api.setField(shellObj, "sleep",      api.makeFunctionRef("shell.sleep"));
  api.setField(shellObj, "read",       api.makeFunctionRef("shell.read"));
  api.setField(shellObj, "write",      api.makeFunctionRef("shell.write"));
api.setField(shellObj, "isatty",      api.makeFunctionRef("shell.isatty"));
  api.setField(shellObj, "ready",       api.makeFunctionRef("shell.ready"));
  api.setField(shellObj, "history_read", api.makeFunctionRef("shell.history_read"));
  api.setField(shellObj, "history_write", api.makeFunctionRef("shell.history_write"));
  api.setField(shellObj, "history_add",  api.makeFunctionRef("shell.history_add"));
  api.setField(shellObj, "exit",        api.makeFunctionRef("shell.exit"));
  api.setField(shellObj, "splitArgs",  api.makeFunctionRef("shell.splitArgs"));
  api.setField(shellObj, "exists",     api.makeFunctionRef("shell.exists"));
  api.setField(shellObj, "isFile",     api.makeFunctionRef("shell.isFile"));
  api.setField(shellObj, "isDir",      api.makeFunctionRef("shell.isDir"));
  api.setField(shellObj, "mkdir",      api.makeFunctionRef("shell.mkdir"));
  api.setField(shellObj, "mkdirs",     api.makeFunctionRef("shell.mkdirs"));
  api.setField(shellObj, "remove",     api.makeFunctionRef("shell.remove"));
  api.setField(shellObj, "removeAll",  api.makeFunctionRef("shell.removeAll"));
  api.setField(shellObj, "copy",       api.makeFunctionRef("shell.copy"));
  api.setField(shellObj, "move",       api.makeFunctionRef("shell.move"));
  api.setField(shellObj, "listDir",    api.makeFunctionRef("shell.listDir"));
  api.setField(shellObj, "tmpfile",    api.makeFunctionRef("shell.tmpfile"));
  api.setField(shellObj, "envList",    api.makeFunctionRef("shell.envList"));
  api.setField(shellObj, "open",       api.makeFunctionRef("shell.open"));

  // Set "shell" global BEFORE loading sidecar so the sidecar can access shell.*
  api.setGlobal("shell", shellObj);

  // Load pure-Havel shell sidecar (adds historyRead, historyWrite, historyAdd, historyPath)
  Value shellExports;
  try {
    shellExports = api.vm().loadModule("shell");
    mergeExports(api, shellObj, shellExports);
  } catch (const std::exception& e) {
    // Shell sidecar not available - continue without Havel wrappers
  } catch (...) {
  }

  // Re-export updated shell object
  api.setGlobal("shell", shellObj);
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(shell, "1.0.0", "Shell execution stdlib module",
    havel::stdlib::registerShellModule(*api);
)
#endif
