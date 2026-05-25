/* SysModule.cpp - VM-native stdlib module (system information) */
#include "SysModule.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#else
#include <windows.h>
#include <tlhelp32.h>
#endif

#include "havel-lang/core/Value.hpp"
#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/runtime/concurrency/Fiber.hpp"
#ifdef HAVEL_ENABLE_LLVM
#include "havel-lang/compiler/BytecodeOrcJIT.h"
#endif
#include "core/process/Launcher.hpp"
#include "host/process/ProcessService.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace fs = std::filesystem;

namespace havel::stdlib {

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\n\r");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\n\r");
  return s.substr(a, b - a + 1);
}

static std::string toLower(const std::string& s) {
  std::string r = s;
  std::transform(r.begin(), r.end(), r.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return r;
}

static std::string runCmd(const char* cmd) {
  FILE* pipe = popen(cmd, "r");
  if (!pipe) return "";
  char buf[512];
  std::string result;
  while (fgets(buf, sizeof(buf), pipe)) result += buf;
  pclose(pipe);
  return trim(result);
}

static std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::string line;
  if (f && std::getline(f, line)) return trim(line);
  return "";
}

static std::string readFileField(const std::string& path, const std::string& prefix) {
  std::ifstream f(path);
  std::string line;
  while (f && std::getline(f, line)) {
    if (line.compare(0, prefix.size(), prefix) == 0) {
      auto val = line.substr(prefix.size());
      size_t start = val.find_first_not_of(" \t:");
      if (start != std::string::npos) return trim(val.substr(start));
    }
  }
  return "";
}

#ifndef _WIN32
static std::mutex g_spawnMutex;
static std::unordered_map<int64_t, std::pair<int, int>> g_spawnPipes;
#endif

void registerSysModule(const VMApi &api) {
  api.registerFunction("sys.platform",
                       [api](const std::vector<Value> &args) {
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
                       [api](const std::vector<Value> &args) {
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
                       [api](const std::vector<Value> &args) {
                         (void)args;
                         return api.makeString("0.1.0");
                       });

  api.registerFunction("sys.argv",
                       [api](const std::vector<Value> &args) {
                         (void)args;
  auto arr = api.makeArray();
  (void)arr;
  return arr;
                       });

  api.registerFunction("sys.env",
                       [api](const std::vector<Value> &args) {
                         if (args.empty())
                           return Value::makeNull();
                         std::string name = api.resolveString(args[0]);
                         const char *val = std::getenv(name.c_str());
                         if (!val)
                           return Value::makeNull();
                         return api.makeString(val);
                       });

  api.registerFunction("sys.envAll",
                       [api](const std::vector<Value> &args) {
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
                       [api](const std::vector<Value> &args) {
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
                       [api](const std::vector<Value> &args) {
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
                       [api](const std::vector<Value> &args) {
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
                       [api](const std::vector<Value> &args) {
                         (void)args;
                         const char *home = std::getenv("HOME");
                         if (!home)
                           home = std::getenv("USERPROFILE");
  return api.makeString(home ? std::string(home) : "");
});

  api.registerFunction("sys.tmpdir",
                       [api](const std::vector<Value> &args) {
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
                       [api](const std::vector<Value> &args) {
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
                       [api](const std::vector<Value> &args) {
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

  // ========================================================================
  // system.detect — detect OS, display protocol, window manager, etc.
  // ========================================================================
  api.registerFunction("system.detect", [api](const std::vector<Value>&) {
    auto obj = api.makeObject();

    std::string os = "Unknown";
    std::string kernel;
    std::string arch;
    std::string hostname;
    std::string displayProtocol = "Unknown";
    std::string display;
    std::string windowManager = "unknown";
    std::string desktopEnv;
    double uptime = 0.0;

#ifndef _WIN32
    struct utsname uts;
    if (uname(&uts) == 0) {
      kernel = uts.release;
      arch = uts.machine;
      hostname = uts.nodename;
    }

#ifdef __linux__
    os = "Linux";

    // Uptime
    auto upStr = readFile("/proc/uptime");
    if (!upStr.empty()) {
      try { uptime = std::stod(upStr); } catch (...) {}
    }

    // Display protocol
    const char* xdgSession = std::getenv("XDG_SESSION_TYPE");
    if (xdgSession) {
      std::string sl = toLower(xdgSession);
      if (sl.find("wayland") != std::string::npos)
        displayProtocol = "Wayland";
      else if (sl.find("x11") != std::string::npos || sl.find("xorg") != std::string::npos)
        displayProtocol = "X11";
      else
        displayProtocol = xdgSession;
    } else {
      const char* d = std::getenv("DISPLAY");
      if (d && d[0]) displayProtocol = "X11";
    }

    const char* disp = std::getenv("DISPLAY");
    if (disp) display = disp;

    // Window manager detection
    const char* xdgDesktop = std::getenv("XDG_CURRENT_DESKTOP");
    if (xdgDesktop) desktopEnv = xdgDesktop;

    // Try to detect WM via process list
    windowManager = runCmd("bash -c '"
      "if pgrep -x hyprland >/dev/null 2>&1; then echo hyprland; "
      "elif pgrep -x mutter >/dev/null 2>&1; then echo gnome; "
      "elif pgrep -x kwin_wayland >/dev/null 2>&1; then echo \"kde plasma\"; "
      "elif pgrep -x kwin_x11 >/dev/null 2>&1; then echo \"kde plasma\"; "
      "elif pgrep -x i3 >/dev/null 2>&1; then echo i3; "
      "elif pgrep -x sway >/dev/null 2>&1; then echo sway; "
      "elif pgrep -x xfwm4 >/dev/null 2>&1; then echo xfce; "
      "elif pgrep -x compiz >/dev/null 2>&1; then echo compiz; "
      "elif pgrep -x cinnamon-sessio >/dev/null 2>&1; then echo cinnamon; "
      "elif pgrep -x wayfire >/dev/null 2>&1; then echo wayfire; "
      "elif pgrep -x openbox >/dev/null 2>&1; then echo openbox; "
      "elif pgrep -x bspwm >/dev/null 2>&1; then echo bspwm; "
      "elif pgrep -x dwm >/dev/null 2>&1; then echo dwm; "
      "elif pgrep -x awesome >/dev/null 2>&1; then echo awesome; "
      "else echo unknown; fi'");

    // Fallback: try XDG desktop env as WM hint
    if (windowManager == "unknown" && xdgDesktop) {
      std::string dl = toLower(xdgDesktop);
      if (dl.find("gnome") != std::string::npos) windowManager = "gnome";
      else if (dl.find("kde") != std::string::npos) windowManager = "kde plasma";
      else if (dl.find("xfce") != std::string::npos) windowManager = "xfce";
      else if (dl.find("cinnamon") != std::string::npos) windowManager = "cinnamon";
    }
#elif __APPLE__
    os = "MacOS";
    windowManager = "quartz";
    displayProtocol = "Quartz";
#elif __FreeBSD__ || __OpenBSD__ || __NetBSD__
    os = "BSD";
#else
    os = "Unknown";
#endif
#else
    os = "Windows";
    arch = "x86_64";
    char hbuf[256];
    DWORD hsz = sizeof(hbuf);
    if (GetComputerNameA(hbuf, &hsz)) hostname = hbuf;
    windowManager = "dwm";
    displayProtocol = "Win32";
#endif

    const char* shell = std::getenv("SHELL");
    const char* user = std::getenv("USER");
    if (!user) user = std::getenv("LOGNAME");
    const char* home = std::getenv("HOME");
#ifndef _WIN32
    if (!home) home = std::getenv("USERPROFILE");
#endif

    api.setField(obj, "os", api.makeString(os));
    api.setField(obj, "kernel", api.makeString(kernel));
    api.setField(obj, "arch", api.makeString(arch));
    api.setField(obj, "hostname", api.makeString(hostname));
    api.setField(obj, "shell", api.makeString(shell ? shell : ""));
    api.setField(obj, "user", api.makeString(user ? user : ""));
    api.setField(obj, "home", api.makeString(home ? home : ""));
    api.setField(obj, "displayProtocol", api.makeString(displayProtocol));
    api.setField(obj, "display", display.empty() ? Value::makeNull() : api.makeString(display));
    api.setField(obj, "windowManager", api.makeString(windowManager));
    api.setField(obj, "desktopEnv", desktopEnv.empty() ? Value::makeNull() : api.makeString(desktopEnv));
    api.setField(obj, "uptime", Value(uptime));

    return obj;
  });

  // ========================================================================
  // system.hardware — detect CPU, GPU, RAM, storage, etc.
  // ========================================================================
  api.registerFunction("system.hardware", [api](const std::vector<Value>&) {
    auto obj = api.makeObject();

    std::string cpu;
    int cpuCores = 0;
    int cpuThreads = 0;
    double cpuFreq = 0.0;
    uint64_t ramTotal = 0;
    uint64_t ramUsed = 0;
    uint64_t ramFree = 0;
    uint64_t swapTotal = 0;
    uint64_t swapUsed = 0;
    uint64_t swapFree = 0;
    std::string gpu;
    std::string motherboard;
    std::string bios;
    double cpuTemp = 0.0;
    double gpuTemp = 0.0;

#ifdef __linux__
    // CPU model from /proc/cpuinfo
    cpu = readFileField("/proc/cpuinfo", "model name");
    if (cpu.empty()) cpu = readFileField("/proc/cpuinfo", "Hardware");

    // CPU cores (physical)
    cpuCores = 0;
    {
      std::ifstream f("/proc/cpuinfo");
      std::string line;
      while (f && std::getline(f, line)) {
        if (line.find("core id") == 0) cpuCores++;
      }
      if (cpuCores == 0) cpuCores = 1;
    }

    // CPU threads
    cpuThreads = 0;
    {
      std::ifstream f("/proc/cpuinfo");
      std::string line;
      while (f && std::getline(f, line)) {
        if (line.find("processor") == 0) cpuThreads++;
      }
      if (cpuThreads == 0) cpuThreads = cpuCores;
    }

    // CPU frequency from /sys
    {
      auto freqStr = readFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
      if (freqStr.empty()) freqStr = readFile("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
      if (!freqStr.empty()) {
        try { cpuFreq = std::stod(freqStr) / 1000.0; } catch (...) {}
      }
    }

    // Memory from /proc/meminfo
    {
      std::ifstream f("/proc/meminfo");
      std::string line;
      while (f && std::getline(f, line)) {
        if (line.find("MemTotal:") == 0) {
          try { ramTotal = std::stoull(line.substr(10)) * 1024; } catch (...) {}
        } else if (line.find("MemAvailable:") == 0) {
          try { ramFree = std::stoull(line.substr(14)) * 1024; } catch (...) {}
        } else if (line.find("SwapTotal:") == 0) {
          try { swapTotal = std::stoull(line.substr(11)) * 1024; } catch (...) {}
        } else if (line.find("SwapFree:") == 0) {
          try { swapFree = std::stoull(line.substr(10)) * 1024; } catch (...) {}
        }
      }
      ramUsed = (ramTotal > ramFree) ? (ramTotal - ramFree) : 0;
      swapUsed = (swapTotal > swapFree) ? (swapTotal - swapFree) : 0;
    }

    // GPU
    gpu = runCmd("lspci 2>/dev/null | grep -i 'vga\\|3d\\|display' | head -1 | cut -d':' -f3-");
    if (gpu.empty()) gpu = "Unknown";
    else gpu = trim(gpu);

    // Motherboard
    {
      std::string vendor = readFile("/sys/class/dmi/id/board_vendor");
      std::string name = readFile("/sys/class/dmi/id/board_name");
      if (!vendor.empty() && !name.empty()) motherboard = vendor + " " + name;
    }
    if (motherboard.empty()) motherboard = "Unknown";

    // BIOS
    bios = readFile("/sys/class/dmi/id/bios_version");
    if (bios.empty()) bios = "Unknown";

    // CPU temp
    {
      auto tempStr = readFile("/sys/class/thermal/thermal_zone0/temp");
      if (!tempStr.empty()) {
        try { cpuTemp = std::stod(tempStr) / 1000.0; } catch (...) {}
      }
    }
#endif

    api.setField(obj, "cpu", api.makeString(cpu));
    api.setField(obj, "cpuCores", Value::makeInt(static_cast<int64_t>(cpuCores)));
    api.setField(obj, "cpuThreads", Value::makeInt(static_cast<int64_t>(cpuThreads)));
    api.setField(obj, "cpuFrequency", Value::makeDouble(cpuFreq));
    api.setField(obj, "cpuUsage", Value::makeDouble(0.0));
    api.setField(obj, "gpu", api.makeString(gpu));
    api.setField(obj, "gpuTemperature", Value::makeDouble(gpuTemp));
    api.setField(obj, "ramTotal", Value::makeInt(static_cast<int64_t>(ramTotal)));
    api.setField(obj, "ramUsed", Value::makeInt(static_cast<int64_t>(ramUsed)));
    api.setField(obj, "ramFree", Value::makeInt(static_cast<int64_t>(ramFree)));
    api.setField(obj, "swapTotal", Value::makeInt(static_cast<int64_t>(swapTotal)));
    api.setField(obj, "swapUsed", Value::makeInt(static_cast<int64_t>(swapUsed)));
    api.setField(obj, "swapFree", Value::makeInt(static_cast<int64_t>(swapFree)));
    api.setField(obj, "motherboard", api.makeString(motherboard));
    api.setField(obj, "bios", api.makeString(bios));
    api.setField(obj, "cpuTemperature", Value::makeDouble(cpuTemp));

    // Storage array
    auto storageArr = api.makeArray();
#ifdef __linux__
    {
      FILE* pipe = popen("lsblk -nd -o NAME,MODEL,SIZE,TYPE,MOUNTPOINT,FSTYPE,AVAIL -b 2>/dev/null", "r");
      if (pipe) {
        char buf[1024];
        while (fgets(buf, sizeof(buf), pipe)) {
          std::string line = trim(buf);
          if (line.empty()) continue;
          std::istringstream iss(line);
          std::string name, model, size, type, mount, fstype, avail;
          iss >> name >> model >> size >> type >> mount >> fstype >> avail;
          if (type != "disk") continue;

          auto sobj = api.makeObject();
          api.setField(sobj, "name", api.makeString(name));
          api.setField(sobj, "model", api.makeString(model.empty() ? "Unknown" : model));
          uint64_t sz = 0;
          try { sz = std::stoull(size); } catch (...) {}
          api.setField(sobj, "size", Value::makeInt(static_cast<int64_t>(sz)));

          uint64_t used = 0, freeB = 0;
          if (!mount.empty() && mount != "none" && mount != "[SWAP]") {
            try { freeB = std::stoull(avail); } catch (...) {}
            used = (sz > freeB) ? (sz - freeB) : 0;
          }
          api.setField(sobj, "used", Value::makeInt(static_cast<int64_t>(used)));
          api.setField(sobj, "free", Value::makeInt(static_cast<int64_t>(freeB)));

          std::string dtype = "Unknown";
          if (name.find("nvme") != std::string::npos) dtype = "NVMe";
          else if (name.find("sd") != std::string::npos) dtype = "SSD";
          api.setField(sobj, "type", api.makeString(dtype));
          api.setField(sobj, "mountPoint", api.makeString(mount == "none" ? "" : mount));
          api.setField(sobj, "filesystem", api.makeString(fstype));

          api.push(storageArr, sobj);
        }
        pclose(pipe);
      }
    }
#endif
    api.setField(obj, "storage", storageArr);

    return obj;
  });

  // ========================================================================
  // process.find — find PIDs by name
  // ========================================================================
  api.registerFunction("process.find", [api](const std::vector<Value>& args) {
    if (args.empty())
      throw std::runtime_error("process.find() requires a process name");
    std::string name = api.resolveString(args[0]);
#ifndef _WIN32
    auto arr = api.makeArray();
    FILE* pipe = popen(("pgrep -x " + name + " 2>/dev/null || pgrep " + name + " 2>/dev/null").c_str(), "r");
    if (pipe) {
      char buf[128];
      while (fgets(buf, sizeof(buf), pipe)) {
        std::string line = trim(buf);
        if (!line.empty()) {
          try {
            int64_t pid = std::stoll(line);
            api.push(arr, Value::makeInt(pid));
          } catch (...) {}
        }
      }
      pclose(pipe);
    }
    return arr;
#else
    return api.makeArray();
#endif
  });

  // ========================================================================
  // process.exists — check if process is running (by name or PID)
  // ========================================================================
  api.registerFunction("process.exists", [api](const std::vector<Value>& args) {
    if (args.empty())
      throw std::runtime_error("process.exists() requires a process name or PID");
#ifndef _WIN32
    if (args[0].isInt()) {
      pid_t pid = static_cast<pid_t>(args[0].asInt());
      return Value::makeBool(kill(pid, 0) == 0);
    }
    std::string name = api.resolveString(args[0]);
    std::string result = runCmd(("pgrep -x " + name + " 2>/dev/null || pgrep " + name + " 2>/dev/null").c_str());
    return Value::makeBool(!result.empty());
#else
    return Value::makeBool(false);
#endif
  });

  // ========================================================================
  // process.kill — send signal to PID
  // ========================================================================
  api.registerFunction("process.kill", [api](const std::vector<Value>& args) {
    if (args.size() < 2)
      throw std::runtime_error("process.kill() requires PID and signal");
    if (!args[0].isInt())
      throw std::runtime_error("process.kill() requires a number PID");
    int32_t pid = static_cast<int32_t>(args[0].asInt());
    std::string sig = api.resolveString(args[1]);
    int signum = 15;
    if (sig == "SIGKILL" || sig == "kill") signum = 9;
    else if (sig == "SIGTERM" || sig == "term") signum = 15;
    else if (sig == "SIGHUP" || sig == "hangup") signum = 1;
    else if (sig == "SIGINT" || sig == "int") signum = 2;
#ifndef _WIN32
    return Value::makeBool(kill(pid, signum) == 0);
#else
    return Value::makeBool(false);
#endif
  });

  // ========================================================================
  // process.nice — set nice value for PID
  // ========================================================================
  api.registerFunction("process.nice", [](const std::vector<Value>& args) -> Value {
    if (args.size() < 2)
      throw std::runtime_error("process.nice() requires PID and nice value");
    if (!args[0].isInt())
      throw std::runtime_error("process.nice() requires a number PID");
    if (!args[1].isInt())
      throw std::runtime_error("process.nice() requires a number nice value");
    int32_t pid = static_cast<int32_t>(args[0].asInt());
    int64_t niceVal = args[1].asInt();
    if (niceVal < -20 || niceVal > 19)
      throw std::runtime_error("process.nice() nice value must be between -20 and 19");
#ifndef _WIN32
    errno = 0;
    int ret = setpriority(PRIO_PROCESS, pid, static_cast<int>(niceVal));
    return Value::makeBool(ret == 0 && errno == 0);
#else
    return Value::makeBool(false);
#endif
  });

  // ========================================================================
  // process.run — run command synchronously, return {pid, exitCode, success, stdout, stderr, error}
  // ========================================================================
  api.registerFunction("process.run", [api](const std::vector<Value>& args) {
    if (api.vm().getScheduler())
      api.vm().getScheduler()->yieldCurrentAndCheckTimers();
    if (args.empty())
      throw std::runtime_error("process.run() requires a command");
    std::string cmd = api.resolveString(args[0]);

    auto presult = Launcher::runSync(cmd);

    auto obj = api.makeObject();
    api.setField(obj, "pid", Value::makeInt(presult.pid));
    api.setField(obj, "exitCode", Value::makeInt(static_cast<int64_t>(presult.exitCode)));
    api.setField(obj, "success", Value::makeBool(presult.success));
    api.setField(obj, "error", presult.success ? Value::makeNull() : api.makeString(presult.error));
    api.setField(obj, "stdout", api.makeString(presult.stdout));
    api.setField(obj, "stderr", api.makeString(presult.stderr));
    return obj;
  });

  // ========================================================================
  // process.runDetached — run command without waiting, return pid
  // ========================================================================
  api.registerFunction("process.runDetached", [api](const std::vector<Value>& args) {
    if (args.empty())
      throw std::runtime_error("process.runDetached() requires a command");
    std::string cmd = api.resolveString(args[0]);

    auto presult = Launcher::runDetached(cmd);
    return Value::makeInt(presult.pid);
  });

  // ========================================================================
  // process.wait — wait for spawned process to finish, update fields on object
  // ========================================================================
  api.registerFunction("process.wait", [api](const std::vector<Value>& args) {
    if (api.vm().getScheduler())
      api.vm().getScheduler()->yieldCurrentAndCheckTimers();
    if (args.empty())
      throw std::runtime_error("process.wait() requires process object");

    Value procObj = args[0];
    Value pidVal = api.getField(procObj, "pid");
    if (!pidVal.isInt())
      throw std::runtime_error("process.wait(): invalid process object");
#ifndef _WIN32
    pid_t pid = static_cast<pid_t>(pidVal.asInt());
    if (pid <= 0)
      throw std::runtime_error("process.wait(): process not started");

    int status;
    pid_t ret = waitpid(pid, &status, 0);

    int exitCode = (ret > 0 && WIFEXITED(status)) ? WEXITSTATUS(status)
                   : (ret > 0 && WIFSIGNALED(status)) ? -WTERMSIG(status) : -1;

    std::string outStr, errStr;
    {
      auto pit = g_spawnPipes.find(pid);
      if (pit != g_spawnPipes.end()) {
        char buf[4096];
        ssize_t n;
        while ((n = read(pit->second.first, buf, sizeof(buf))) > 0)
          outStr.append(buf, n);
        while ((n = read(pit->second.second, buf, sizeof(buf))) > 0)
          errStr.append(buf, n);
        close(pit->second.first);
        close(pit->second.second);
        g_spawnPipes.erase(pit);
      }
    }

    api.setField(procObj, "exitCode", Value::makeInt(exitCode));
    api.setField(procObj, "success", Value::makeBool(exitCode == 0 && ret > 0));
    api.setField(procObj, "stdout", api.makeString(outStr));
    api.setField(procObj, "stderr", api.makeString(errStr));

    return Value::makeInt(exitCode);
#else
    return Value::makeNull();
#endif
  });

  // ========================================================================
  // process.killObj — kill spawned process (use process.kill(pid, sig) for PID)
  // ========================================================================
  api.registerFunction("process.killObj", [api](const std::vector<Value>& args) {
    if (args.empty())
      throw std::runtime_error("process.killObj() requires process object");

    Value procObj = args[0];
    Value pidVal = api.getField(procObj, "pid");
    if (!pidVal.isInt())
      throw std::runtime_error("process.killObj(): invalid process object");

#ifndef _WIN32
    pid_t pid = static_cast<pid_t>(pidVal.asInt());
    if (pid <= 0)
      return Value::makeBool(false);

    // Send SIGKILL directly to ensure process dies
    return Value::makeBool(::kill(pid, SIGKILL) == 0);
#else
    return Value::makeBool(false);
#endif
  });

  // ========================================================================
  // process.spawn — run command, return process object with .pid (data fields only)
  // ========================================================================
  api.registerFunction("process.spawn", [api](const std::vector<Value>& args) {
    if (api.vm().getScheduler())
      api.vm().getScheduler()->yieldCurrentAndCheckTimers();
    if (args.empty())
      throw std::runtime_error("process.spawn() requires a command");
    std::string cmd = api.resolveString(args[0]);
#ifndef _WIN32
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0)
      throw std::runtime_error("process.spawn() pipe failed");

    pid_t pid = fork();
    if (pid == -1) {
      close(stdout_pipe[0]); close(stdout_pipe[1]);
      close(stderr_pipe[0]); close(stderr_pipe[1]);
      throw std::runtime_error("process.spawn() fork failed");
    }

    if (pid == 0) {
      close(stdout_pipe[0]);
      close(stderr_pipe[0]);
      dup2(stdout_pipe[1], STDOUT_FILENO);
      dup2(stderr_pipe[1], STDERR_FILENO);
      close(stdout_pipe[1]);
      close(stderr_pipe[1]);
      execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
      _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    {
      std::lock_guard<std::mutex> lock(g_spawnMutex);
      g_spawnPipes[pid] = {stdout_pipe[0], stderr_pipe[0]};
    }

    auto obj = api.makeObject();
    api.setField(obj, "pid", Value::makeInt(pid));
#else
    auto obj = api.makeObject();
    api.setField(obj, "pid", Value::makeInt(-1));
#endif
    return obj;
  });

  // ========================================================================
  // system namespace object
  // ========================================================================
  auto systemObj = api.makeObject();
  api.setField(systemObj, "detect", api.makeFunctionRef("system.detect"));
  api.setField(systemObj, "hardware", api.makeFunctionRef("system.hardware"));
  api.setGlobal("system", systemObj);

  // ========================================================================
  // process namespace object
  // ========================================================================
  auto processObj = api.makeObject();
  api.setField(processObj, "find", api.makeFunctionRef("process.find"));
  api.setField(processObj, "exists", api.makeFunctionRef("process.exists"));
  api.setField(processObj, "kill", api.makeFunctionRef("process.kill"));
  api.setField(processObj, "nice", api.makeFunctionRef("process.nice"));
  api.setField(processObj, "run", api.makeFunctionRef("process.run"));
  api.setField(processObj, "runDetached", api.makeFunctionRef("process.runDetached"));
  api.setField(processObj, "spawn", api.makeFunctionRef("process.spawn"));
  api.setField(processObj, "wait", api.makeFunctionRef("process.wait"));
    api.setField(processObj, "killObj", api.makeFunctionRef("process.killObj"));
    api.setField(processObj, "exit", api.makeFunctionRef("sys.exit"));
    api.setGlobal("process", processObj);
}

} // namespace havel::stdlib
