#include "BrightnessManager.hpp"
#include "../utils/Logger.hpp"
#include "ConfigManager.hpp"
#include "window/WindowManagerDetector.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <algorithm>
#include <cmath>
#include <qguiapplication_platform.h>
#ifdef __WAYLAND__
#include <wayland-client.h>
#endif
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __WAYLAND__
// Wayland protocols
extern "C" {
#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
}
#endif

#ifdef __WAYLAND__
// Wayland globals
static struct wl_display *wl_display = nullptr;
static struct wl_registry *wl_registry = nullptr;
static struct zwlr_gamma_control_manager_v1 *gamma_control_manager = nullptr;
static struct zxdg_output_manager_v1 *xdg_output_manager = nullptr;
#endif

#ifdef __WAYLAND__
// Wayland output structure
struct WaylandOutput {
  struct wl_output *wl_output = nullptr;
  struct zxdg_output_v1 *xdg_output = nullptr;
  std::string name;
  int32_t width = 0;
  int32_t height = 0;
  bool done = false;
};
#endif

#ifdef __WAYLAND__
// Wayland globals for output tracking
static std::vector<WaylandOutput> wayland_outputs;
static std::mutex wayland_mutex;
#endif

namespace havel {

// === CONSTRUCTOR/DESTRUCTOR ===
BrightnessManager::BrightnessManager() {
  x11_display = DisplayManager::GetDisplay();
  x11_root = DisplayManager::GetRootWindow();
  vector<string> monitors = getConnectedMonitors();
  primaryMonitor = monitors[0];
  for (string monitor : monitors) {
    brightness[monitor] = getBrightness(monitor);
    debug("Brightness for " + monitor + ": " + std::to_string(brightness[monitor]));
    temperature[monitor] = getTemperature(monitor);
    debug("Temperature for " + monitor + ": " + std::to_string(temperature[monitor]));
  }
  // Apply initial settings if auto-adjust is enabled
  if (dayNightSettings.autoAdjust) {
    applyCurrentTimeSettings();
  }
}

BrightnessManager::~BrightnessManager() { disableDayNightMode(); }
string BrightnessManager::getMonitor(int index){
  return getConnectedMonitors()[index];
}

// === WAYLAND INITIALIZATION ===
#ifdef __WAYLAND__
// Wayland registry listener callbacks
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
  if (strcmp(interface, wl_output_interface.name) == 0) {
    WaylandOutput output;
    const uint32_t wl_output_version =
        std::min(version, 3u); // v3 gives you geometry + scale
    output.wl_output = static_cast<wl_output *>(wl_registry_bind(
        registry, name, &wl_output_interface, wl_output_version));
    wayland_outputs.push_back(output);
  } else if (strcmp(interface, zwlr_gamma_control_manager_v1_interface.name) ==
             0) {
    gamma_control_manager =
        static_cast<zwlr_gamma_control_manager_v1 *>(wl_registry_bind(
            registry, name, &zwlr_gamma_control_manager_v1_interface,
            std::min(version, 1u)));

    xdg_output_manager = static_cast<zxdg_output_manager_v1 *>(
        wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface,
                         std::min(version, 2u)));
  } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
    xdg_output_manager = static_cast<zxdg_output_manager_v1 *>(
        wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface,
                         std::min(version, 2u)));
  }
}

static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name) {
  if (strcmp(interface, wl_output_interface.name) == 0) {
    WaylandOutput output;
    output.wl_output = static_cast<wl_output *>(wl_registry_bind(
        registry, name, &wl_output_interface, std::min(version, 3u)));
    {
      std::scoped_lock lock(wayland_mutex);
      wayland_outputs.push_back(std::move(output));
    }
  }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove};

// XDG output listener callbacks
static void xdg_output_handle_logical_position(
    void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y) {}

static void xdg_output_handle_logical_size(void *data,
                                           struct zxdg_output_v1 *xdg_output,
                                           int32_t width, int32_t height) {
  auto *output = static_cast<WaylandOutput *>(data);
  output->width = width;
  output->height = height;
}

static void xdg_output_handle_done(void *data,
                                   struct zxdg_output_v1 *xdg_output) {
  auto *output = static_cast<WaylandOutput *>(data);
  output->done = true;
}

static void xdg_output_handle_name(void *data,
                                   struct zxdg_output_v1 *xdg_output,
                                   const char *name) {
  auto *output = static_cast<WaylandOutput *>(data);
  output->name = name;
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_handle_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = xdg_output_handle_done,
    .name = xdg_output_handle_name,
    .description = nullptr};

void BrightnessManager::initializeWayland() {
  wl_display = wl_display_connect(nullptr);
  if (!wl_display) {
    throw std::runtime_error("Failed to connect to "
                             "Wayland display");
  }

  wl_registry = wl_display_get_registry(wl_display);
  wl_registry_add_listener(wl_registry, &registry_listener, nullptr);

  // Wait for the initial registry
  // events
  wl_display_roundtrip(wl_display);

  if (!gamma_control_manager) {
    wl_display_disconnect(wl_display);
    wl_display = nullptr;
    throw std::runtime_error("Compositor doesn't "
                             "support wlr-gamma-control "
                             "protocol");
  }

  // Set up XDG output for each
  // output
  for (auto &output : wayland_outputs) {
    if (xdg_output_manager) {
      output.xdg_output = zxdg_output_manager_v1_get_xdg_output(
          xdg_output_manager, output.wl_output);
      zxdg_output_v1_add_listener(output.xdg_output, &xdg_output_listener,
                                  &output);
    }
  }

  // Wait for all outputs to be
  // configured
  wl_display_roundtrip(wl_display);

  displayMethod = "wayland";
}
#endif

// === KELVIN TO RGB CONVERSION
// (Tanner Helland algorithm) ===
BrightnessManager::RGBColor BrightnessManager::kelvinToRGB(int kelvin) const {
  kelvin = std::clamp(kelvin, MIN_TEMPERATURE, MAX_TEMPERATURE);
  double temp = kelvin / 100.0;

  double red, green, blue;

  // Calculate Red
  if (temp <= 66) {
    red = 1.0;
  } else {
    red = temp - 60;
    red = 329.698727446 * std::pow(red, -0.1332047592);
    red = std::clamp(red / 255.0, 0.0, 1.0);
  }

  // Calculate Green
  if (temp <= 66) {
    green = temp;
    green = 99.4708025861 * std::log(green) - 161.1195681661;
  } else {
    green = temp - 60;
    green = 288.1221695283 * std::pow(green, -0.0755148492);
  }
  green = std::clamp(green / 255.0, 0.0, 1.0);

  // Calculate Blue
  if (temp >= 66) {
    blue = 1.0;
  } else if (temp <= 19) {
    blue = 0.0;
  } else {
    blue = temp - 10;
    blue = 138.5177312231 * std::log(blue) - 305.0447927307;
    blue = std::clamp(blue / 255.0, 0.0, 1.0);
  }

  return {red, green, blue};
}

// === X11 BACKEND IMPLEMENTATION
// ===
std::vector<std::string> BrightnessManager::getConnectedMonitors() {
  if(!monitors.empty()) {
      return monitors;
  }
  if (!x11_display)
    return {};

  XRRScreenResources *screen_res =
      XRRGetScreenResourcesCurrent(x11_display, x11_root);
  if (!screen_res)
    return {};

  for (int i = 0; i < screen_res->noutput; ++i) {
    XRROutputInfo *output_info =
        XRRGetOutputInfo(x11_display, screen_res, screen_res->outputs[i]);
    if (output_info && output_info->connection == RR_Connected) {
      monitors.emplace_back(output_info->name);
    }
    if (output_info)
      XRRFreeOutputInfo(output_info);
  }

  XRRFreeScreenResources(screen_res);
  return monitors;
}

bool BrightnessManager::setBrightnessXrandr(const std::string &monitor,
                                            double brightness) {
  if (!x11_display)
    return false;

  brightness = std::clamp(brightness, 0.0, 1.0);

  XRRScreenResources *screen_res =
      XRRGetScreenResourcesCurrent(x11_display, x11_root);
  if (!screen_res)
    return false;

  bool success = false;

  // Find the output
  for (int i = 0; i < screen_res->noutput; ++i) {
    XRROutputInfo *output_info =
        XRRGetOutputInfo(x11_display, screen_res, screen_res->outputs[i]);

    if (output_info && output_info->connection == RR_Connected &&
        monitor == output_info->name) {

      // Get brightness property
      Atom brightness_atom = XInternAtom(x11_display, "Backlight", x11::XFalse);
      if (brightness_atom == x11::XNone) {
        brightness_atom = XInternAtom(x11_display, "BACKLIGHT", x11::XFalse);
      }

      if (brightness_atom != x11::XNone) {
        XRRPropertyInfo *prop_info = XRRQueryOutputProperty(
            x11_display, screen_res->outputs[i], brightness_atom);
        if (prop_info) {
          // Calculate brightness
          // value in the property's
          // range
          int32_t brightness_value = static_cast<int32_t>(
            brightness * (prop_info->values[1] - prop_info->values[0]) + prop_info->values[0]);
        
          XRRChangeOutputProperty(
              x11_display, screen_res->outputs[i], brightness_atom, XA_INTEGER,
              32, PropModeReplace, (unsigned char *)&brightness_value, 1);

          XFlush(x11_display);
          success = true;

          XFree(prop_info);
        }
      }
    }

    if (output_info)
      XRRFreeOutputInfo(output_info);
    if (success)
      break;
  }

  XRRFreeScreenResources(screen_res);
  return success;
}

bool BrightnessManager::setGammaXrandrRGB(const std::string &monitor,
                                          double red, double green,
                                          double blue) {
  if (!x11_display)
    return false;

  red = std::clamp(red, 0.1, 10.0);
  green = std::clamp(green, 0.1, 10.0);
  blue = std::clamp(blue, 0.1, 10.0);

  XRRScreenResources *screen_res =
      XRRGetScreenResourcesCurrent(x11_display, x11_root);
  if (!screen_res)
    return false;

  bool success = false;

  // Find the CRTC for this output
  for (int i = 0; i < screen_res->noutput; ++i) {
    XRROutputInfo *output_info =
        XRRGetOutputInfo(x11_display, screen_res, screen_res->outputs[i]);

    if (output_info && output_info->connection == RR_Connected &&
        monitor == output_info->name && output_info->crtc != x11::XNone) {

      XRRCrtcInfo *crtc_info =
          XRRGetCrtcInfo(x11_display, screen_res, output_info->crtc);
      if (crtc_info) {
        int gamma_size = XRRGetCrtcGammaSize(x11_display, output_info->crtc);
        if (gamma_size > 0) {
          XRRCrtcGamma *gamma = XRRAllocGamma(gamma_size);
          if (gamma) {
            // Generate gamma ramps
            for (int j = 0; j < gamma_size; ++j) {
              double normalized = (double)j / (gamma_size - 1);
              gamma->red[j] =
                  (unsigned short)(65535 * std::pow(normalized, 1.0 / red));
              gamma->green[j] =
                  (unsigned short)(65535 * std::pow(normalized, 1.0 / green));
              gamma->blue[j] =
                  (unsigned short)(65535 * std::pow(normalized, 1.0 / blue));
            }

            XRRSetCrtcGamma(x11_display, output_info->crtc, gamma);
            XFlush(x11_display);
            success = true;

            XRRFreeGamma(gamma);
          }
        }
        XRRFreeCrtcInfo(crtc_info);
      }
    }

    if (output_info)
      XRRFreeOutputInfo(output_info);
    if (success)
      break;
  }

  XRRFreeScreenResources(screen_res);
  return success;
}

// === WAYLAND BACKEND IMPLEMENTATION ===
#ifdef __WAYLAND__
std::vector<std::string> BrightnessManager::getConnectedMonitorsWayland() {
  std::vector<std::string> monitors;
  std::lock_guard<std::mutex> lock(wayland_mutex);

  for (const auto &output : wayland_outputs) {
    if (!output.name.empty()) {
      monitors.push_back(output.name);
    }
  }

  return monitors;
}

bool BrightnessManager::setBrightnessWayland(const std::string &monitor,
                                             double brightness) {
  if (!wl_display || !gamma_control_manager)
    return false;

  brightness = std::clamp(brightness, 0.0, 1.0);

  std::lock_guard<std::mutex> lock(wayland_mutex);
  bool success = false;

  for (const auto &output : wayland_outputs) {
    if (output.name == monitor || (monitor.empty() && !output.name.empty())) {
      auto gamma_control = zwlr_gamma_control_manager_v1_get_gamma_control(
          gamma_control_manager, output.wl_output);

      if (gamma_control) {
        // Create a gamma ramp with the given brightness
        const uint16_t ramp_size = 256;
        uint16_t ramp[ramp_size];

        for (int i = 0; i < ramp_size; ++i) {
          double value = (i * brightness * 65535) / (ramp_size - 1);
          ramp[i] = static_cast<uint16_t>(std::clamp(value, 0.0, 65535.0));
        }

        // Apply the gamma ramp
        zwlr_gamma_control_v1_set_gamma(gamma_control, ramp, ramp, ramp,
                                        ramp_size);
        wl_display_roundtrip(wl_display);

        zwlr_gamma_control_v1_destroy(gamma_control);
        success = true;
      }

      if (!monitor.empty())
        break; // Only process the specified monitor
    }
  }

  return success;
}

bool BrightnessManager::setGammaWaylandRGB(const std::string &monitor,
                                           double red, double green,
                                           double blue) {
  if (!wl_display || !gamma_control_manager)
    return false;

  red = std::clamp(red, 0.1, 10.0);
  green = std::clamp(green, 0.1, 10.0);
  blue = std::clamp(blue, 0.1, 10.0);

  std::lock_guard<std::mutex> lock(wayland_mutex);
  bool success = false;

  for (const auto &output : wayland_outputs) {
    if (output.name == monitor || (monitor.empty() && !output.name.empty())) {
      auto gamma_control = zwlr_gamma_control_manager_v1_get_gamma_control(
          gamma_control_manager, output.wl_output);

      if (gamma_control) {
        const uint16_t ramp_size = 256;
        uint16_t r_ramp[ramp_size];
        uint16_t g_ramp[ramp_size];
        uint16_t b_ramp[ramp_size];

        // Generate gamma ramps for each color channel
        for (int i = 0; i < ramp_size; ++i) {
          double value = static_cast<double>(i) / (ramp_size - 1);

          r_ramp[i] = static_cast<uint16_t>(
              std::clamp(std::pow(value, 1.0 / red) * 65535.0, 0.0, 65535.0));
          g_ramp[i] = static_cast<uint16_t>(
              std::clamp(std::pow(value, 1.0 / green) * 65535.0, 0.0, 65535.0));
          b_ramp[i] = static_cast<uint16_t>(
              std::clamp(std::pow(value, 1.0 / blue) * 65535.0, 0.0, 65535.0));
        }

        // Apply the gamma ramps
        zwlr_gamma_control_v1_set_gamma(gamma_control, r_ramp, g_ramp, b_ramp,
                                        ramp_size);
        wl_display_roundtrip(wl_display);

        zwlr_gamma_control_v1_destroy(gamma_control);
        success = true;
      }

      if (!monitor.empty())
        break; // Only process the specified monitor
    }
  }

  return success;
}
#endif
// === KELVIN TEMPERATURE METHODS ===
bool BrightnessManager::setTemperature(int kelvin) {
  kelvin = std::clamp(kelvin, MIN_TEMPERATURE, MAX_TEMPERATURE);
  RGBColor rgb = kelvinToRGB(kelvin);

  bool success = setGammaRGB(rgb.red, rgb.green, rgb.blue);
  if (success) {
    temperature[primaryMonitor] = kelvin;
  }

  return success;
}

bool BrightnessManager::setTemperature(const std::string &monitor, int kelvin) {
  kelvin = std::clamp(kelvin, MIN_TEMPERATURE, MAX_TEMPERATURE);
  RGBColor rgb = kelvinToRGB(kelvin);

  bool success = setGammaRGB(monitor, rgb.red, rgb.green, rgb.blue);
  if (success) {
    temperature[monitor] = kelvin;
  }

  return success;
}

// === COMBINED OPERATIONS ===
bool BrightnessManager::setBrightnessAndRGB(double brightness, double red,
                                            double green, double blue) {
  bool brightness_success = setBrightness(brightness);
  bool gamma_success = setGammaRGB(red, green, blue);
  return brightness_success && gamma_success;
}

bool BrightnessManager::setBrightnessAndRGB(const std::string &monitor,
                                            double brightness, double red,
                                            double green, double blue) {
  bool brightness_success = setBrightness(monitor, brightness);
  bool gamma_success = setGammaRGB(monitor, red, green, blue);
  return brightness_success && gamma_success;
}

bool BrightnessManager::setBrightnessAndTemperature(double brightness,
                                                    int kelvin) {
  auto rgb = kelvinToRGB(kelvin);
  temperature[primaryMonitor] = kelvin;
  return setBrightnessAndRGB(brightness, rgb.red, rgb.green, rgb.blue);
}

bool BrightnessManager::setBrightnessAndTemperature(const std::string &monitor,
                                                    double brightness,
                                                    int kelvin) {
  auto rgb = kelvinToRGB(kelvin);
  temperature[monitor] = kelvin;
  return setBrightnessAndRGB(monitor, brightness, rgb.red, rgb.green, rgb.blue);
}

// === RGB GAMMA METHODS ===
bool BrightnessManager::setGammaRGB(double red, double green, double blue) {
  return displayMethod == "randr" ? setGammaXrandrRGB(red, green, blue)
                                  : setGammaWaylandRGB(red, green, blue);
}

bool BrightnessManager::setGammaRGB(const std::string &monitor, double red,
                                    double green, double blue) {
  return displayMethod == "randr"
             ? setGammaXrandrRGB(monitor, red, green, blue)
             : setGammaWaylandRGB(monitor, red, green, blue);
}

// === TEMPERATURE INCREMENT METHODS ===
bool BrightnessManager::increaseTemperature(int amount) {
  int newTemp =
      std::min(MAX_TEMPERATURE, static_cast<int>(temperature[primaryMonitor]) + amount);
  return setTemperature(newTemp);
}

bool BrightnessManager::increaseTemperature(const std::string &monitor,
                                            int amount) {
  int newTemp =
      std::min(MAX_TEMPERATURE, static_cast<int>(temperature[monitor]) + amount);
  return setTemperature(monitor, newTemp);
}

// === BRIGHTNESS CONTROL METHODS ===
bool BrightnessManager::setBrightness(double brightness) {
  brightness = std::clamp(brightness, 0.0, 1.0);
  bool success = false;

  if (WindowManagerDetector::IsX11()) {
    success = setBrightnessXrandr(brightness);
  } else {
    success = setBrightnessWayland(brightness);
  }

  if (success) {
    this->brightness[primaryMonitor] = brightness;
  }

  return success;
}

bool BrightnessManager::setBrightness(const std::string &monitor,
                                      double brightness) {
  brightness = std::clamp(brightness, 0.0, 1.0);
  bool success = false;

  if (WindowManagerDetector::IsX11()) {
    success = setBrightnessXrandr(monitor, brightness);
  } else {
    success = setBrightnessWayland(monitor, brightness);
  }

  if (success) {
    this->brightness[monitor] = brightness;
  }

  return success;
}
// === BRIGHTNESS GETTERS ===
double BrightnessManager::getBrightness() {
  // Get brightness from primary/first monitor
  auto monitors = getConnectedMonitors();
  if (monitors.empty()) return 0.0;
  return getBrightness(monitors[0]);
}

double BrightnessManager::getBrightness(const std::string& monitor) {
  if (WindowManagerDetector::IsX11()) {
      return getBrightnessXrandr(monitor);
  }
#ifdef __WAYLAND__
  else if (WindowManagerDetector::IsWayland()) {
      // Wayland doesn't have a direct brightness query API
      // We'd need to track our last-set values or use sysfs
      return getBrightnessWayland(monitor);
  }
#endif
  return 0.0;
}

// === GAMMA GETTERS ===
BrightnessManager::RGBColor BrightnessManager::getGammaRGB() {
  auto monitors = getConnectedMonitors();
  if (monitors.empty()) return {1.0, 1.0, 1.0};
  return getGammaRGB(monitors[0]);
}

// === TEMPERATURE GETTERS ===
int BrightnessManager::getTemperature() {
  auto monitors = getConnectedMonitors();
  if (monitors.empty()) return 6500;
  return getTemperature(monitors[0]);
}

int BrightnessManager::getTemperature(const std::string& monitor) {
  // Get current RGB gamma values and reverse-engineer the temperature
  RGBColor currentGamma = getGammaRGB(monitor);
  int temp = rgbToKelvin(currentGamma);
  temperature[monitor] = temp;
  return temp;
}
void BrightnessManager::decreaseGamma(int amount) {
    temperature[primaryMonitor] = std::max(MIN_TEMPERATURE, temperature[primaryMonitor] - amount);
    setTemperature(temperature[primaryMonitor]);
}
void BrightnessManager::increaseGamma(int amount) {
    temperature[primaryMonitor] = std::min(MAX_TEMPERATURE, temperature[primaryMonitor] + amount);
    setTemperature(temperature[primaryMonitor]);
}
// === X11 BRIGHTNESS GETTER ===
double BrightnessManager::getBrightnessXrandr(const std::string& monitor) {
  if (!x11_display) return 0.0;

  XRRScreenResources* screen_res = XRRGetScreenResourcesCurrent(x11_display, x11_root);
  if (!screen_res) return 0.0;

  double brightness = 0.0;

  for (int i = 0; i < screen_res->noutput; ++i) {
      XRROutputInfo* output_info = XRRGetOutputInfo(x11_display, screen_res, screen_res->outputs[i]);
      
      if (output_info && output_info->connection == RR_Connected && 
          monitor == output_info->name) {
          
          // Try Backlight property first
          Atom brightness_atom = XInternAtom(x11_display, "Backlight", x11::XFalse);
          if (brightness_atom == x11::XNone) {
              brightness_atom = XInternAtom(x11_display, "BACKLIGHT", x11::XFalse);
          }
          
          if (brightness_atom != x11::XNone) {
              Atom actual_type;
              int actual_format;
              unsigned long nitems, bytes_after;
              unsigned char* prop = nullptr;
              
              if (XRRGetOutputProperty(x11_display, screen_res->outputs[i], brightness_atom,
                                     0, 4, x11::XFalse, x11::XFalse, x11::XNone,
                                     &actual_type, &actual_format, &nitems, &bytes_after, &prop) == x11::XSuccess) {
                  
                  if (prop && nitems == 1 && actual_format == 32) {
                      long current_value = *((long*)prop);
                      
                      // Get the property range
                      XRRPropertyInfo* prop_info = XRRQueryOutputProperty(x11_display, screen_res->outputs[i], brightness_atom);
                      if (prop_info) {
                          long min_val = prop_info->values[0];
                          long max_val = prop_info->values[1];
                          brightness = (double)(current_value - min_val) / (max_val - min_val);
                          XFree(prop_info);
                      }
                  }
                  if (prop) XFree(prop);
              }
          }
      }
      
      if (output_info) XRRFreeOutputInfo(output_info);
      if (brightness > 0.0) break; // Found it
  }

  XRRFreeScreenResources(screen_res);
  return brightness;
}

// === SYSFS BRIGHTNESS FALLBACK ===
double BrightnessManager::getBrightnessSysfs(const std::string& monitor) {
  // Try common backlight paths
  std::vector<std::string> backlight_paths = {
      "/sys/class/backlight/intel_backlight/brightness",
      "/sys/class/backlight/acpi_video0/brightness",
      "/sys/class/backlight/amdgpu_bl0/brightness"
  };
  
  for (const auto& brightness_path : backlight_paths) {
      std::ifstream brightness_file(brightness_path);
      std::ifstream max_brightness_file(brightness_path.substr(0, brightness_path.rfind('/')) + "/max_brightness");
      
      if (brightness_file.is_open() && max_brightness_file.is_open()) {
          int current, max;
          brightness_file >> current;
          max_brightness_file >> max;
          
          return (double)current / max;
      }
  }
  
  return 0.0; // Couldn't read
}

// === X11 GAMMA GETTER ===
double BrightnessManager::getGammaXrandr(const std::string& monitor) {
  if (!x11_display) return 1.0;

  XRRScreenResources* screen_res = XRRGetScreenResourcesCurrent(x11_display, x11_root);
  if (!screen_res) return 1.0;

  double gamma = 1.0;

  for (int i = 0; i < screen_res->noutput; ++i) {
      XRROutputInfo* output_info = XRRGetOutputInfo(x11_display, screen_res, screen_res->outputs[i]);
      
      if (output_info && output_info->connection == RR_Connected && 
          monitor == output_info->name && output_info->crtc != x11::XNone) {
          
          XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(x11_display, screen_res, output_info->crtc);
          if (crtc_info) {
              int gamma_size = XRRGetCrtcGammaSize(x11_display, output_info->crtc);
              if (gamma_size > 0) {
                  XRRCrtcGamma* crtc_gamma = XRRGetCrtcGamma(x11_display, output_info->crtc);
                  if (crtc_gamma) {
                      // Calculate average gamma from the ramp (simplified)
                      // In reality, you'd want to analyze the curve more carefully
                      double red_gamma = (double)crtc_gamma->red[static_cast<int>(gamma_size/2)] / 65535.0;
                      double green_gamma = (double)crtc_gamma->green[static_cast<int>(gamma_size/2)] / 65535.0;
                      double blue_gamma = (double)crtc_gamma->blue[static_cast<int>(gamma_size/2)] / 65535.0;
                      
                      gamma = (red_gamma + green_gamma + blue_gamma) / 3.0;
                      
                      XRRFreeGamma(crtc_gamma);
                  }
              }
              XRRFreeCrtcInfo(crtc_info);
          }
      }
      
      if (output_info) XRRFreeOutputInfo(output_info);
      if (gamma != 1.0) break;
  }

  XRRFreeScreenResources(screen_res);
  return gamma;
}

// === RGB TO KELVIN CONVERSION ===
int BrightnessManager::rgbToKelvin(const RGBColor& rgb) const {
  // This is the reverse of your kelvinToRGB function
  // It's an approximation - exact conversion is mathematically complex
  
  // Simple heuristic based on red/blue ratio
  double ratio = rgb.blue / rgb.red;
  
  if (ratio > 1.0) {
      // Cool temperature (high blue)
      return (int)(6500 + (ratio - 1.0) * 3500);
  } else {
      // Warm temperature (low blue)
      return (int)(6500 - (1.0 - ratio) * 4500);
  }
}

// === GET CURRENT RGB GAMMA VALUES ===
BrightnessManager::RGBColor BrightnessManager::getGammaRGB(const std::string& monitor) {
  if (WindowManagerDetector::IsX11()) {
      return getGammaXrandrRGB(monitor);
  }
#ifdef __WAYLAND__
  else if (WindowManagerDetector::IsWayland()) {
      // Wayland: we'd need to track our last-set values
      // or implement gamma reading from compositor
      std::scoped_lock lock(wayland_mutex);
      // Return last known values or neutral
      return {1.0, 1.0, 1.0};
  }
#endif
  return {1.0, 1.0, 1.0}; // neutral
}

BrightnessManager::RGBColor BrightnessManager::getGammaXrandrRGB(const std::string& monitor) {
  // Similar to getGammaX11 but returns RGB components separately
  if (!x11_display) return {1.0, 1.0, 1.0};

  XRRScreenResources* screen_res = XRRGetScreenResourcesCurrent(x11_display, x11_root);
  if (!screen_res) return {1.0, 1.0, 1.0};

  RGBColor rgb = {1.0, 1.0, 1.0};

  for (int i = 0; i < screen_res->noutput; ++i) {
      XRROutputInfo* output_info = XRRGetOutputInfo(x11_display, screen_res, screen_res->outputs[i]);
      
      if (output_info && output_info->connection == RR_Connected && 
          monitor == output_info->name && output_info->crtc != x11::XNone) {
          
          int gamma_size = XRRGetCrtcGammaSize(x11_display, output_info->crtc);
          if (gamma_size > 0) {
              XRRCrtcGamma* crtc_gamma = XRRGetCrtcGamma(x11_display, output_info->crtc);
              if (crtc_gamma) {
                  // Get RGB values from middle of gamma ramp
                  int mid = gamma_size / 2;
                  rgb.red = (double)crtc_gamma->red[mid] / 65535.0;
                  rgb.green = (double)crtc_gamma->green[mid] / 65535.0;
                  rgb.blue = (double)crtc_gamma->blue[mid] / 65535.0;
                  
                  XRRFreeGamma(crtc_gamma);
              }
          }
      }
      
      if (output_info) XRRFreeOutputInfo(output_info);
      if (rgb.red != 1.0 || rgb.green != 1.0 || rgb.blue != 1.0) break;
  }

  XRRFreeScreenResources(screen_res);
  return rgb;
}
bool BrightnessManager::increaseBrightness(double amount) {
  double newBrightness = std::min(1.0, brightness[primaryMonitor] + amount);
  return setBrightness(newBrightness);
}

bool BrightnessManager::increaseBrightness(const std::string &monitor,
                                           double amount) {
  double newBrightness = std::min(1.0, brightness[monitor] + amount);
  return setBrightness(monitor, newBrightness);
}

bool BrightnessManager::decreaseBrightness(double amount) {
  double newBrightness = std::max(0.0, brightness[primaryMonitor] - amount);
  return setBrightness(newBrightness);
}

bool BrightnessManager::decreaseBrightness(const std::string &monitor,
                                           double amount) {
  double newBrightness = std::max(0.0, brightness[monitor] - amount);
  return setBrightness(monitor, newBrightness);
}

// === TEMPERATURE INCREMENT METHODS (CONTINUED) ===
bool BrightnessManager::decreaseTemperature(int amount) {
  int newTemp =
      std::max(MIN_TEMPERATURE, static_cast<int>(temperature[primaryMonitor]) - amount);
  return setTemperature(newTemp);
}

bool BrightnessManager::decreaseTemperature(const std::string &monitor,
                                            int amount) {
  int newTemp =
      std::max(MIN_TEMPERATURE, static_cast<int>(temperature[monitor]) - amount);
  return setTemperature(monitor, newTemp);
}

// === X11/WAYLAND BACKEND WRAPPERS ===
bool BrightnessManager::setBrightnessXrandr(double brightness) {
  auto monitors = getConnectedMonitors();
  if (monitors.empty())
    return false;

  bool success = true;
  for (const auto &monitor : monitors) {
    if (!setBrightnessXrandr(monitor, brightness)) {
      success = false;
    }
  }
  return success;
}

bool BrightnessManager::setBrightnessWayland(double brightness) {
  return setBrightnessWayland("", brightness);
}

bool BrightnessManager::setGammaXrandrRGB(double red, double green,
                                          double blue) {
  auto monitors = getConnectedMonitors();
  if (monitors.empty())
    return false;

  bool success = true;
  for (const auto &monitor : monitors) {
    if (!setGammaXrandrRGB(monitor, red, green, blue)) {
      success = false;
    }
  }
  return success;
}

bool BrightnessManager::setGammaWaylandRGB(double red, double green,
                                           double blue) {
  return setGammaWaylandRGB("", red, green, blue);
}

// === DAY/NIGHT AUTOMATION ===
void BrightnessManager::enableDayNightMode(const DayNightSettings &settings) {
  std::lock_guard<std::mutex> lock(settingsMutex);
  dayNightSettings = settings;
  dayNightSettings.autoAdjust = true;

  // Stop existing thread if running
  if (dayNightThread) {
    stopDayNightThread = true;
    dayNightThread->join();
  }

  // Start new thread
  stopDayNightThread = false;
  dayNightThread =
      std::make_unique<std::thread>([this]() { dayNightWorkerThread(); });

  // Save settings to config
  Configs::Get().Set("Brightness.DayNightAutoAdjust",
                     dayNightSettings.autoAdjust);
  Configs::Get().Set("Brightness.DayStartHour", dayNightSettings.dayStartHour);
  Configs::Get().Set("Brightness.NightStartHour",
                     dayNightSettings.nightStartHour);
  Configs::Get().Set("Brightness.DayBrightness",
                     dayNightSettings.dayBrightness);
  Configs::Get().Set("Brightness.NightBrightness",
                     dayNightSettings.nightBrightness);
  Configs::Get().Set("Brightness.DayTemperature",
                     dayNightSettings.dayTemperature);
  Configs::Get().Set("Brightness.NightTemperature",
                     dayNightSettings.nightTemperature);
}

void BrightnessManager::disableDayNightMode() {
  {
    std::lock_guard<std::mutex> lock(settingsMutex);
    dayNightSettings.autoAdjust = false;
    stopDayNightThread = true;
  }

  if (dayNightThread && dayNightThread->joinable()) {
    dayNightThread->join();
    dayNightThread.reset();
  }
}

bool BrightnessManager::isDay() {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto *tm = std::localtime(&time_t);

  int currentHour = tm->tm_hour;

  std::lock_guard<std::mutex> lock(settingsMutex);
  return currentHour >= dayNightSettings.dayStartHour &&
         currentHour < dayNightSettings.nightStartHour;
}

void BrightnessManager::dayNightWorkerThread() {
  while (!stopDayNightThread) {
    applyCurrentTimeSettings();

    // Sleep for the check interval
    std::this_thread::sleep_for(dayNightSettings.checkInterval);
  }
}

void BrightnessManager::applyCurrentTimeSettings() {
  std::lock_guard<std::mutex> lock(settingsMutex);

  if (!dayNightSettings.autoAdjust)
    return;

  if (isDay()) {
    setBrightnessAndTemperature(dayNightSettings.dayBrightness,
                                dayNightSettings.dayTemperature);
    if (Configs::Get().GetVerboseWindowLogging()) {
      info("Applied day settings: brightness=" +
           std::to_string(dayNightSettings.dayBrightness) +
           ", temp=" + std::to_string(dayNightSettings.dayTemperature) + "K");
    }
  } else {
    setBrightnessAndTemperature(dayNightSettings.nightBrightness,
                                dayNightSettings.nightTemperature);
    if (Configs::Get().GetVerboseWindowLogging()) {
      info("Applied night settings: brightness=" +
           std::to_string(dayNightSettings.nightBrightness) +
           ", temp=" + std::to_string(dayNightSettings.nightTemperature) + "K");
    }
  }
}

// === MANUAL DAY/NIGHT SWITCHING ===
bool BrightnessManager::switchToDay() {
  std::lock_guard<std::mutex> lock(settingsMutex);
  return setBrightnessAndTemperature(dayNightSettings.dayBrightness,
                                     dayNightSettings.dayTemperature);
}

bool BrightnessManager::switchToNight() {
  std::lock_guard<std::mutex> lock(settingsMutex);
  return setBrightnessAndTemperature(dayNightSettings.nightBrightness,
                                     dayNightSettings.nightTemperature);
}

bool BrightnessManager::switchToDay(const std::string &monitor) {
  std::lock_guard<std::mutex> lock(settingsMutex);
  return setBrightnessAndTemperature(monitor, dayNightSettings.dayBrightness,
                                     dayNightSettings.dayTemperature);
}

bool BrightnessManager::switchToNight(const std::string &monitor) {
  std::lock_guard<std::mutex> lock(settingsMutex);
  return setBrightnessAndTemperature(monitor, dayNightSettings.nightBrightness,
                                     dayNightSettings.nightTemperature);
}

// === CONFIGURATION HELPERS ===
void BrightnessManager::setDaySettings(double brightness, int temperature) {
  std::lock_guard<std::mutex> lock(settingsMutex);
  dayNightSettings.dayBrightness = std::clamp(brightness, 0.0, 1.0);
  dayNightSettings.dayTemperature =
      std::clamp(temperature, MIN_TEMPERATURE, MAX_TEMPERATURE);

  // Save to config
  Configs::Get().Set("General.DayBrightness",
                     std::to_string(dayNightSettings.dayBrightness));
  Configs::Get().Set("General.DayTemperature",
                     std::to_string(dayNightSettings.dayTemperature));
}

void BrightnessManager::setNightSettings(double brightness, int temperature) {
  std::lock_guard<std::mutex> lock(settingsMutex);
  dayNightSettings.nightBrightness = std::clamp(brightness, 0.0, 1.0);
  dayNightSettings.nightTemperature =
      std::clamp(temperature, MIN_TEMPERATURE, MAX_TEMPERATURE);

  // Save to config
  Configs::Get().Set("General.NightBrightness",
                     std::to_string(dayNightSettings.nightBrightness));
  Configs::Get().Set("General.NightTemperature",
                     std::to_string(dayNightSettings.nightTemperature));
}

void BrightnessManager::setDayNightTiming(int dayStart, int nightStart) {
  std::lock_guard<std::mutex> lock(settingsMutex);
  dayNightSettings.dayStartHour = std::clamp(dayStart, 0, 23);
  dayNightSettings.nightStartHour = std::clamp(nightStart, 0, 23);

  // Save to config
  Configs::Get().Set("General.DayStartHour",
                     std::to_string(dayNightSettings.dayStartHour));
  Configs::Get().Set("General.NightStartHour",
                     std::to_string(dayNightSettings.nightStartHour));
}
} // namespace havel