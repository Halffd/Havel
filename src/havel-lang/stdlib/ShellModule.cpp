/* ShellModule.cpp - VM-native stdlib module (shell/process operations)
   Multi-platform: Linux, macOS, BSD, Windows */
#include "ShellModule.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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
#include "havel-lang/compiler/vm/VM.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace fs = std::filesystem;

namespace havel::stdlib {

// Utility: convert a Value to a C++ string using the VM's string table
static std::string valueToString(const Value &v, VMApi &api) {
  return api.vm.resolveStringKey(v);
}

// Utility: push a string into the heap and return a string ID Value
static Value makeStringId(const std::string &s, VMApi &api) {
  auto strRef = api.vm.getHeap().allocateString(s);
  return Value::makeStringId(strRef.id);
}

// Cross‑platform pipe helpers
static FILE* openPipe(const std::string& cmd, const char* mode) {
#ifdef _WIN32
  return _popen(cmd.c_str(), mode);
#else
  return popen(cmd.c_str(), mode);
#endif
}

static int closePipe(FILE* pipe) {
#ifdef _WIN32
  return _pclose(pipe);
#else
  return pclose(pipe);
#endif
}

// Return a string describing the current platform
static std::string getPlatform() {
#ifdef _WIN32
  return "windows";
#elif __APPLE__
  #include <TargetConditionals.h>
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
static Value listEnvironment(VMApi &api) {
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
        api.setField(envObj, key, makeStringId(val, api));
      }
      cur += entry.size() + 1;  // next null‑terminated entry
    }
    FreeEnvironmentStringsA(envBlock);
  }
#else
  extern char **environ;
  if (environ) {
    for (char **cur = environ; *cur; ++cur) {
      std::string entry(*cur);
      auto eqPos = entry.find('=');
      if (eqPos != std::string::npos) {
        std::string key = entry.substr(0, eqPos);
        std::string val = entry.substr(eqPos + 1);
        api.setField(envObj, key, makeStringId(val, api));
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
void registerShellModule(VMApi &api) {
  // ----------------------------------------------------------------------
  // shell.run – execute command via system shell (returns exit code)
  // ----------------------------------------------------------------------
  api.registerFunction("shell.run",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        throw std::runtime_error("shell.run() requires a command string");
      std::string cmd = valueToString(args[0], api);
      int ret = std::system(cmd.c_str());
      return Value(static_cast<int64_t>(ret));
    });

  // ----------------------------------------------------------------------
  // shell.exec – capture stdout of command (returns object {stdout, stderr, exitCode})
  // ----------------------------------------------------------------------
  api.registerFunction("shell.exec",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        throw std::runtime_error("shell.exec() requires a command string");
      std::string cmd = valueToString(args[0], api);

      std::string stdout_str;
      FILE *pipe = openPipe(cmd, "r");
      if (!pipe) {
        auto resultRef = api.vm.createHostObject();
        api.vm.setHostObjectField(resultRef, "stdout", makeStringId("", api));
        api.vm.setHostObjectField(resultRef, "stderr", makeStringId("", api));
        api.vm.setHostObjectField(resultRef, "exitCode",
                                  Value::makeInt(static_cast<int64_t>(-1)));
        return Value::makeObjectId(resultRef.id);
      }

      char buf[4096];
      while (fgets(buf, sizeof(buf), pipe))
        stdout_str += buf;
      int exitCode = closePipe(pipe);

      auto resultRef = api.vm.createHostObject();
      api.vm.setHostObjectField(resultRef, "stdout", makeStringId(stdout_str, api));
      api.vm.setHostObjectField(resultRef, "stderr", makeStringId("", api));
      api.vm.setHostObjectField(resultRef, "exitCode",
                                Value::makeInt(static_cast<int64_t>(exitCode)));
      return Value::makeObjectId(resultRef.id);
    });

  // ----------------------------------------------------------------------
  // shell.which – locate executable in PATH
  // ----------------------------------------------------------------------
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
          return makeStringId(candidate.string(), api);
        for (const auto &ext : exts) {
          fs::path withExt = candidate; withExt += ext;
          if (fs::exists(withExt) && fs::is_regular_file(withExt))
            return makeStringId(withExt.string(), api);
        }
#else
        if (fs::exists(candidate) && fs::is_regular_file(candidate))
          return makeStringId(candidate.string(), api);
#endif
      }
      return Value::makeNull();
    });

  // ----------------------------------------------------------------------
  // shell.env – get / set a single environment variable
  // ----------------------------------------------------------------------
  api.registerFunction("shell.env",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeNull();
      std::string name = valueToString(args[0], api);

      if (args.size() >= 2) {  // set
        std::string val = valueToString(args[1], api);
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
      return makeStringId(val, api);
    });

  // ----------------------------------------------------------------------
  // shell.cwd – current working directory
  // ----------------------------------------------------------------------
  api.registerFunction("shell.cwd",
    [&api](const std::vector<Value>&) {
      return makeStringId(fs::current_path().string(), api);
    });

  // ----------------------------------------------------------------------
  // shell.getenv – get environment variable (readonly, returns null if missing)
  // ----------------------------------------------------------------------
  api.registerFunction("shell.getenv",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeNull();
      std::string name = valueToString(args[0], api);
      const char *val = std::getenv(name.c_str());
      if (!val)
        return Value::makeNull();
      return makeStringId(val, api);
    });

  // ----------------------------------------------------------------------
  // shell.cd – change directory
  // ----------------------------------------------------------------------
  api.registerFunction("shell.cd",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeBool(false);
      std::string path = valueToString(args[0], api);
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
    [&api](const std::vector<Value> &args) {
      if (args.empty())
#ifdef _WIN32
        return makeStringId("\"\"", api);
#else
        return makeStringId("''", api);
#endif
      std::string input = valueToString(args[0], api);
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
      return makeStringId(escaped, api);
    });

  // ----------------------------------------------------------------------
  // shell.platform – returns OS identifier (e.g. "linux", "windows", "macos")
  // ----------------------------------------------------------------------
  api.registerFunction("shell.platform",
    [&api](const std::vector<Value>&) {
      return makeStringId(getPlatform(), api);
    });

  // ----------------------------------------------------------------------
  // shell.pid – current process ID
  // ----------------------------------------------------------------------
  api.registerFunction("shell.pid",
    [&api](const std::vector<Value>&) {
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
    [&api](const std::vector<Value>&) {
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
      return home.empty() ? Value::makeNull() : makeStringId(home, api);
    });

  // ----------------------------------------------------------------------
  // shell.tmpdir – system temporary directory
  // ----------------------------------------------------------------------
  api.registerFunction("shell.tmpdir",
    [&api](const std::vector<Value>&) {
#ifdef _WIN32
      char buf[MAX_PATH];
      DWORD len = GetTempPathA(MAX_PATH, buf);
      if (len > 0 && len < MAX_PATH) {
        std::string tmp(buf, len);
        // strip trailing backslash if present
        if (!tmp.empty() && tmp.back() == '\\') tmp.pop_back();
        return makeStringId(tmp, api);
      }
      return makeStringId(fs::temp_directory_path().string(), api);
#else
      const char *tmp = std::getenv("TMPDIR");
      if (!tmp) tmp = std::getenv("TEMP");
      if (!tmp) tmp = std::getenv("TMP");
      if (!tmp) tmp = "/tmp";
      return makeStringId(tmp, api);
#endif
    });

  // ----------------------------------------------------------------------
  // shell.hostname – system host name
  // ----------------------------------------------------------------------
  api.registerFunction("shell.hostname",
    [&api](const std::vector<Value>&) {
      char buf[256];
#ifdef _WIN32
      DWORD size = sizeof(buf);
      if (GetComputerNameA(buf, &size))
        return makeStringId(std::string(buf), api);
#else
      if (gethostname(buf, sizeof(buf)) == 0)
        return makeStringId(std::string(buf), api);
#endif
      return Value::makeNull();
    });

  // ----------------------------------------------------------------------
  // shell.user – current user name
  // ----------------------------------------------------------------------
  api.registerFunction("shell.user",
    [&api](const std::vector<Value>&) {
#ifdef _WIN32
      char buf[256];
      DWORD size = sizeof(buf);
      if (GetUserNameA(buf, &size))
        return makeStringId(std::string(buf), api);
      return Value::makeNull();
#else
      const char *user = std::getenv("USER");
      if (!user) user = std::getenv("LOGNAME");
      if (!user) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) user = pw->pw_name;
      }
      return user ? makeStringId(std::string(user), api) : Value::makeNull();
#endif
    });

  // ----------------------------------------------------------------------
  // shell.shell – path to the default system shell
  // ----------------------------------------------------------------------
  api.registerFunction("shell.shell",
    [&api](const std::vector<Value>&) {
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
      return makeStringId(shell, api);
    });

  // ----------------------------------------------------------------------
  // shell.sleep – suspend execution for given seconds (fractional)
  // ----------------------------------------------------------------------
  api.registerFunction("shell.sleep",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        throw std::runtime_error("shell.sleep() requires a number (seconds)");
      double secs = args[0].getDouble();  // assume numeric conversion exists
      std::this_thread::sleep_for(std::chrono::duration<double>(secs));
      return Value::makeNull();
    });

  // ----------------------------------------------------------------------
  // shell.read – read line from stdin
  // ----------------------------------------------------------------------
  api.registerFunction("shell.read",
    [&api](const std::vector<Value> &) {
      std::string line;
      if (!std::getline(std::cin, line))
        return Value::makeNull();
      return makeStringId(line, api);
    });

  // ----------------------------------------------------------------------
  // shell.write – write text to stdout (default) or stderr
  //   shell.write(text) or shell.write(text, fd) where fd: 1=stdout, 2=stderr
  // ----------------------------------------------------------------------
  api.registerFunction("shell.write",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        throw std::runtime_error("shell.write() requires a string");
      std::string text = valueToString(args[0], api);
      FILE *dest = stdout;
      if (args.size() >= 2) {
        int64_t fd = args[1].getInt();
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
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeBool(false);
      int fd = static_cast<int>(args[0].getInt());
      if (fd < 0 || fd > 2) return Value::makeBool(false);
#ifdef _WIN32
      return Value::makeBool(_isatty(fd) != 0);
#else
      return Value::makeBool(isatty(fd) != 0);
#endif
    });

  // ----------------------------------------------------------------------
  // shell.exit – terminate the program with a status code
  // ----------------------------------------------------------------------
  api.registerFunction("shell.exit",
    [&api](const std::vector<Value> &args) {
      int code = 0;
      if (!args.empty()) code = static_cast<int>(args[0].getInt());
      std::exit(code);
      // unreachable
      return Value::makeNull();
    });

  // ----------------------------------------------------------------------
  // shell.splitArgs – split a command string into a list of arguments
  // ----------------------------------------------------------------------
  api.registerFunction("shell.splitArgs",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        return api.makeArray();  // empty array
      std::string cmd = valueToString(args[0], api);
      auto parts = splitArgs(cmd);
      auto arr = api.makeArray();
      for (const auto &p : parts)
        api.arrayPush(arr, makeStringId(p, api));
      return arr;
    });

  // ----------------------------------------------------------------------
  // Filesystem helpers (cross‑platform via std::filesystem)
  // ----------------------------------------------------------------------

  // shell.exists(path)
  api.registerFunction("shell.exists",
    [&api](const std::vector<Value> &args) {
      if (args.empty()) return Value::makeBool(false);
      std::string p = valueToString(args[0], api);
      return Value::makeBool(fs::exists(p));
    });

  // shell.isFile(path)
  api.registerFunction("shell.isFile",
    [&api](const std::vector<Value> &args) {
      if (args.empty()) return Value::makeBool(false);
      std::string p = valueToString(args[0], api);
      return Value::makeBool(fs::exists(p) && fs::is_regular_file(p));
    });

  // shell.isDir(path)
  api.registerFunction("shell.isDir",
    [&api](const std::vector<Value> &args) {
      if (args.empty()) return Value::makeBool(false);
      std::string p = valueToString(args[0], api);
      return Value::makeBool(fs::exists(p) && fs::is_directory(p));
    });

  // shell.mkdir(path) – create single directory (non‑recursive)
  api.registerFunction("shell.mkdir",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeBool(false);
      std::string p = valueToString(args[0], api);
      std::error_code ec;
      bool ok = fs::create_directory(p, ec);
      return Value::makeBool(ok);
    });

  // shell.mkdirs(path) – create directory and all missing parents
  api.registerFunction("shell.mkdirs",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeBool(false);
      std::string p = valueToString(args[0], api);
      std::error_code ec;
      bool ok = fs::create_directories(p, ec);
      return Value::makeBool(ok);
    });

  // shell.remove(path) – delete a file or empty directory
  api.registerFunction("shell.remove",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeBool(false);
      std::string p = valueToString(args[0], api);
      std::error_code ec;
      bool ok = fs::remove(p, ec);
      return Value::makeBool(ok);
    });

  // shell.removeAll(path) – delete a file or directory recursively
  api.registerFunction("shell.removeAll",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        return Value::makeBool(false);
      std::string p = valueToString(args[0], api);
      std::error_code ec;
      uintmax_t cnt = fs::remove_all(p, ec);
      return Value::makeInt(static_cast<int64_t>(cnt));  // number of deleted items
    });

  // shell.copy(src, dst) – copy file; if dst is a directory, file is copied inside it
  api.registerFunction("shell.copy",
    [&api](const std::vector<Value> &args) {
      if (args.size() < 2)
        throw std::runtime_error("shell.copy() requires source and destination");
      std::string src = valueToString(args[0], api);
      std::string dst = valueToString(args[1], api);
      std::error_code ec;
      fs::copy(src, dst, fs::copy_options::overwrite_existing, ec);
      return Value::makeBool(!ec);
    });

  // shell.move(src, dst) – move/rename a file or directory
  api.registerFunction("shell.move",
    [&api](const std::vector<Value> &args) {
      if (args.size() < 2)
        throw std::runtime_error("shell.move() requires source and destination");
      std::string src = valueToString(args[0], api);
      std::string dst = valueToString(args[1], api);
      std::error_code ec;
      fs::rename(src, dst, ec);
      return Value::makeBool(!ec);
    });

  // shell.listDir(path) – returns array of filenames inside directory
  api.registerFunction("shell.listDir",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        throw std::runtime_error("shell.listDir() requires a directory path");
      std::string p = valueToString(args[0], api);
      auto arr = api.makeArray();
      std::error_code ec;
      if (!fs::exists(p, ec) || !fs::is_directory(p, ec))
        return arr;  // empty if not a directory

      for (const auto &entry : fs::directory_iterator(p, ec)) {
        api.arrayPush(arr, makeStringId(entry.path().filename().string(), api));
      }
      return arr;
    });

  // shell.tmpfile() – create a temporary file and return its path
  api.registerFunction("shell.tmpfile",
    [&api](const std::vector<Value>&) {
#ifdef _WIN32
      char tmpPath[MAX_PATH];
      if (GetTempPathA(MAX_PATH, tmpPath) == 0) return Value::makeNull();
      char tmpFile[MAX_PATH];
      if (GetTempFileNameA(tmpPath, "hvl", 0, tmpFile) == 0) return Value::makeNull();
      return makeStringId(tmpFile, api);
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
      close(fd);  // we only want the filename
      std::string result(buf);
      delete[] buf;
      return makeStringId(result, api);
#endif
    });

  // shell.envList() – returns an object containing all environment variables
  api.registerFunction("shell.envList",
    [&api](const std::vector<Value>&) {
      return listEnvironment(api);
    });

  // shell.open(path) – open a file/URL with the default system handler
  api.registerFunction("shell.open",
    [&api](const std::vector<Value> &args) {
      if (args.empty())
        throw std::runtime_error("shell.open() requires a path or URL");
      std::string path = valueToString(args[0], api);
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
      std::system(cmd.c_str());
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
  api.setField(shellObj, "isatty",     api.makeFunctionRef("shell.isatty"));
  api.setField(shellObj, "exit",       api.makeFunctionRef("shell.exit"));
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
  api.setGlobal("shell", shellObj);
}

} // namespace havel::stdlib