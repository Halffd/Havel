#include "AudioBackendImpl.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <memory>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include "utils/SafeExec.hpp"

namespace havel {

// ============================================================
// Constructor / Destructor / Init
// ============================================================
AudioBackendImpl::AudioBackendImpl(AudioBackendMode mode) : backendMode(mode) {}

AudioBackendImpl::~AudioBackendImpl() { cleanup(); }

bool AudioBackendImpl::initialize() {
  useCli_ = (backendMode == AudioBackendMode::CLI);

  if (backendMode == AudioBackendMode::Automatic || backendMode == AudioBackendMode::Native) {
#ifdef HAVE_PIPEWIRE
    if (initNativePipeWire()) { activeBackend = AudioBackend::PIPEWIRE; return true; }
#endif
#ifdef HAVE_PULSEAUDIO
    if (initNativePulseAudio()) { activeBackend = AudioBackend::PULSE; return true; }
#endif
#ifdef HAVE_ALSA
    if (initNativeAlsa()) { activeBackend = AudioBackend::ALSA; return true; }
#endif
#ifdef _WIN32
    if (initNativeWindows()) { activeBackend = AudioBackend::WINDOWS; return true; }
#endif
  }

  // CLI fallback
  useCli_ = true;
#ifdef __linux__
  if (system("which wpctl 2>/dev/null >/dev/null") == 0) { activeBackend = AudioBackend::PIPEWIRE; return true; }
  if (system("which pactl 2>/dev/null >/dev/null") == 0) { activeBackend = AudioBackend::PULSE; return true; }
  if (system("which amixer 2>/dev/null >/dev/null") == 0) { activeBackend = AudioBackend::ALSA; return true; }
#endif
#ifdef _WIN32
  activeBackend = AudioBackend::WINDOWS;
  return true;
#endif
  return false;
}

void AudioBackendImpl::cleanup() {
#ifdef HAVE_PIPEWIRE
  pwStop();
  if (pw_core_) { pw_core_disconnect(pw_core_); pw_core_ = nullptr; }
  if (pw_context_) { pw_context_destroy(pw_context_); pw_context_ = nullptr; }
  if (pw_loop) { pw_thread_loop_stop(pw_loop); pw_thread_loop_destroy(pw_loop); pw_loop = nullptr; }
  pw_initialized = false;
#endif
#ifdef HAVE_PULSEAUDIO
  paCleanup();
#endif
#ifdef HAVE_ALSA
  alsaCleanup();
#endif
#ifdef _WIN32
  winCleanup();
#endif
}

std::string AudioBackendImpl::getName() const {
  if (useCli_) return std::string("CLI/") + (
    activeBackend == AudioBackend::PIPEWIRE ? "PipeWire" :
    activeBackend == AudioBackend::PULSE ? "PulseAudio" :
    activeBackend == AudioBackend::WINDOWS ? "Windows" : "ALSA");
  switch (activeBackend) {
    case AudioBackend::PIPEWIRE: return "PipeWire";
    case AudioBackend::PULSE: return "PulseAudio";
    case AudioBackend::ALSA: return "ALSA";
    case AudioBackend::WINDOWS: return "Windows";
    default: return "unknown";
  }
}

// ============================================================
// CLI helpers (cross-platform)
// ============================================================
std::string AudioBackendImpl::cliExec(const std::string &cmd) const {
  std::string result;
  auto pipe = popen(cmd.c_str(), "r");
  if (!pipe) return result;
  std::array<char, 512> buf;
  while (fgets(buf.data(), buf.size(), pipe)) result += buf.data();
  pclose(pipe);
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
  return result;
}

bool AudioBackendImpl::cliSetVolume(const std::string &device, double volume) const {
  int pct = static_cast<int>(std::clamp(volume, 0.0, 1.5) * 100);
  std::string dev = device.empty() ? "" : device;
#ifdef __linux__
  if (activeBackend == AudioBackend::PIPEWIRE) {
    std::string cmd = "wpctl set-volume ";
    cmd += dev.empty() ? "@DEFAULT_AUDIO_SINK@" : dev;
    cmd += " " + std::to_string(pct) + "% 2>/dev/null";
    return system(cmd.c_str()) == 0;
  }
  if (activeBackend == AudioBackend::PULSE) {
    std::string sink = dev.empty() ? "@DEFAULT_SINK@" : dev;
    return system(("pactl set-sink-volume " + sink + " " + std::to_string(pct) + "% 2>/dev/null").c_str()) == 0;
  }
  if (activeBackend == AudioBackend::ALSA) {
    std::string cmd = "amixer set Master " + std::to_string(pct) + "% 2>/dev/null";
    if (!dev.empty()) cmd = "amixer sset '" + dev + "' " + std::to_string(pct) + "% 2>/dev/null";
    return system(cmd.c_str()) == 0;
  }
#endif
#ifdef _WIN32
  (void)dev; (void)pct;
  return system("powershell -c \"$wsh = New-Object -ComObject WScript.Shell; $wsh.SendKeys([char]175)\" 2>nul") == 0;
#endif
  return false;
}

double AudioBackendImpl::cliGetVolume(const std::string &device) const {
#ifdef __linux__
  if (activeBackend == AudioBackend::PIPEWIRE) {
    std::string cmd = "wpctl get-volume ";
    cmd += device.empty() ? "@DEFAULT_AUDIO_SINK@" : device;
    auto out = cliExec(cmd + " 2>/dev/null");
    auto pos = out.find("Volume:");
    if (pos != std::string::npos) {
      auto numStart = out.find_first_of("0123456789.", pos);
      if (numStart != std::string::npos) {
        try { return std::stod(out.substr(numStart)); } catch (...) {}
      }
    }
    return 1.0;
  }
  if (activeBackend == AudioBackend::PULSE) {
    std::string sink = device.empty() ? "@DEFAULT_SINK@" : device;
    auto out = cliExec("pactl get-sink-volume " + sink + " 2>/dev/null");
    auto pos = out.find('/');
    if (pos != std::string::npos) {
      auto pct = out.find('%', pos);
      if (pct != std::string::npos) {
        try { return std::stoi(out.substr(pos + 1, pct - pos - 1)) / 100.0; } catch (...) {}
      }
    }
    return 1.0;
  }
  if (activeBackend == AudioBackend::ALSA) {
    auto out = cliExec("amixer get Master 2>/dev/null | grep -oP '\\d+%' | head -1");
    if (!out.empty() && out.back() == '%') {
      try { return std::stoi(out.substr(0, out.size() - 1)) / 100.0; } catch (...) {}
    }
    return 1.0;
  }
#endif
#ifdef _WIN32
  auto out = cliExec("powershell -c \"(New-Object -ComObject WScript.Shell)\" 2>nul");
  (void)device;
  return 1.0;
#endif
  return 1.0;
}

bool AudioBackendImpl::cliSetMute(const std::string &device, bool muted) const {
#ifdef __linux__
  if (activeBackend == AudioBackend::PIPEWIRE) {
    std::string cmd = "wpctl set-mute ";
    cmd += device.empty() ? "@DEFAULT_AUDIO_SINK@" : device;
    cmd += muted ? " 1" : " 0";
    return system((cmd + " 2>/dev/null").c_str()) == 0;
  }
  if (activeBackend == AudioBackend::PULSE) {
    std::string sink = device.empty() ? "@DEFAULT_SINK@" : device;
    return system(("pactl set-sink-mute " + sink + " " + std::string(muted ? "1" : "0") + " 2>/dev/null").c_str()) == 0;
  }
  if (activeBackend == AudioBackend::ALSA) {
    return system(("amixer set Master " + std::string(muted ? "mute" : "unmute") + " 2>/dev/null").c_str()) == 0;
  }
#endif
#ifdef _WIN32
  (void)device;
  return system(("powershell -c \"$wsh = New-Object -ComObject WScript.Shell; $wsh.SendKeys([char]" +
    std::string(muted ? "173" : "175") + ")\" 2>nul").c_str()) == 0;
#endif
  return false;
}

bool AudioBackendImpl::cliIsMuted(const std::string &device) const {
#ifdef __linux__
  if (activeBackend == AudioBackend::PIPEWIRE) {
    std::string cmd = "wpctl get-volume ";
    cmd += device.empty() ? "@DEFAULT_AUDIO_SINK@" : device;
    auto out = cliExec(cmd + " 2>/dev/null");
    return out.find("[MUTED]") != std::string::npos;
  }
  if (activeBackend == AudioBackend::PULSE) {
    std::string sink = device.empty() ? "@DEFAULT_SINK@" : device;
    auto out = cliExec("pactl get-sink-mute " + sink + " 2>/dev/null");
    return out.find("yes") != std::string::npos;
  }
  if (activeBackend == AudioBackend::ALSA) {
    auto out = cliExec("amixer get Master 2>/dev/null | grep -oP '\\[on\\]|\\[off\\]' | head -1");
    return out.find("[off]") != std::string::npos;
  }
#endif
#ifdef _WIN32
  (void)device;
  auto out = cliExec("powershell -c \"(New-Object -ComObject WScript.Shell)\" 2>nul");
  return false;
#endif
  return false;
}

std::vector<AudioDevice> AudioBackendImpl::cliGetDevices(bool input) const {
  std::vector<AudioDevice> devs;
#ifdef __linux__
  if (activeBackend == AudioBackend::PIPEWIRE) {
    auto out = cliExec("wpctl status 2>/dev/null");
    // Parse wpctl status output for Audio/Sinks and Audio/Sources
    std::istringstream ss(out);
    std::string line;
    bool inSection = false;
    std::string section = input ? "Source" : "Sink";
    while (std::getline(ss, line)) {
      if (line.find(section) != std::string::npos) { inSection = true; continue; }
      if (inSection && (line.empty() || (line.find("Sink") != std::string::npos && !input) ||
          (line.find("Source") != std::string::npos && input))) {
        if (!line.empty()) inSection = false;
        continue;
      }
      if (!inSection) continue;
      // Parse lines like " 1. alsa_output.pci-0000_00_1f.3.analog-stereo [ALC892 Analog...]
      auto dot = line.find('.');
      if (dot == std::string::npos) continue;
      auto bracket = line.find('[');
      if (bracket == std::string::npos) continue;
      auto bracketEnd = line.find(']', bracket);
      if (bracketEnd == std::string::npos) continue;
      AudioDevice d;
      try { d.index = static_cast<uint32_t>(std::stoi(line.substr(0, dot))); } catch (...) {}
      d.name = line.substr(dot + 1, bracket - dot - 2);
      while (!d.name.empty() && d.name.front() == ' ') d.name.erase(d.name.begin());
      d.description = line.substr(bracket + 1, bracketEnd - bracket - 1);
      d.isInput = input;
      d.isDefault = line.find('*') != std::string::npos;
      devs.push_back(d);
    }
  }
  if (activeBackend == AudioBackend::PULSE) {
    std::string cmd = input ? "pactl list sources 2>/dev/null" : "pactl list sinks 2>/dev/null";
    auto out = cliExec(cmd);
    std::istringstream ss(out);
    std::string line;
    AudioDevice d;
    bool inDevice = false;
    while (std::getline(ss, line)) {
      if (line.find("Sink #") != std::string::npos || line.find("Source #") != std::string::npos) {
        if (inDevice && !d.name.empty()) devs.push_back(d);
        d = AudioDevice();
        d.isInput = input;
        inDevice = true;
        auto hash = line.find('#');
        if (hash != std::string::npos) {
          try { d.index = static_cast<uint32_t>(std::stoi(line.substr(hash + 1))); } catch (...) {}
        }
        continue;
      }
      if (!inDevice) continue;
      if (line.find("Name:") != std::string::npos) {
        auto colon = line.find(':');
        d.name = line.substr(colon + 2);
      }
      if (line.find("Description:") != std::string::npos) {
        auto colon = line.find(':');
        d.description = line.substr(colon + 2);
      }
      if (line.find("Mute:") != std::string::npos) {
        d.isMuted = line.find("yes") != std::string::npos;
      }
      if (line.find("Volume:") != std::string::npos) {
        auto pct = line.find('%');
        if (pct != std::string::npos && pct > 0) {
          auto start = pct - 1;
          while (start > 0 && line[start] >= '0' && line[start] <= '9') start--;
          try { d.volume = std::stoi(line.substr(start + 1, pct - start - 1)) / 100.0; } catch (...) {}
        }
      }
      if (line.find("State:") != std::string::npos) {
        d.isDefault = (line.find("RUNNING") != std::string::npos);
      }
    }
    if (inDevice && !d.name.empty()) devs.push_back(d);
  }
  if (activeBackend == AudioBackend::ALSA) {
    auto out = cliExec("amixer controls 2>/dev/null");
    std::istringstream ss(out);
    std::string line;
    int idx = 0;
    while (std::getline(ss, line)) {
      AudioDevice d;
      d.index = static_cast<uint32_t>(idx++);
      d.isInput = input;
      auto numStart = line.find("numid=");
      auto iface = line.find(",iface=");
      if (numStart != std::string::npos && iface != std::string::npos) {
        try { d.index = static_cast<uint32_t>(std::stoi(line.substr(numStart + 6, iface - numStart - 6))); } catch (...) {}
      }
      auto nameStart = line.find("name='");
      if (nameStart != std::string::npos) {
        nameStart += 6;
        auto nameEnd = line.find('\'', nameStart);
        d.name = line.substr(nameStart, nameEnd - nameStart);
        d.description = d.name;
      }
      if (!d.name.empty()) devs.push_back(d);
    }
  }
#endif
#ifdef _WIN32
  (void)input;
  AudioDevice d;
  d.name = "default";
  d.description = "Windows Default";
  d.isDefault = true;
  devs.push_back(d);
#endif
  return devs;
}

std::string AudioBackendImpl::cliGetDefaultOutput() const {
#ifdef __linux__
  if (activeBackend == AudioBackend::PIPEWIRE) {
    auto out = cliExec("wpctl get-default @DEFAULT_AUDIO_SINK@ 2>/dev/null");
    if (!out.empty()) return out;
  }
  if (activeBackend == AudioBackend::PULSE) {
    auto out = cliExec("pactl get-default-sink 2>/dev/null");
    if (!out.empty()) return out;
  }
#endif
  return "default";
}

std::string AudioBackendImpl::cliGetDefaultInput() const {
#ifdef __linux__
  if (activeBackend == AudioBackend::PIPEWIRE) {
    auto out = cliExec("wpctl get-default @DEFAULT_AUDIO_SOURCE@ 2>/dev/null");
    if (!out.empty()) return out;
  }
  if (activeBackend == AudioBackend::PULSE) {
    auto out = cliExec("pactl get-default-source 2>/dev/null");
    if (!out.empty()) return out;
  }
#endif
  return "default";
}

bool AudioBackendImpl::cliSetApplicationVolume(const std::string &appName, double volume) const {
#ifdef __linux__
  int pct = static_cast<int>(std::clamp(volume, 0.0, 1.5) * 100);
  if (activeBackend == AudioBackend::PULSE || activeBackend == AudioBackend::PIPEWIRE) {
    return system(("pactl set-sink-input-volume " + appName + " " + std::to_string(pct) + "% 2>/dev/null").c_str()) == 0;
  }
#endif
  (void)appName; (void)volume;
  return false;
}

double AudioBackendImpl::cliGetApplicationVolume(const std::string &appName) const {
#ifdef __linux__
  if (activeBackend == AudioBackend::PULSE || activeBackend == AudioBackend::PIPEWIRE) {
    auto out = cliExec("pactl list sink-inputs 2>/dev/null | grep -A5 'application.name = \"" + appName + "\"' | grep Volume | head -1");
    auto pct = out.find('%');
    if (pct != std::string::npos && pct > 0) {
      auto start = pct - 1;
      while (start > 0 && out[start] >= '0' && out[start] <= '9') start--;
      try { return std::stoi(out.substr(start + 1, pct - start - 1)) / 100.0; } catch (...) {}
    }
  }
#endif
  (void)appName;
  return 1.0;
}

std::vector<AudioManager::ApplicationInfo> AudioBackendImpl::cliGetApplications() const {
  std::vector<AudioManager::ApplicationInfo> apps;
#ifdef __linux__
  if (activeBackend == AudioBackend::PULSE || activeBackend == AudioBackend::PIPEWIRE) {
    auto out = cliExec("pactl list sink-inputs 2>/dev/null");
    std::istringstream ss(out);
    std::string line;
    AudioManager::ApplicationInfo app;
    bool inEntry = false;
    while (std::getline(ss, line)) {
      if (line.find("Sink Input #") != std::string::npos) {
        if (inEntry && !app.name.empty()) apps.push_back(app);
        app = AudioManager::ApplicationInfo();
        inEntry = true;
        auto hash = line.find('#');
        if (hash != std::string::npos) {
          try { app.sinkInputIndex = static_cast<uint32_t>(std::stoi(line.substr(hash + 1))); app.index = app.sinkInputIndex; } catch (...) {}
        }
        continue;
      }
      if (!inEntry) continue;
      if (line.find("application.name") != std::string::npos) {
        auto eq = line.find('=');
        if (eq != std::string::npos) {
          auto start = line.find('"', eq);
          auto end = line.find('"', start + 1);
          if (start != std::string::npos && end != std::string::npos)
            app.name = line.substr(start + 1, end - start - 1);
        }
      }
      if (line.find("application.icon_name") != std::string::npos) {
        auto eq = line.find('=');
        if (eq != std::string::npos) {
          auto start = line.find('"', eq);
          auto end = line.find('"', start + 1);
          if (start != std::string::npos && end != std::string::npos)
            app.icon = line.substr(start + 1, end - start - 1);
        }
      }
      if (line.find("Mute:") != std::string::npos) {
        app.isMuted = line.find("yes") != std::string::npos;
      }
      if (line.find("Volume:") != std::string::npos) {
        auto pctPos = line.find('%');
        if (pctPos != std::string::npos && pctPos > 0) {
          auto start = pctPos - 1;
          while (start > 0 && line[start] >= '0' && line[start] <= '9') start--;
          try { app.volume = std::stoi(line.substr(start + 1, pctPos - start - 1)) / 100.0; } catch (...) {}
        }
      }
    }
    if (inEntry && !app.name.empty()) apps.push_back(app);
  }
#endif
  return apps;
}

// ============================================================
// Native PipeWire
// ============================================================
#ifdef HAVE_PIPEWIRE
static void pw_on_core_sync_done(void *data, uint32_t, int seq) {
  auto *self = static_cast<AudioBackendImpl *>(data);
  if (seq == self->pw_sync_seq) {
    self->pw_ready = true;
    pw_thread_loop_signal(self->pw_loop, false);
  }
}

static void pw_on_core_error(void *data, uint32_t, int, int res, const char *msg) {
  auto *self = static_cast<AudioBackendImpl *>(data);
  if (res < 0) self->pw_failed = true;
  pw_thread_loop_signal(self->pw_loop, false);
}

static void pw_on_node_info(void *data, const struct pw_node_info *info) {
  auto *self = static_cast<AudioBackendImpl *>(data);
  if (!info || !info->props) return;
  std::lock_guard<std::mutex> lk(self->pw_mutex);
  auto it = self->pw_nodes.find(info->id);
  if (it == self->pw_nodes.end()) return;
  auto &node = it->second;
  const char *v;
  if ((v = spa_dict_lookup(info->props, "node.name"))) node.name = v;
  if ((v = spa_dict_lookup(info->props, "node.description"))) node.description = v;
  if ((v = spa_dict_lookup(info->props, "media.class"))) node.mediaClass = v;
  // Parse volume from Props
  if (info->props) {
    if ((v = spa_dict_lookup(info->props, "node.muted"))) node.isMuted = (strcmp(v, "true") == 0);
  }
  // Volume from Props param must be read via pw_node_enum_params
  // which is async; use wpctl for reliable read instead
}

static void pw_on_registry_global(void *data, uint32_t id, uint32_t,
    const char *type, uint32_t, const struct spa_dict *props) {
  auto *self = static_cast<AudioBackendImpl *>(data);
  if (!props || strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;
  const char *cls = spa_dict_lookup(props, "media.class");
  if (!cls) return;
  // Only care about Audio/Sink and Audio/Source
  if (strcmp(cls, "Audio/Sink") != 0 && strcmp(cls, "Audio/Source") != 0 &&
      strcmp(cls, "Audio/Duplex") != 0 && strcmp(cls, "Stream/Output/Audio") != 0 &&
      strcmp(cls, "Stream/Input/Audio") != 0)
    return;

  AudioBackendImpl::PWNode node;
  node.id = id;
  node.proxy = static_cast<pw_proxy *>(pw_registry_bind(self->pw_registry_, id, type, PW_VERSION_NODE, 0));
  if (!node.proxy) return;
  const char *v;
  if ((v = spa_dict_lookup(props, "node.name"))) node.name = v;
  if ((v = spa_dict_lookup(props, "node.description"))) node.description = v;
  node.mediaClass = cls;
  node.node_events.version = PW_VERSION_NODE_EVENTS;
  node.node_events.info = pw_on_node_info;
  pw_node_add_listener(reinterpret_cast<pw_node *>(node.proxy), &node.node_listener,
                        &node.node_events, data);
  std::lock_guard<std::mutex> lk(self->pw_mutex);
  self->pw_nodes[id] = std::move(node);
}

static void pw_on_registry_global_remove(void *data, uint32_t id) {
  auto *self = static_cast<AudioBackendImpl *>(data);
  std::lock_guard<std::mutex> lk(self->pw_mutex);
  self->pw_nodes.erase(id);
}

bool AudioBackendImpl::initNativePipeWire() {
  pw_init(nullptr, nullptr);
  pw_loop = pw_thread_loop_new("havel-audio-pw", nullptr);
  if (!pw_loop) return false;
  pw_context_ = pw_context_new(pw_thread_loop_get_loop(pw_loop), nullptr, 0);
  if (!pw_context_) { pw_thread_loop_destroy(pw_loop); pw_loop = nullptr; return false; }
  pw_core_ = pw_context_connect(pw_context_, nullptr, 0);
  if (!pw_core_) { pw_context_destroy(pw_context_); pw_context_ = nullptr; pw_thread_loop_destroy(pw_loop); pw_loop = nullptr; return false; }

    static const pw_core_events core_evt = {
        .version = PW_VERSION_CORE_EVENTS,
        .info = nullptr,
        .done = pw_on_core_sync_done,
        .ping = nullptr,
        .error = pw_on_core_error,
        .remove_id = nullptr,
        .bound_id = nullptr,
        .add_mem = nullptr,
        .remove_mem = nullptr,
        .bound_props = nullptr,
    };
  pw_core_add_listener(pw_core_, &pw_core_listener, &core_evt, this);

  pw_registry_ = pw_core_get_registry(pw_core_, PW_VERSION_REGISTRY, 0);
  static const pw_registry_events reg_evt = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = pw_on_registry_global,
    .global_remove = pw_on_registry_global_remove,
  };
  pw_registry_add_listener(pw_registry_, &pw_registry_listener, &reg_evt, this);

  if (pw_thread_loop_start(pw_loop) < 0) return false;
  pwSyncWait();
  if (pw_failed) return false;
  pw_initialized = true;
  pwStart();
  return true;
}

void AudioBackendImpl::pwStart() {
  pw_running = true;
  pw_cmd_thread = std::thread([this] { pwProcess(); });
}

void AudioBackendImpl::pwStop() {
  pw_running = false;
  pw_cmd_cv.notify_all();
  if (pw_cmd_thread.joinable()) pw_cmd_thread.join();
}

void AudioBackendImpl::pwProcess() {
  while (pw_running) {
    std::function<void()> fn;
    {
      std::unique_lock lk(pw_cmd_mutex);
      pw_cmd_cv.wait_for(lk, std::chrono::milliseconds(100), [this] { return !pw_cmds.empty() || !pw_running; });
      if (!pw_running) return;
      if (pw_cmds.empty()) continue;
      fn = std::move(pw_cmds.front()); pw_cmds.pop();
    }
    fn();
  }
}

void AudioBackendImpl::pwSyncWait() {
  pw_thread_loop_lock(pw_loop);
  pw_ready = false;
  pw_sync_seq = pw_core_sync(pw_core_, PW_ID_CORE, pw_sync_seq);
  while (!pw_ready && !pw_failed) pw_thread_loop_wait(pw_loop);
  pw_thread_loop_unlock(pw_loop);
}

uint32_t AudioBackendImpl::pwFindDefaultNode(bool input) const {
  // Use metadata to find default node
  // For now, find first node of the right class
  std::lock_guard<std::mutex> lk(pw_mutex);
  for (const auto &[id, n] : pw_nodes) {
    if (input && (n.mediaClass == "Audio/Source" || n.mediaClass == "Audio/Duplex")) return id;
    if (!input && (n.mediaClass == "Audio/Sink" || n.mediaClass == "Audio/Duplex")) return id;
  }
  return 0;
}

bool AudioBackendImpl::pwSetNodeVolume(uint32_t id, double vol) {
  std::string nodeName;
  {
    std::lock_guard<std::mutex> lk(pw_mutex);
    auto it = pw_nodes.find(id);
    if (it == pw_nodes.end()) return false;
    nodeName = it->second.name;
  }
  int pct = static_cast<int>(std::clamp(vol, 0.0, 1.5) * 100);
  return system(("wpctl set-volume " + nodeName + " " + std::to_string(pct) + "% 2>/dev/null").c_str()) == 0;
}

bool AudioBackendImpl::pwSetNodeMute(uint32_t id, bool mute) {
  std::string nodeName;
  {
    std::lock_guard<std::mutex> lk(pw_mutex);
    auto it = pw_nodes.find(id);
    if (it == pw_nodes.end()) return false;
    nodeName = it->second.name;
  }
  return system(("wpctl set-mute " + nodeName + (mute ? " 1" : " 0") + " 2>/dev/null").c_str()) == 0;
}

double AudioBackendImpl::pwGetNodeVolume(uint32_t id) const {
  std::lock_guard<std::mutex> lk(pw_mutex);
  auto it = pw_nodes.find(id);
  if (it != pw_nodes.end()) return it->second.volume;
  return 1.0;
}

bool AudioBackendImpl::pwGetNodeMuted(uint32_t id) const {
  std::lock_guard<std::mutex> lk(pw_mutex);
  auto it = pw_nodes.find(id);
  if (it != pw_nodes.end()) return it->second.isMuted;
  return false;
}

std::vector<AudioDevice> AudioBackendImpl::pwGetDevices(bool input) const {
  std::vector<AudioDevice> devs;
  std::lock_guard<std::mutex> lk(pw_mutex);
  for (const auto &[id, n] : pw_nodes) {
    bool isInput = (n.mediaClass == "Audio/Source" || n.mediaClass == "Audio/Duplex");
    bool isOutput = (n.mediaClass == "Audio/Sink" || n.mediaClass == "Audio/Duplex");
    if (input && !isInput) continue;
    if (!input && !isOutput) continue;
    AudioDevice d;
    d.name = n.name;
    d.description = n.description;
    d.index = n.id;
    d.volume = n.volume;
    d.isMuted = n.isMuted;
    d.isInput = input;
    devs.push_back(d);
  }
  return devs;
}

std::string AudioBackendImpl::pwGetDefault(bool input) const {
  uint32_t id = pwFindDefaultNode(input);
  if (id == 0) return "default";
  std::lock_guard<std::mutex> lk(pw_mutex);
  auto it = pw_nodes.find(id);
  if (it != pw_nodes.end()) return it->second.name;
  return "default";
}

bool AudioBackendImpl::pwSetAppVolume(const std::string &, double) {
  // PipeWire per-app volume requires stream restore API
  return false;
}

double AudioBackendImpl::pwGetAppVolume(const std::string &) const {
  return 1.0;
}

std::vector<AudioManager::ApplicationInfo> AudioBackendImpl::pwGetApplications() const {
  // PipeWire application listing requires session manager protocol
  // Fall through to CLI
  return const_cast<AudioBackendImpl *>(this)->cliGetApplications();
}
#endif

// ============================================================
// Native PulseAudio
// ============================================================
#ifdef HAVE_PULSEAUDIO
bool AudioBackendImpl::initNativePulseAudio() {
  pa_ml = pa_threaded_mainloop_new();
  if (!pa_ml) return false;
  pa_ctx = pa_context_new(pa_threaded_mainloop_get_api(pa_ml), "havel-audio");
  if (!pa_ctx) { pa_threaded_mainloop_free(pa_ml); pa_ml = nullptr; return false; }
  pa_context_set_state_callback(pa_ctx, [](pa_context *c, void *ud) {
    pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(ud), 0);
  }, pa_ml);
  if (pa_threaded_mainloop_start(pa_ml) < 0) {
    pa_context_unref(pa_ctx); pa_ctx = nullptr;
    pa_threaded_mainloop_free(pa_ml); pa_ml = nullptr;
    return false;
  }
  pa_threaded_mainloop_lock(pa_ml);
  if (pa_context_connect(pa_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
    pa_threaded_mainloop_unlock(pa_ml);
    return false;
  }
  while (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) {
    if (pa_context_get_state(pa_ctx) == PA_CONTEXT_FAILED) break;
    pa_threaded_mainloop_wait(pa_ml);
  }
  pa_threaded_mainloop_unlock(pa_ml);
  if (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) { paCleanup(); return false; }

  // Subscribe to changes
  pa_threaded_mainloop_lock(pa_ml);
  pa_operation *op = pa_context_subscribe(pa_ctx, static_cast<pa_subscription_mask_t>(
    PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SINK_INPUT),
    [](pa_context *, int, void *ud) { pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(ud), 0); }, pa_ml);
  if (op) { while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml); pa_operation_unref(op); }
  pa_threaded_mainloop_unlock(pa_ml);

  // Cache default devices
  defaultOutputDevice = paGetDefault(false);
  defaultInputDevice = paGetDefault(true);
  return true;
}

void AudioBackendImpl::paCleanup() {
  if (pa_ctx) { pa_context_disconnect(pa_ctx); pa_context_unref(pa_ctx); pa_ctx = nullptr; }
  if (pa_ml) { pa_threaded_mainloop_stop(pa_ml); pa_threaded_mainloop_free(pa_ml); pa_ml = nullptr; }
}

void AudioBackendImpl::paSetVolume(const std::string &device, double vol) {
  pa_threaded_mainloop_lock(pa_ml);
  pa_cvolume cv;
  pa_cvolume_set(&cv, 2, pa_sw_volume_from_linear(vol));
  pa_operation *op = pa_context_set_sink_volume_by_name(pa_ctx, device.c_str(), &cv, nullptr, nullptr);
  if (op) pa_operation_unref(op);
  pa_threaded_mainloop_unlock(pa_ml);
}

double AudioBackendImpl::paGetVolume(const std::string &device) {
  pa_threaded_mainloop_lock(pa_ml);
  double vol = 1.0;
  PAResultDouble r{&vol, pa_ml};
  pa_operation *op = pa_context_get_sink_info_by_name(pa_ctx, device.c_str(),
    [](pa_context *, const pa_sink_info *i, int, void *ud) {
      auto *r = static_cast<PAResultDouble *>(ud);
      if (i) *r->out = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
      pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(r->ml), 0);
    }, &r);
  if (op) { while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml); pa_operation_unref(op); }
  pa_threaded_mainloop_unlock(pa_ml);
  return vol;
}

void AudioBackendImpl::paSetMute(const std::string &device, bool mute) {
  pa_threaded_mainloop_lock(pa_ml);
  pa_operation *op = pa_context_set_sink_mute_by_name(pa_ctx, device.c_str(), mute ? 1 : 0, nullptr, nullptr);
  if (op) pa_operation_unref(op);
  pa_threaded_mainloop_unlock(pa_ml);
}

bool AudioBackendImpl::paGetMuted(const std::string &device) {
  pa_threaded_mainloop_lock(pa_ml);
  bool muted = false;
  PAResultBool r{&muted, pa_ml};
  pa_operation *op = pa_context_get_sink_info_by_name(pa_ctx, device.c_str(),
    [](pa_context *, const pa_sink_info *i, int, void *ud) {
      auto *r = static_cast<PAResultBool *>(ud);
      if (i) *r->out = i->mute != 0;
      pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(r->ml), 0);
    }, &r);
  if (op) { while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml); pa_operation_unref(op); }
  pa_threaded_mainloop_unlock(pa_ml);
  return muted;
}

std::vector<AudioDevice> AudioBackendImpl::paDevices(bool input) {
  std::vector<AudioDevice> devs;
  pa_threaded_mainloop_lock(pa_ml);
  PAResultDevices r{&devs, pa_ml};
  pa_operation *op;
  if (input) {
    op = pa_context_get_source_info_list(pa_ctx,
      [](pa_context *, const pa_source_info *i, int eol, void *ud) {
        auto *r = static_cast<PAResultDevices *>(ud);
        if (!i) { if (eol) pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(r->ml), 0); return; }
        AudioDevice d; d.name = i->name; d.description = i->description;
        d.index = i->index; d.volume = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
        d.isMuted = i->mute != 0; d.channels = i->sample_spec.channels; d.isInput = true;
        r->out->push_back(d);
      }, &r);
  } else {
    op = pa_context_get_sink_info_list(pa_ctx,
      [](pa_context *, const pa_sink_info *i, int eol, void *ud) {
        auto *r = static_cast<PAResultDevices *>(ud);
        if (!i) { if (eol) pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(r->ml), 0); return; }
        AudioDevice d; d.name = i->name; d.description = i->description;
        d.index = i->index; d.volume = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
        d.isMuted = i->mute != 0; d.channels = i->sample_spec.channels; d.isInput = false;
        r->out->push_back(d);
      }, &r);
  }
  if (op) { while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml); pa_operation_unref(op); }
  pa_threaded_mainloop_unlock(pa_ml);
  return devs;
}

std::string AudioBackendImpl::paGetDefault(bool input) const {
  if (!pa_ml || !pa_ctx) return "default";
  pa_threaded_mainloop_lock(pa_ml);
  std::string result;
  PAResultString r{&result, pa_ml};
  pa_operation *op;
  if (input) {
    op = pa_context_get_server_info(pa_ctx,
      [](pa_context *, const pa_server_info *i, void *ud) {
        auto *r = static_cast<PAResultString *>(ud);
        if (i && i->default_source_name) *r->out = i->default_source_name;
        pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(r->ml), 0);
      }, &r);
  } else {
    op = pa_context_get_server_info(pa_ctx,
      [](pa_context *, const pa_server_info *i, void *ud) {
        auto *r = static_cast<PAResultString *>(ud);
        if (i && i->default_sink_name) *r->out = i->default_sink_name;
        pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(r->ml), 0);
      }, &r);
  }
  if (op) { while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml); pa_operation_unref(op); }
  pa_threaded_mainloop_unlock(pa_ml);
  return result.empty() ? "default" : result;
}

bool AudioBackendImpl::paSetAppVolume(const std::string &appName, double vol) {
  if (!pa_ml || !pa_ctx) return false;
  pa_threaded_mainloop_lock(pa_ml);
  pa_cvolume cv;
  pa_cvolume_set(&cv, 2, pa_sw_volume_from_linear(vol));
  // Find sink input by app name
  struct AppVolData { const std::string *name; pa_cvolume *cvol; pa_context *ctx; pa_threaded_mainloop *ml; bool found; };
  AppVolData data{&appName, &cv, pa_ctx, pa_ml, false};
  pa_operation *op = pa_context_get_sink_input_info_list(pa_ctx,
    [](pa_context *ctx, const pa_sink_input_info *i, int eol, void *ud) {
      auto *d = static_cast<AppVolData *>(ud);
      if (!i) { if (eol) pa_threaded_mainloop_signal(d->ml, 0); return; }
      const char *name = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
      if (name && *d->name == name) {
        pa_operation *setOp = pa_context_set_sink_input_volume(ctx, i->index, d->cvol, nullptr, nullptr);
        if (setOp) pa_operation_unref(setOp);
        d->found = true;
      }
    }, &data);
  if (op) { while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml); pa_operation_unref(op); }
  pa_threaded_mainloop_unlock(pa_ml);
  return data.found;
}

double AudioBackendImpl::paGetAppVolume(const std::string &appName) const {
  if (!pa_ml || !pa_ctx) return 1.0;
  pa_threaded_mainloop_lock(pa_ml);
  double vol = 1.0;
  struct AppVolData { const std::string *name; double *vol; pa_threaded_mainloop *ml; bool found; };
  AppVolData data{&appName, &vol, pa_ml, false};
  pa_operation *op = pa_context_get_sink_input_info_list(pa_ctx,
    [](pa_context *, const pa_sink_input_info *i, int eol, void *ud) {
      auto *d = static_cast<AppVolData *>(ud);
      if (!i) { if (eol) pa_threaded_mainloop_signal(d->ml, 0); return; }
      const char *name = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
      if (name && *d->name == name) {
        *d->vol = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
        d->found = true;
      }
    }, &data);
  if (op) { while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml); pa_operation_unref(op); }
  pa_threaded_mainloop_unlock(pa_ml);
  return vol;
}

std::vector<AudioManager::ApplicationInfo> AudioBackendImpl::paGetApplications() const {
  std::vector<AudioManager::ApplicationInfo> apps;
  if (!pa_ml || !pa_ctx) return apps;
  pa_threaded_mainloop_lock(pa_ml);
  PAResultApps r{&apps, pa_ml};
  pa_operation *op = pa_context_get_sink_input_info_list(pa_ctx,
    [](pa_context *, const pa_sink_input_info *i, int eol, void *ud) {
      auto *r = static_cast<PAResultApps *>(ud);
      if (!i) { if (eol) pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(r->ml), 0); return; }
      AudioManager::ApplicationInfo app;
      app.sinkInputIndex = i->index;
      app.index = i->index;
      app.volume = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
      app.isMuted = (i->mute != 0);
      const char *name = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
      if (name) app.name = name;
      const char *icon = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_ICON_NAME);
      if (icon) app.icon = icon;
      r->out->push_back(app);
    }, &r);
  if (op) { while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml); pa_operation_unref(op); }
  pa_threaded_mainloop_unlock(pa_ml);
  return apps;
}

std::string AudioBackendImpl::paGetActiveApplicationName() const {
  auto apps = paGetApplications();
  if (!apps.empty()) return apps[0].name;
  return "";
}
#endif

// ============================================================
// Native ALSA
// ============================================================
#ifdef HAVE_ALSA
bool AudioBackendImpl::initNativeAlsa() {
  snd_mixer_t *handle = nullptr;
  if (snd_mixer_open(&handle, 0) < 0) return false;
  if (snd_mixer_attach(handle, "default") < 0) { snd_mixer_close(handle); return false; }
  if (snd_mixer_selem_register(handle, nullptr, nullptr) < 0) { snd_mixer_close(handle); return false; }
  if (snd_mixer_load(handle) < 0) { snd_mixer_close(handle); return false; }

  for (snd_mixer_elem_t *e = snd_mixer_first_elem(handle); e; e = snd_mixer_elem_next(e)) {
    if (snd_mixer_elem_get_type(e) != SND_MIXER_ELEM_SIMPLE) continue;
    AlsaMixer mixer;
    mixer.handle = handle;
    mixer.elem = e;
    snd_mixer_selem_id_alloca(&mixer.sid);
    snd_mixer_selem_get_id(e, mixer.sid);
    mixer.name = snd_mixer_selem_id_get_name(mixer.sid);
    mixer.hasPlayback = snd_mixer_selem_has_playback_volume(e) != 0;
    mixer.hasCapture = snd_mixer_selem_has_capture_volume(e) != 0;
    if (!mixer.hasPlayback && !mixer.hasCapture) continue;
    alsa_mixers.push_back(mixer);
    if (!alsa_default_playback && mixer.hasPlayback) alsa_default_playback = &alsa_mixers.back();
    if (!alsa_default_capture && mixer.hasCapture) alsa_default_capture = &alsa_mixers.back();
  }
  return !alsa_mixers.empty();
}

void AudioBackendImpl::alsaCleanup() {
  // Only close the first handle (all AlsaMixer entries share same handle for "default")
  if (!alsa_mixers.empty() && alsa_mixers[0].handle) {
    snd_mixer_detach(alsa_mixers[0].handle, "default");
    snd_mixer_close(alsa_mixers[0].handle);
  }
  alsa_mixers.clear();
  alsa_default_playback = nullptr;
  alsa_default_capture = nullptr;
}

AudioBackendImpl::AlsaMixer *AudioBackendImpl::findAlsaMixer(const std::string &device, bool capture) const {
  if (device.empty() || device == "default") return capture ? alsa_default_capture : alsa_default_playback;
  for (auto &m : alsa_mixers) {
    if (m.name == device) return const_cast<AlsaMixer *>(&m);
  }
  return capture ? alsa_default_capture : alsa_default_playback;
}

bool AudioBackendImpl::alsaSetVolume(const std::string &device, double vol) {
  auto *m = const_cast<AlsaMixer *>(findAlsaMixer(device, false));
  if (!m || !m->elem) return false;
  long min, max;
  snd_mixer_selem_get_playback_volume_range(m->elem, &min, &max);
  long val = min + static_cast<long>(vol * (max - min));
  return snd_mixer_selem_set_playback_volume_all(m->elem, val) == 0;
}

double AudioBackendImpl::alsaGetVolume(const std::string &device) const {
  auto *m = findAlsaMixer(device, false);
  if (!m || !m->elem) return 1.0;
  long min, max, val;
  snd_mixer_selem_get_playback_volume_range(m->elem, &min, &max);
  snd_mixer_selem_get_playback_volume(m->elem, SND_MIXER_SCHN_FRONT_LEFT, &val);
  return max > min ? static_cast<double>(val - min) / (max - min) : 1.0;
}

bool AudioBackendImpl::alsaSetMute(const std::string &device, bool mute) {
  auto *m = const_cast<AlsaMixer *>(findAlsaMixer(device, false));
  if (!m || !m->elem) return false;
  return snd_mixer_selem_set_playback_switch_all(m->elem, mute ? 0 : 1) == 0;
}

bool AudioBackendImpl::alsaGetMuted(const std::string &device) const {
  auto *m = findAlsaMixer(device, false);
  if (!m || !m->elem) return false;
  int val;
  snd_mixer_selem_get_playback_switch(m->elem, SND_MIXER_SCHN_FRONT_LEFT, &val);
  return val == 0;
}

std::vector<AudioDevice> AudioBackendImpl::alsaGetDevices(bool input) const {
  std::vector<AudioDevice> devs;
  for (auto &m : alsa_mixers) {
    if (input && !m.hasCapture) continue;
    if (!input && !m.hasPlayback) continue;
    AudioDevice d;
    d.name = m.name;
    d.description = m.name;
    d.isInput = input;
    d.isDefault = (input ? alsa_default_capture : alsa_default_playback) == &m;
    if (m.elem) {
      if (m.hasPlayback) {
        long min, max, val;
        snd_mixer_selem_get_playback_volume_range(m.elem, &min, &max);
        snd_mixer_selem_get_playback_volume(m.elem, SND_MIXER_SCHN_FRONT_LEFT, &val);
        d.volume = max > min ? static_cast<double>(val - min) / (max - min) : 1.0;
        int sw;
        snd_mixer_selem_get_playback_switch(m.elem, SND_MIXER_SCHN_FRONT_LEFT, &sw);
        d.isMuted = sw == 0;
      }
    }
    devs.push_back(d);
  }
  return devs;
}
#endif

// ============================================================
// Native Windows
// ============================================================
#ifdef _WIN32
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <psapi.h>

#pragma comment(lib, "ole32.lib")

class CCoInit {
public:
  CCoInit() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~CCoInit() { if (SUCCEEDED(hr_)) CoUninitialize(); }
  HRESULT result() const { return hr_; }
private:
  HRESULT hr_;
};

bool AudioBackendImpl::initNativeWindows() {
  static CCoInit coin;
  return SUCCEEDED(coin.result());
}

void AudioBackendImpl::winCleanup() {}

float AudioBackendImpl::winGetVolume(const std::string &) {
  IMMDeviceEnumerator *enumerator = nullptr;
  IMMDevice *device = nullptr;
  IAudioEndpointVolume *volume = nullptr;
  float level = 1.0f;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&enumerator))) {
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
      if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, (void **)&volume))) {
        volume->GetMasterVolumeLevelScalar(&level);
        volume->Release();
      }
      device->Release();
    }
    enumerator->Release();
  }
  return level;
}

void AudioBackendImpl::winSetVolume(const std::string &, float vol) {
  IMMDeviceEnumerator *enumerator = nullptr;
  IMMDevice *device = nullptr;
  IAudioEndpointVolume *volume = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&enumerator))) {
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
      if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, (void **)&volume))) {
        volume->SetMasterVolumeLevelScalar(std::clamp(vol, 0.0f, 1.0f), nullptr);
        volume->Release();
      }
      device->Release();
    }
    enumerator->Release();
  }
}

bool AudioBackendImpl::winGetMuted(const std::string &) {
  IMMDeviceEnumerator *enumerator = nullptr;
  IMMDevice *device = nullptr;
  IAudioEndpointVolume *volume = nullptr;
  BOOL muted = FALSE;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&enumerator))) {
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
      if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, (void **)&volume))) {
        volume->GetMute(&muted);
        volume->Release();
      }
      device->Release();
    }
    enumerator->Release();
  }
  return muted != FALSE;
}

void AudioBackendImpl::winSetMute(const std::string &, bool mute) {
  IMMDeviceEnumerator *enumerator = nullptr;
  IMMDevice *device = nullptr;
  IAudioEndpointVolume *volume = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&enumerator))) {
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
      if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, (void **)&volume))) {
        volume->SetMute(mute ? TRUE : FALSE, nullptr);
        volume->Release();
      }
      device->Release();
    }
    enumerator->Release();
  }
}

std::vector<AudioDevice> AudioBackendImpl::winGetDevices(bool input) const {
  std::vector<AudioDevice> devs;
  IMMDeviceEnumerator *enumerator = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&enumerator))) {
    IMMDeviceCollection *collection = nullptr;
    EDataFlow flow = input ? eCapture : eRender;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection))) {
      UINT count = 0;
      collection->GetCount(&count);
      for (UINT i = 0; i < count; i++) {
        IMMDevice *device = nullptr;
        if (SUCCEEDED(collection->Item(i, &device))) {
          AudioDevice d;
          d.isInput = input;
          LPWSTR idStr = nullptr;
          if (SUCCEEDED(device->GetId(&idStr))) {
            char buf[256] = {};
            wcstombs(buf, idStr, sizeof(buf) - 1);
            d.name = buf;
            CoTaskMemFree(idStr);
          }
          IPropertyStore *props = nullptr;
          if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT val;
            PropVariantInit(&val);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &val))) {
              char buf[256] = {};
              wcstombs(buf, val.pwszVal, sizeof(buf) - 1);
              d.description = buf;
              PropVariantClear(&val);
            }
            props->Release();
          }
          IAudioEndpointVolume *volCtrl = nullptr;
          if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, (void **)&volCtrl))) {
            float level = 1.0f;
            volCtrl->GetMasterVolumeLevelScalar(&level);
            d.volume = static_cast<double>(level);
            BOOL muted = FALSE;
            volCtrl->GetMute(&muted);
            d.isMuted = muted != FALSE;
            volCtrl->Release();
          }
          devs.push_back(d);
          device->Release();
        }
      }
      collection->Release();
    }

    // Mark default device
    IMMDevice *defaultDev = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &defaultDev))) {
      LPWSTR defaultId = nullptr;
      if (SUCCEEDED(defaultDev->GetId(&defaultId))) {
        for (auto &d : devs) {
          char buf[256] = {};
          wcstombs(buf, defaultId, sizeof(buf) - 1);
          if (d.name == buf) d.isDefault = true;
        }
        CoTaskMemFree(defaultId);
      }
      defaultDev->Release();
    }

    enumerator->Release();
  }
  return devs;
}

std::string AudioBackendImpl::winGetDefault(bool input) const {
  std::string result = "default";
  IMMDeviceEnumerator *enumerator = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IMMDeviceEnumerator), (void **)&enumerator))) {
    IMMDevice *device = nullptr;
    EDataFlow flow = input ? eCapture : eRender;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device))) {
      LPWSTR idStr = nullptr;
      if (SUCCEEDED(device->GetId(&idStr))) {
        char buf[256] = {};
        wcstombs(buf, idStr, sizeof(buf) - 1);
        result = buf;
        CoTaskMemFree(idStr);
      }
      device->Release();
    }
    enumerator->Release();
  }
  return result;
}

bool AudioBackendImpl::winSetAppVolume(const std::string &, double) {
  // Windows per-app volume requires IAudioSessionManager2
  return false;
}

double AudioBackendImpl::winGetAppVolume(const std::string &) const {
  return 1.0;
}

std::vector<AudioManager::ApplicationInfo> AudioBackendImpl::winGetApplications() const {
  return {};
}
#endif

// ============================================================
// Dispatchers
// ============================================================
bool AudioBackendImpl::setVolume(const std::string &device, double volume) {
  volume = std::clamp(volume, 0.0, 1.5);
  if (useCli_) return cliSetVolume(device, volume);
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: {
    uint32_t id = device.empty() ? pwFindDefaultNode(false) : 0;
    if (id == 0) {
      // Try matching by name
      std::lock_guard<std::mutex> lk(pw_mutex);
      for (const auto &[nid, n] : pw_nodes) {
        if (n.name == device) { id = nid; break; }
      }
    }
    if (id != 0) return pwSetNodeVolume(id, volume);
    return cliSetVolume(device, volume);
  }
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: {
    std::string dev = device.empty() ? defaultOutputDevice : device;
    if (dev.empty()) dev = "@DEFAULT_SINK@";
    paSetVolume(dev, volume);
    return true;
  }
#endif
#ifdef HAVE_ALSA
  case AudioBackend::ALSA: return alsaSetVolume(device, volume);
#endif
#ifdef _WIN32
  case AudioBackend::WINDOWS: winSetVolume(device, static_cast<float>(volume)); return true;
#endif
  default: return false;
  }
}

double AudioBackendImpl::getVolume(const std::string &device) {
  if (useCli_) return cliGetVolume(device);
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: {
    uint32_t id = device.empty() ? pwFindDefaultNode(false) : 0;
    if (id == 0) {
      std::lock_guard<std::mutex> lk(pw_mutex);
      for (const auto &[nid, n] : pw_nodes) {
        if (n.name == device) { id = nid; break; }
      }
    }
    return id != 0 ? pwGetNodeVolume(id) : cliGetVolume(device);
  }
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: {
    std::string dev = device.empty() ? defaultOutputDevice : device;
    if (dev.empty()) dev = "@DEFAULT_SINK@";
    return paGetVolume(dev);
  }
#endif
#ifdef HAVE_ALSA
  case AudioBackend::ALSA: return alsaGetVolume(device);
#endif
#ifdef _WIN32
  case AudioBackend::WINDOWS: return winGetVolume(device);
#endif
  default: return 1.0;
  }
}

bool AudioBackendImpl::setMute(const std::string &device, bool muted) {
  if (useCli_) return cliSetMute(device, muted);
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: {
    uint32_t id = device.empty() ? pwFindDefaultNode(false) : 0;
    if (id == 0) {
      std::lock_guard<std::mutex> lk(pw_mutex);
      for (const auto &[nid, n] : pw_nodes) {
        if (n.name == device) { id = nid; break; }
      }
    }
    if (id != 0) return pwSetNodeMute(id, muted);
    return cliSetMute(device, muted);
  }
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: {
    std::string dev = device.empty() ? defaultOutputDevice : device;
    if (dev.empty()) dev = "@DEFAULT_SINK@";
    paSetMute(dev, muted);
    return true;
  }
#endif
#ifdef HAVE_ALSA
  case AudioBackend::ALSA: return alsaSetMute(device, muted);
#endif
#ifdef _WIN32
  case AudioBackend::WINDOWS: winSetMute(device, muted); return true;
#endif
  default: return false;
  }
}

bool AudioBackendImpl::isMuted(const std::string &device) {
  if (useCli_) return cliIsMuted(device);
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: {
    uint32_t id = device.empty() ? pwFindDefaultNode(false) : 0;
    if (id == 0) {
      std::lock_guard<std::mutex> lk(pw_mutex);
      for (const auto &[nid, n] : pw_nodes) {
        if (n.name == device) { id = nid; break; }
      }
    }
    return id != 0 ? pwGetNodeMuted(id) : cliIsMuted(device);
  }
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: {
    std::string dev = device.empty() ? defaultOutputDevice : device;
    if (dev.empty()) dev = "@DEFAULT_SINK@";
    return paGetMuted(dev);
  }
#endif
#ifdef HAVE_ALSA
  case AudioBackend::ALSA: return alsaGetMuted(device);
#endif
#ifdef _WIN32
  case AudioBackend::WINDOWS: return winGetMuted(device);
#endif
  default: return false;
  }
}

void AudioBackendImpl::updateDeviceCache(std::vector<AudioDevice> &cache) const {
  std::lock_guard<std::mutex> lk(deviceMutex);
  cache.clear();
  if (useCli_) {
    auto outputs = const_cast<AudioBackendImpl *>(this)->cliGetDevices(false);
    auto inputs = const_cast<AudioBackendImpl *>(this)->cliGetDevices(true);
    cache = outputs;
    cache.insert(cache.end(), inputs.begin(), inputs.end());
    return;
  }
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: {
    auto outputs = const_cast<AudioBackendImpl *>(this)->pwGetDevices(false);
    auto inputs = const_cast<AudioBackendImpl *>(this)->pwGetDevices(true);
    cache = outputs;
    cache.insert(cache.end(), inputs.begin(), inputs.end());
    return;
  }
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: {
    auto outputs = const_cast<AudioBackendImpl *>(this)->paDevices(false);
    auto inputs = const_cast<AudioBackendImpl *>(this)->paDevices(true);
    cache = outputs;
    cache.insert(cache.end(), inputs.begin(), inputs.end());
    return;
  }
#endif
#ifdef HAVE_ALSA
  case AudioBackend::ALSA: {
    auto outputs = const_cast<AudioBackendImpl *>(this)->alsaGetDevices(false);
    auto inputs = const_cast<AudioBackendImpl *>(this)->alsaGetDevices(true);
    cache = outputs;
    cache.insert(cache.end(), inputs.begin(), inputs.end());
    return;
  }
#endif
#ifdef _WIN32
  case AudioBackend::WINDOWS: {
    cache = const_cast<AudioBackendImpl *>(this)->winGetDevices(false);
    auto inputs = const_cast<AudioBackendImpl *>(this)->winGetDevices(true);
    cache.insert(cache.end(), inputs.begin(), inputs.end());
    return;
  }
#endif
  default: break;
  }
}

std::vector<AudioDevice> AudioBackendImpl::getDevices(bool input) const {
  if (useCli_) return const_cast<AudioBackendImpl *>(this)->cliGetDevices(input);
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: return const_cast<AudioBackendImpl *>(this)->pwGetDevices(input);
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: return const_cast<AudioBackendImpl *>(this)->paDevices(input);
#endif
#ifdef HAVE_ALSA
  case AudioBackend::ALSA: return const_cast<AudioBackendImpl *>(this)->alsaGetDevices(input);
#endif
#ifdef _WIN32
  case AudioBackend::WINDOWS: return const_cast<AudioBackendImpl *>(this)->winGetDevices(input);
#endif
  default: return {};
  }
}

std::string AudioBackendImpl::getDefaultOutput() const {
  if (useCli_) return cliGetDefaultOutput();
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: return pwGetDefault(false);
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: return paGetDefault(false);
#endif
#ifdef HAVE_ALSA
  case AudioBackend::ALSA: return alsa_default_playback ? alsa_default_playback->name : "default";
#endif
#ifdef _WIN32
  case AudioBackend::WINDOWS: return winGetDefault(false);
#endif
  default: return "default";
  }
}

std::string AudioBackendImpl::getDefaultInput() const {
  if (useCli_) return cliGetDefaultInput();
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: return pwGetDefault(true);
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: return paGetDefault(true);
#endif
#ifdef HAVE_ALSA
  case AudioBackend::ALSA: return alsa_default_capture ? alsa_default_capture->name : "default";
#endif
#ifdef _WIN32
  case AudioBackend::WINDOWS: return winGetDefault(true);
#endif
  default: return "default";
  }
}

bool AudioBackendImpl::setApplicationVolume(const std::string &appName, double vol) {
  vol = std::clamp(vol, 0.0, 1.5);
  if (useCli_) return cliSetApplicationVolume(appName, vol);
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: return pwSetAppVolume(appName, vol);
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: return paSetAppVolume(appName, vol);
#endif
  case AudioBackend::ALSA: return false;
#ifdef _WIN32
  case AudioBackend::WINDOWS: return winSetAppVolume(appName, vol);
#endif
  default: return false;
  }
}

bool AudioBackendImpl::setApplicationVolume(uint32_t, double) { return false; }
double AudioBackendImpl::getApplicationVolume(const std::string &appName) const {
  if (useCli_) return cliGetApplicationVolume(appName);
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: return pwGetAppVolume(appName);
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: return paGetAppVolume(appName);
#endif
  case AudioBackend::ALSA: return 1.0;
#ifdef _WIN32
  case AudioBackend::WINDOWS: return winGetAppVolume(appName);
#endif
  default: return 1.0;
  }
}
double AudioBackendImpl::getApplicationVolume(uint32_t) const { return 1.0; }

std::vector<AudioManager::ApplicationInfo> AudioBackendImpl::getApplications() const {
  if (useCli_) return cliGetApplications();
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
  case AudioBackend::PIPEWIRE: return pwGetApplications();
#endif
#ifdef HAVE_PULSEAUDIO
  case AudioBackend::PULSE: return paGetApplications();
#endif
  case AudioBackend::ALSA: return {};
#ifdef _WIN32
  case AudioBackend::WINDOWS: return winGetApplications();
#endif
  default: return {};
  }
}

std::string AudioBackendImpl::getActiveApplicationName() const {
#ifdef HAVE_PULSEAUDIO
  if (activeBackend == AudioBackend::PULSE && !useCli_) return paGetActiveApplicationName();
#endif
  auto apps = getApplications();
  return apps.empty() ? "" : apps[0].name;
}

bool AudioBackendImpl::playTestSound() {
  if (useCli_) {
#ifdef __linux__
    return system("speaker-test -c 2 -l 1 -t sine -f 440 2>/dev/null") == 0;
#elif _WIN32
    return system("powershell -c \"[Console]::Beep(440,500)\" 2>nul") == 0;
#endif
  }
  return false;
}

bool AudioBackendImpl::playSound(const std::string &soundFile) {
#ifdef __linux__
  auto r1 = havel::utils::execSync({"pw-play", soundFile});
  if (r1 && r1->exitCode == 0) return true;
  auto r2 = havel::utils::execSync({"paplay", soundFile});
  if (r2 && r2->exitCode == 0) return true;
  auto r3 = havel::utils::execSync({"aplay", soundFile});
  return r3 && r3->exitCode == 0;
#elif _WIN32
  return system(("powershell -c \"(New-Object System.Media.SoundPlayer '" + soundFile + "').PlaySync()\" 2>nul").c_str()) == 0;
#else
  return false;
#endif
}

IAudioBackend *CreateAudioBackend(AudioBackend preferred) {
  if (preferred == AudioBackend::AUTO) {
    auto *backend = new AudioBackendImpl(AudioBackendMode::Automatic);
    if (backend->initialize()) return backend;
    delete backend;
    // Fallback to CLI
    backend = new AudioBackendImpl(AudioBackendMode::CLI);
    if (backend->initialize()) return backend;
    delete backend;
    return nullptr;
  }
  auto *backend = new AudioBackendImpl();
  backend->activeBackend = preferred;
  if (backend->initialize()) return backend;
  delete backend;
  return nullptr;
}

} // namespace havel
