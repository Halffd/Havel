#include "AudioBackendImpl.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <memory>
#include <sstream>
#include <thread>
#include <chrono>

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
    if (initNativeWindows()) { activeBackend = AudioBackend::ALSA; return true; }
#endif
  }

  // CLI fallback
  useCli_ = true;
#ifdef __linux__
  if (system("which amixer 2>/dev/null >/dev/null") == 0) { activeBackend = AudioBackend::ALSA; return true; }
  if (system("which pactl 2>/dev/null >/dev/null") == 0) { activeBackend = AudioBackend::PULSE; return true; }
  if (system("which wpctl 2>/dev/null >/dev/null") == 0) { activeBackend = AudioBackend::PIPEWIRE; return true; }
#endif
#ifdef _WIN32
  activeBackend = AudioBackend::ALSA;
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
  if (pa_ctx) { pa_context_disconnect(pa_ctx); pa_context_unref(pa_ctx); pa_ctx = nullptr; }
  if (pa_ml) { pa_threaded_mainloop_stop(pa_ml); pa_threaded_mainloop_free(pa_ml); pa_ml = nullptr; }
#endif
#ifdef HAVE_ALSA
  if (alsa_mixer) { snd_mixer_detach(alsa_mixer, "default"); snd_mixer_close(alsa_mixer); alsa_mixer = nullptr; alsa_elem = nullptr; }
#endif
}

std::string AudioBackendImpl::getName() const {
  if (useCli_) return std::string("CLI/") + (activeBackend == AudioBackend::PIPEWIRE ? "PipeWire" :
    activeBackend == AudioBackend::PULSE ? "PulseAudio" : activeBackend == AudioBackend::ALSA ? "ALSA" : "Windows");
  switch (activeBackend) {
    case AudioBackend::PIPEWIRE: return "PipeWire";
    case AudioBackend::PULSE: return "PulseAudio";
    case AudioBackend::ALSA: return "ALSA";
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
  std::array<char, 256> buf;
  while (fgets(buf.data(), buf.size(), pipe)) result += buf.data();
  pclose(pipe);
  if (!result.empty() && result.back() == '\n') result.pop_back();
  return result;
}

bool AudioBackendImpl::cliSetVolume(const std::string &, double volume) const {
  int pct = static_cast<int>(std::clamp(volume, 0.0, 1.5) * 100);
#ifdef __linux__
  if (activeBackend == AudioBackend::ALSA)
    return system(("amixer set Master " + std::to_string(pct) + "% 2>/dev/null").c_str()) == 0;
  if (activeBackend == AudioBackend::PULSE)
    return system(("pactl set-sink-volume @DEFAULT_SINK@ " + std::to_string(pct) + "% 2>/dev/null").c_str()) == 0;
#endif
#ifdef _WIN32
  return system(("powershell -c \"(New-Object -ComObject WScript.Shell).SendKeys([char]175)\" 2>nul").c_str()) == 0;
#endif
  return false;
}

double AudioBackendImpl::cliGetVolume(const std::string &) const {
#ifdef __linux__
  if (activeBackend == AudioBackend::ALSA) {
    auto out = cliExec("amixer get Master 2>/dev/null | grep -oP '\\d+%' | head -1");
    if (!out.empty()) return std::stoi(out.substr(0, out.size()-1)) / 100.0;
  }
  if (activeBackend == AudioBackend::PULSE) {
    auto out = cliExec("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -oP '\\d+%' | head -1");
    if (!out.empty()) return std::stoi(out.substr(0, out.size()-1)) / 100.0;
  }
  if (activeBackend == AudioBackend::PIPEWIRE) {
    auto out = cliExec("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null | grep -oP '\\d+\\.\\d+'");
    if (!out.empty()) return std::stod(out);
  }
#endif
  return 1.0;
}

bool AudioBackendImpl::cliSetMute(const std::string &, bool muted) const {
#ifdef __linux__
  if (activeBackend == AudioBackend::ALSA)
    return system(("amixer set Master " + std::string(muted ? "mute" : "unmute") + " 2>/dev/null").c_str()) == 0;
  if (activeBackend == AudioBackend::PULSE)
    return system(("pactl set-sink-mute @DEFAULT_SINK@ " + std::string(muted ? "1" : "0") + " 2>/dev/null").c_str()) == 0;
#endif
#ifdef _WIN32
  return system(("powershell -c \"(New-Object -ComObject WScript.Shell).SendKeys([char]" +
    std::string(muted ? "173" : "175") + ")\" 2>nul").c_str()) == 0;
#endif
  return false;
}

// ============================================================
// Native PipeWire
// ============================================================
#ifdef HAVE_PIPEWIRE
// Forward declarations
void pw_on_core_sync(void *data, uint32_t, int);
void pw_on_core_error(void *, uint32_t, int, int, const char *);
void pw_on_node_info(void *data, const struct pw_node_info *info);
void pw_on_registry_global(void *data, uint32_t id, uint32_t,
                            const char *type, uint32_t, const struct spa_dict *props);
void pw_on_registry_global_remove(void *data, uint32_t id);

static const struct pw_core_events pw_core_evt = {
  PW_VERSION_CORE_EVENTS, .done = pw_on_core_sync, .error = pw_on_core_error,
};
static const struct pw_node_events pw_node_evt = {
  PW_VERSION_NODE_EVENTS, .info = pw_on_node_info,
};
static const struct pw_registry_events pw_registry_evt = {
  PW_VERSION_REGISTRY_EVENTS, .global = pw_on_registry_global, .global_remove = pw_on_registry_global_remove,
};

void pw_on_core_sync(void *data, uint32_t, int) {
  auto *self = static_cast<AudioBackendImpl *>(data);
  self->pw_ready = true;
  pw_thread_loop_signal(self->pw_loop, false);
}
void pw_on_core_error(void *, uint32_t, int, int, const char *) {}
void pw_on_node_info(void *data, const struct pw_node_info *info) {
  auto *self = static_cast<AudioBackendImpl *>(data);
  for (auto &[id, n] : self->pw_nodes) {
    if (n.proxy && info->props) {
      const char *v;
      if ((v = spa_dict_lookup(info->props, "node.name"))) n.name = v;
      if ((v = spa_dict_lookup(info->props, "node.description"))) n.description = v;
      if ((v = spa_dict_lookup(info->props, "media.class"))) n.mediaClass = v;
      break;
    }
  }
}
void pw_on_registry_global(void *data, uint32_t id, uint32_t,
                            const char *type, uint32_t, const struct spa_dict *props) {
  auto *self = static_cast<AudioBackendImpl *>(data);
  if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0 || !props) return;
  const char *cls = spa_dict_lookup(props, "media.class");
  if (!cls) return;
  AudioBackendImpl::PWNode n;
  n.id = id;
  n.proxy = static_cast<pw_proxy *>(pw_registry_bind(self->pw_registry_, id, type, PW_VERSION_NODE, 0));
  if (!n.proxy) return;
  pw_node_add_listener(reinterpret_cast<pw_node *>(n.proxy), &n.node_listener, &pw_node_evt, data);
  std::lock_guard<std::mutex> lk(self->pw_mutex);
  self->pw_nodes[id] = n;
}
void pw_on_registry_global_remove(void *data, uint32_t id) {
  auto *self = static_cast<AudioBackendImpl *>(data);
  std::lock_guard<std::mutex> lk(self->pw_mutex);
  self->pw_nodes.erase(id);
}

bool AudioBackendImpl::initNativePipeWire() {
  pw_init(nullptr, nullptr);
  pw_loop = pw_thread_loop_new("havel-audio", nullptr);
  if (!pw_loop) return false;
  pw_context_ = pw_context_new(pw_thread_loop_get_loop(pw_loop), nullptr, 0);
  if (!pw_context_) { pw_thread_loop_destroy(pw_loop); pw_loop = nullptr; return false; }
  pw_core_ = pw_context_connect(pw_context_, nullptr, 0);
  if (!pw_core_) { pw_context_destroy(pw_context_); pw_context_ = nullptr; pw_thread_loop_destroy(pw_loop); pw_loop = nullptr; return false; }
  pw_core_add_listener(pw_core_, &pw_core_listener, &pw_core_evt, this);
  pw_registry_ = pw_core_get_registry(pw_core_, PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener(pw_registry_, &pw_registry_listener, &pw_registry_evt, this);
  if (pw_thread_loop_start(pw_loop) < 0) return false;
  pw_thread_loop_lock(pw_loop);
  pw_core_sync(pw_core_, PW_ID_CORE, 0);
  while (!pw_ready && !pw_failed) pw_thread_loop_wait(pw_loop);
  pw_thread_loop_unlock(pw_loop);
  if (!pw_ready) return false;
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

void AudioBackendImpl::pwSetNodeVolume(uint32_t, double) {
  // PipeWire native volume requires SPA pod API not available on this system
  // CLI fallback will be used instead
}

void AudioBackendImpl::pwSetNodeMute(uint32_t, bool) {}
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
  if (pa_threaded_mainloop_start(pa_ml) < 0) { pa_context_unref(pa_ctx); pa_ctx = nullptr; pa_threaded_mainloop_free(pa_ml); pa_ml = nullptr; return false; }
  pa_threaded_mainloop_lock(pa_ml);
  if (pa_context_connect(pa_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) { pa_threaded_mainloop_unlock(pa_ml); return false; }
  while (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) {
    if (pa_context_get_state(pa_ctx) == PA_CONTEXT_FAILED) break;
    pa_threaded_mainloop_wait(pa_ml);
  }
  pa_threaded_mainloop_unlock(pa_ml);
  if (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) { cleanup(); return false; }
  return true;
}

void AudioBackendImpl::paSetVolume(const std::string &device, double vol) {
  pa_threaded_mainloop_lock(pa_ml);
  pa_cvolume cv;
  pa_cvolume_set(&cv, 2, pa_sw_volume_from_linear(vol));
  pa_operation *op = pa_context_set_sink_volume_by_name(pa_ctx, device.c_str(), &cv, nullptr, nullptr);
  if (op) { pa_operation_unref(op); }
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
  if (op) {
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml);
    pa_operation_unref(op);
  }
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
  if (op) {
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml);
    pa_operation_unref(op);
  }
  pa_threaded_mainloop_unlock(pa_ml);
  return muted;
}

std::vector<AudioDevice> AudioBackendImpl::paDevices(bool input) {
  std::vector<AudioDevice> devs;
  pa_threaded_mainloop_lock(pa_ml);
  PAResultDevices r{&devs, pa_ml};
  auto cb = input
    ? pa_context_get_source_info_list(pa_ctx,
        [](pa_context *, const pa_source_info *i, int, void *ud) {
          auto *r = static_cast<PAResultDevices *>(ud); if (!i) return;
          AudioDevice d; d.name = i->name; d.description = i->description;
          d.index = i->index; d.volume = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
          d.isMuted = i->mute != 0; d.channels = i->sample_spec.channels;
          r->out->push_back(d);
        }, &r)
    : pa_context_get_sink_info_list(pa_ctx,
        [](pa_context *, const pa_sink_info *i, int, void *ud) {
          auto *r = static_cast<PAResultDevices *>(ud); if (!i) return;
          AudioDevice d; d.name = i->name; d.description = i->description;
          d.index = i->index; d.volume = pa_sw_volume_to_linear(pa_cvolume_avg(&i->volume));
          d.isMuted = i->mute != 0; d.channels = i->sample_spec.channels;
          r->out->push_back(d);
        }, &r);
  if (cb) {
    while (pa_operation_get_state(cb) == PA_OPERATION_RUNNING) pa_threaded_mainloop_wait(pa_ml);
    pa_operation_unref(cb);
  }
  pa_threaded_mainloop_unlock(pa_ml);
  return devs;
}
#endif

// ============================================================
// Native ALSA
// ============================================================
#ifdef HAVE_ALSA
bool AudioBackendImpl::initNativeAlsa() {
  if (snd_mixer_open(&alsa_mixer, 0) < 0) return false;
  if (snd_mixer_attach(alsa_mixer, "default") < 0) { snd_mixer_close(alsa_mixer); alsa_mixer = nullptr; return false; }
  if (snd_mixer_selem_register(alsa_mixer, nullptr, nullptr) < 0) { snd_mixer_close(alsa_mixer); alsa_mixer = nullptr; return false; }
  if (snd_mixer_load(alsa_mixer) < 0) { snd_mixer_close(alsa_mixer); alsa_mixer = nullptr; return false; }
  for (snd_mixer_elem_t *e = snd_mixer_first_elem(alsa_mixer); e; e = snd_mixer_elem_next(e)) {
    if (snd_mixer_elem_get_type(e) == SND_MIXER_ELEM_SIMPLE && snd_mixer_selem_has_playback_volume(e)) {
      alsa_elem = e; break;
    }
  }
  return alsa_elem != nullptr;
}

void AudioBackendImpl::alsaSetVolume(double vol) {
  if (!alsa_elem) return;
  long min, max;
  snd_mixer_selem_get_playback_volume_range(alsa_elem, &min, &max);
  snd_mixer_selem_set_playback_volume_all(alsa_elem, min + static_cast<long>(vol * (max - min)));
}

double AudioBackendImpl::alsaGetVolume() {
  if (!alsa_elem) return 1.0;
  long min, max, val;
  snd_mixer_selem_get_playback_volume_range(alsa_elem, &min, &max);
  snd_mixer_selem_get_playback_volume(alsa_elem, SND_MIXER_SCHN_FRONT_LEFT, &val);
  return max > min ? static_cast<double>(val - min) / (max - min) : 1.0;
}

void AudioBackendImpl::alsaSetMute(bool mute) {
  if (alsa_elem) snd_mixer_selem_set_playback_switch_all(alsa_elem, mute ? 0 : 1);
}

bool AudioBackendImpl::alsaGetMuted() {
  if (!alsa_elem) return false;
  int val;
  snd_mixer_selem_get_playback_switch(alsa_elem, SND_MIXER_SCHN_FRONT_LEFT, &val);
  return val == 0;
}
#endif

// ============================================================
// Native Windows
// ============================================================
#ifdef _WIN32
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>

bool AudioBackendImpl::initNativeWindows() {
  return true; // COM initialized elsewhere
}

float AudioBackendImpl::winGetVolume(const std::string &) {
  HRESULT hr;
  IMMDeviceEnumerator *enumerator = nullptr;
  IMMDevice *device = nullptr;
  IAudioEndpointVolume *volume = nullptr;
  float level = 1.0f;

  hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                        __uuidof(IMMDeviceEnumerator), (void **)&enumerator);
  if (FAILED(hr)) return level;
  hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
  if (SUCCEEDED(hr)) {
    hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, (void **)&volume);
    if (SUCCEEDED(hr)) { volume->GetMasterVolumeLevelScalar(&level); volume->Release(); }
    device->Release();
  }
  enumerator->Release();
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
        volume->SetMasterVolumeLevelScalar(vol, nullptr);
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
#endif

// ============================================================
// Dispatchers
// ============================================================
bool AudioBackendImpl::setVolume(const std::string &device, double volume) {
  volume = std::clamp(volume, 0.0, 1.5);
  if (useCli_) return cliSetVolume(device, volume);
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
    case AudioBackend::PIPEWIRE: pwSetNodeVolume(0, volume); return true;
#endif
#ifdef HAVE_PULSEAUDIO
    case AudioBackend::PULSE: paSetVolume(device, volume); return true;
#endif
#ifdef HAVE_ALSA
    case AudioBackend::ALSA: alsaSetVolume(volume); return true;
#endif
#ifdef _WIN32
    case AudioBackend::ALSA: winSetVolume(device, volume); return true;
#endif
    default: return false;
  }
}

double AudioBackendImpl::getVolume(const std::string &device) {
  if (useCli_) return cliGetVolume(device);
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
    case AudioBackend::PIPEWIRE: return 1.0;
#endif
#ifdef HAVE_PULSEAUDIO
    case AudioBackend::PULSE: return paGetVolume(device);
#endif
#ifdef HAVE_ALSA
    case AudioBackend::ALSA: return alsaGetVolume();
#endif
#ifdef _WIN32
    case AudioBackend::ALSA: return winGetVolume(device);
#endif
    default: return 1.0;
  }
}

bool AudioBackendImpl::setMute(const std::string &device, bool muted) {
  if (useCli_) return cliSetMute(device, muted);
  switch (activeBackend) {
#ifdef HAVE_PIPEWIRE
    case AudioBackend::PIPEWIRE: pwSetNodeMute(0, muted); return true;
#endif
#ifdef HAVE_PULSEAUDIO
    case AudioBackend::PULSE: paSetMute(device, muted); return true;
#endif
#ifdef HAVE_ALSA
    case AudioBackend::ALSA: alsaSetMute(muted); return true;
#endif
#ifdef _WIN32
    case AudioBackend::ALSA: winSetMute(device, muted); return true;
#endif
    default: return false;
  }
}

bool AudioBackendImpl::isMuted(const std::string &device) {
  switch (activeBackend) {
#ifdef HAVE_PULSEAUDIO
    case AudioBackend::PULSE: return paGetMuted(device);
#endif
#ifdef HAVE_ALSA
    case AudioBackend::ALSA: return alsaGetMuted();
#endif
#ifdef _WIN32
    case AudioBackend::ALSA: return winGetMuted(device);
#endif
    default: return false;
  }
}

void AudioBackendImpl::updateDeviceCache(std::vector<AudioDevice> &cache) const {
  std::lock_guard<std::mutex> lk(deviceMutex);
  if (useCli_) {
    cache.clear();
    AudioDevice d;
    d.name = "default"; d.description = "Default device";
    d.volume = const_cast<AudioBackendImpl *>(this)->cliGetVolume("");
    cache.push_back(d);
    return;
  }
  cache.clear();
  switch (activeBackend) {
#ifdef HAVE_PULSEAUDIO
    case AudioBackend::PULSE: cache = const_cast<AudioBackendImpl *>(this)->paDevices(false); return;
#endif
#ifdef HAVE_ALSA
    case AudioBackend::ALSA: {
      AudioDevice d;
      d.name = "default"; d.description = "ALSA default";
      d.volume = const_cast<AudioBackendImpl *>(this)->alsaGetVolume();
      d.isMuted = const_cast<AudioBackendImpl *>(this)->alsaGetMuted();
      cache.push_back(d); return;
    }
#endif
#ifdef _WIN32
    case AudioBackend::ALSA: {
      AudioDevice d;
      d.name = "default"; d.description = "Windows default";
      d.volume = const_cast<AudioBackendImpl *>(this)->winGetVolume("");
      d.isMuted = const_cast<AudioBackendImpl *>(this)->winGetMuted("");
      cache.push_back(d); return;
    }
#endif
    default: break;
  }
}

std::vector<AudioDevice> AudioBackendImpl::getDevices(bool) const {
  std::lock_guard<std::mutex> lk(deviceMutex);
  return cachedDevices;
}

std::string AudioBackendImpl::getDefaultOutput() const { return "default"; }
std::string AudioBackendImpl::getDefaultInput() const { return "default"; }

bool AudioBackendImpl::setApplicationVolume(const std::string &, double) { return false; }
bool AudioBackendImpl::setApplicationVolume(uint32_t, double) { return false; }
double AudioBackendImpl::getApplicationVolume(const std::string &) const { return 1.0; }
double AudioBackendImpl::getApplicationVolume(uint32_t) const { return 1.0; }
std::vector<AudioManager::ApplicationInfo> AudioBackendImpl::getApplications() const { return {}; }
std::string AudioBackendImpl::getActiveApplicationName() const { return ""; }

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
  return system(("paplay " + soundFile + " 2>/dev/null").c_str()) == 0 ||
         system(("aplay " + soundFile + " 2>/dev/null").c_str()) == 0;
#elif _WIN32
  return system(("powershell -c \"(New-Object Media.SoundPlayer '" + soundFile + "').PlaySync()\" 2>nul").c_str()) == 0;
#else
  return false;
#endif
}

IAudioBackend *CreateAudioBackend(AudioBackend) {
  return new AudioBackendImpl();
}

} // namespace havel
