#pragma once
#include "IAudioBackend.hpp"
#include <atomic>
#include <condition_variable>
#include <future>
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

  // Public for friend callbacks
  AudioBackend activeBackend = AudioBackend::ALSA;
  AudioBackendMode backendMode = AudioBackendMode::Automatic;
  mutable std::vector<AudioDevice> cachedDevices;
  mutable std::mutex deviceMutex;
  std::string defaultOutputDevice, defaultInputDevice;

private:
  bool useCli_ = false;

  // Native backend init
  bool initNativePipeWire();
  bool initNativePulseAudio();
  bool initNativeAlsa();
  bool initNativeWindows();

  // CLI helpers
  std::string cliExec(const std::string &cmd) const;
  double cliGetVolume(const std::string &device) const;
  bool cliSetVolume(const std::string &device, double volume) const;
  bool cliSetMute(const std::string &device, bool muted) const;

  // Native PipeWire
#ifdef HAVE_PIPEWIRE
  friend void pw_on_core_sync(void *data, uint32_t id, int seq);
  friend void pw_on_core_error(void *data, uint32_t id, int seq, int res, const char *msg);
  friend void pw_on_node_info(void *data, const struct pw_node_info *info);
  friend void pw_on_registry_global(void *data, uint32_t id, uint32_t permissions,
                                     const char *type, uint32_t version, const struct spa_dict *props);
  friend void pw_on_registry_global_remove(void *data, uint32_t id);

  struct PWNode {
    uint32_t id;
    std::string name, mediaClass, description;
    double volume = 1.0; bool isMuted = false;
    pw_proxy *proxy = nullptr;
    spa_hook node_listener;
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

  void pwStart();
  void pwStop();
  void pwProcess();
  void pwSetNodeVolume(uint32_t id, double vol);
  void pwSetNodeMute(uint32_t id, bool mute);
#endif

  // Native PulseAudio
#ifdef HAVE_PULSEAUDIO
  pa_threaded_mainloop *pa_ml = nullptr;
  pa_context *pa_ctx = nullptr;

  bool paInit();
  void paSetVolume(const std::string &device, double vol);
  double paGetVolume(const std::string &device);
  void paSetMute(const std::string &device, bool mute);
  bool paGetMuted(const std::string &device);
  std::vector<AudioDevice> paDevices(bool input);
#endif

  // Native ALSA
#ifdef HAVE_ALSA
  snd_mixer_t *alsa_mixer = nullptr;
  snd_mixer_elem_t *alsa_elem = nullptr;

  bool alsaInit();
  void alsaSetVolume(double vol);
  double alsaGetVolume();
  void alsaSetMute(bool mute);
  bool alsaGetMuted();
#endif

  // Windows native
#ifdef _WIN32
  bool winInit();
  float winGetVolume(const std::string &device);
  void winSetVolume(const std::string &device, float vol);
  bool winGetMuted(const std::string &device);
  void winSetMute(const std::string &device, bool mute);
#endif
};

IAudioBackend *CreateAudioBackend(AudioBackend preferred);

} // namespace havel
