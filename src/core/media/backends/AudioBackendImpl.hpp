#pragma once
#include "IAudioBackend.hpp"
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <queue>
#include <thread>

#ifdef HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <pipewire/core.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/utils/keys.h>
#include <spa/utils/result.h>
#include <spa/utils/string.h>
#endif

#ifdef HAVE_PULSEAUDIO
#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>
#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/volume.h>
#include <pulse/error.h>
#include <pulse/stream.h>
#include <pulse/subscribe.h>
#endif

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#include <alsa/mixer.h>
#include <alsa/control.h>
#endif

namespace havel {

enum class AudioBackendMode { Automatic, Native, CLI };

struct PAResultDouble { double *out; void *ml; };
struct PAResultBool { bool *out; void *ml; };
struct PAResultString { std::string *out; void *ml; };
struct PAResultDevices { std::vector<AudioDevice> *out; void *ml; };
struct PAResultApps { std::vector<AudioManager::ApplicationInfo> *out; void *ml; };

class AudioBackendImpl : public IAudioBackend {
public:
  AudioBackendImpl(AudioBackendMode mode = AudioBackendMode::Automatic);
  ~AudioBackendImpl() override;

  bool initialize() override;
  void cleanup() override;
  AudioBackend getType() const override { return activeBackend; }
  std::string getName() const override;

  bool setVolume(const std::string &device, double volume) override;
  double getVolume(const std::string &device) override;
  bool setMute(const std::string &device, bool muted) override;
  bool isMuted(const std::string &device) override;

  std::vector<AudioDevice> getDevices(bool input) const override;
  void updateDeviceCache(std::vector<AudioDevice> &cache) const override;
  std::string getDefaultOutput() const override;
  std::string getDefaultInput() const override;

  bool setApplicationVolume(const std::string &appName, double volume) override;
  bool setApplicationVolume(uint32_t appIndex, double volume) override;
  double getApplicationVolume(const std::string &appName) const override;
  double getApplicationVolume(uint32_t appIndex) const override;
  std::vector<AudioManager::ApplicationInfo> getApplications() const override;
  std::string getActiveApplicationName() const override;

  bool playTestSound() override;
  bool playSound(const std::string &soundFile) override;

  AudioBackend activeBackend = AudioBackend::ALSA;
  AudioBackendMode backendMode = AudioBackendMode::Automatic;
  mutable std::vector<AudioDevice> cachedDevices;
  mutable std::mutex deviceMutex;
  std::string defaultOutputDevice, defaultInputDevice;

  // === PipeWire native ===
#ifdef HAVE_PIPEWIRE
  struct PWNode {
    uint32_t id;
    std::string name, mediaClass, description;
    double volume = 1.0;
    bool isMuted = false;
    int nVolumeChannels = 0;
    float channelVolumes[SPA_AUDIO_MAX_CHANNELS] = {};
    pw_proxy *proxy = nullptr;
  spa_hook node_listener;
#if PW_MAJOR_VERSION > 0 || (PW_MAJOR_VERSION == 0 && PW_MINOR_VERSION >= 3 && PW_MICRO_VERSION >= 82)
#else
  pw_node_events node_events{};
#endif
  };

  pw_thread_loop *pw_loop = nullptr;
  pw_context *pw_context_ = nullptr;
  pw_core *pw_core_ = nullptr;
  pw_registry *pw_registry_ = nullptr;
  spa_hook pw_core_listener, pw_registry_listener;
  std::map<uint32_t, PWNode> pw_nodes;
  mutable std::mutex pw_mutex;
  bool pw_ready = false, pw_initialized = false, pw_failed = false;
  std::thread pw_cmd_thread;
  std::mutex pw_cmd_mutex;
  std::condition_variable pw_cmd_cv;
  std::queue<std::function<void()>> pw_cmds;
  bool pw_running = false;
  int pw_sync_seq = 0;

  void pwStart();
  void pwStop();
  void pwProcess();
  void pwSyncWait();
  uint32_t pwFindDefaultNode(bool input) const;
  bool pwSetNodeVolume(uint32_t id, double vol);
  bool pwSetNodeMute(uint32_t id, bool mute);
  double pwGetNodeVolume(uint32_t id) const;
  bool pwGetNodeMuted(uint32_t id) const;
  std::vector<AudioDevice> pwGetDevices(bool input) const;
  std::string pwGetDefault(bool input) const;
  bool pwSetAppVolume(const std::string &appName, double vol);
  double pwGetAppVolume(const std::string &appName) const;
  std::vector<AudioManager::ApplicationInfo> pwGetApplications() const;
#endif

  // === PulseAudio native ===
#ifdef HAVE_PULSEAUDIO
  pa_threaded_mainloop *pa_ml = nullptr;
  pa_context *pa_ctx = nullptr;
  pa_subscription_mask_t pa_sub_mask = PA_SUBSCRIPTION_MASK_NULL;

  bool paInit();
  void paCleanup();
  void paSetVolume(const std::string &device, double vol);
  double paGetVolume(const std::string &device);
  void paSetMute(const std::string &device, bool mute);
  bool paGetMuted(const std::string &device);
  std::vector<AudioDevice> paDevices(bool input);
  std::string paGetDefault(bool input) const;
  bool paSetAppVolume(const std::string &appName, double vol);
  double paGetAppVolume(const std::string &appName) const;
  std::vector<AudioManager::ApplicationInfo> paGetApplications() const;
  std::string paGetActiveApplicationName() const;
#endif

  // === ALSA native ===
#ifdef HAVE_ALSA
  struct AlsaMixer {
    std::string name;
    snd_mixer_t *handle = nullptr;
    snd_mixer_elem_t *elem = nullptr;
    snd_mixer_selem_id_t *sid = nullptr;
    bool hasPlayback = false;
    bool hasCapture = false;
  };

  std::vector<AlsaMixer> alsa_mixers;
  AlsaMixer *alsa_default_playback = nullptr;
  AlsaMixer *alsa_default_capture = nullptr;

  bool alsaInit();
  void alsaCleanup();
  AlsaMixer *findAlsaMixer(const std::string &device, bool capture) const;
  bool alsaSetVolume(const std::string &device, double vol);
  double alsaGetVolume(const std::string &device) const;
  bool alsaSetMute(const std::string &device, bool mute);
  bool alsaGetMuted(const std::string &device) const;
  std::vector<AudioDevice> alsaGetDevices(bool input) const;
#endif

  // === Windows native ===
#ifdef _WIN32
  bool winInit();
  void winCleanup();
  float winGetVolume(const std::string &device);
  void winSetVolume(const std::string &device, float vol);
  bool winGetMuted(const std::string &device);
  void winSetMute(const std::string &device, bool mute);
  std::vector<AudioDevice> winGetDevices(bool input) const;
  std::string winGetDefault(bool input) const;
  bool winSetAppVolume(const std::string &appName, double vol);
  double winGetAppVolume(const std::string &appName) const;
  std::vector<AudioManager::ApplicationInfo> winGetApplications() const;
#endif

private:
  bool useCli_ = false;

  bool initNativePipeWire();
  bool initNativePulseAudio();
  bool initNativeAlsa();
  bool initNativeWindows();

  std::string cliExec(const std::string &cmd) const;
  double cliGetVolume(const std::string &device) const;
  bool cliSetVolume(const std::string &device, double volume) const;
  bool cliSetMute(const std::string &device, bool muted) const;
  bool cliIsMuted(const std::string &device) const;
  std::vector<AudioDevice> cliGetDevices(bool input) const;
  std::string cliGetDefaultOutput() const;
  std::string cliGetDefaultInput() const;
  bool cliSetApplicationVolume(const std::string &appName, double volume) const;
  double cliGetApplicationVolume(const std::string &appName) const;
  std::vector<AudioManager::ApplicationInfo> cliGetApplications() const;
};

IAudioBackend *CreateAudioBackend(AudioBackend preferred);

} // namespace havel
