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
#include "core/DisplayManager.hpp"

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
  // Check if we're running under Wayland
  const char *wayland_display = getenv("WAYLAND_DISPLAY");
  const char *xdg_session_type = getenv("XDG_SESSION_TYPE");

  if (WindowManagerDetector::IsWayland()) {
    // Running under Wayland
    try {
#ifdef __WAYLAND__
      initializeWayland();
      displayMethod = "wayland";
      debug("Initialized Wayland backend");
#else
      error("Wayland support not compiled in!");
#endif
    } catch (const std::exception &e) {
      error("Failed to initialize Wayland backend: " + std::string(e.what()));
      // Fall back to X11
      x11_display = DisplayManager::GetDisplay();
      x11_root = DisplayManager::GetRootWindow();
      displayMethod = "x11";
    }
  } else {
    // Default to X11
    x11_display = DisplayManager::GetDisplay();
    x11_root = DisplayManager::GetRootWindow();
    displayMethod = "x11";
  }

  vector<string> monitors = getConnectedMonitors();
  if (!monitors.empty()) {
    primaryMonitor = monitors[0];
    for (string monitor : monitors) {
      brightness[monitor] = getBrightness(monitor);
      info("Brightness for " + monitor + ": " +
           std::to_string(brightness[monitor]));
      shadowLift[monitor] = 0.0;
      gammaRGB[monitor] = {1.0, 1.0, 1.0};
      temperature[monitor] = getTemperature(monitor);
      info("Temperature for " + monitor + ": " +
           std::to_string(temperature[monitor]));
    }
    // Apply initial settings if auto-adjust is enabled
    if (dayNightSettings.autoAdjust) {
      applyCurrentTimeSettings();
    }
  } else {
    error("No monitors detected!");
  }
}

BrightnessManager::~BrightnessManager() {
  disableDayNightMode();

#ifdef __WAYLAND__
  if (wl_display) {
    // Clean up Wayland resources
    if (wl_registry) {
      wl_registry_destroy(wl_registry);
      wl_registry = nullptr;
    }

    // Clean up gamma control manager if it exists
    if (gamma_control_manager) {
      // Note: The protocol doesn't provide a destroy function for the manager
      // as it's a global object managed by the compositor
      gamma_control_manager = nullptr;
    }

    // Clean up XDG output manager if it exists
    if (xdg_output_manager) {
      // Note: The protocol doesn't provide a destroy function for the manager
      xdg_output_manager = nullptr;
    }

    // Clean up outputs
    for (auto &output : wayland_outputs) {
      if (output.xdg_output) {
        zxdg_output_v1_destroy(output.xdg_output);
        output.xdg_output = nullptr;
      }
      if (output.wl_output) {
        wl_output_destroy(output.wl_output);
        output.wl_output = nullptr;
      }
    }
    wayland_outputs.clear();

    // Disconnect from the display
    wl_display_disconnect(wl_display);
    wl_display = nullptr;
  }
#endif
}

vector<string> BrightnessManager::getConnectedMonitors() {
    return DisplayManager::GetMonitorNames();
}

string BrightnessManager::getMonitor(int index) {
  auto monitors = getConnectedMonitors();
  if (index >= 0 && index < static_cast<int>(monitors.size())) {
      return monitors[index];
  }
  error("Invalid monitor index requested: {}", index);
  return "";
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
  // This function is rarely used in modern compositors but included for completeness
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
  debug("Initializing Wayland backend...");

  // Connect to the Wayland display
  wl_display = wl_display_connect(nullptr);
  if (!wl_display) {
    error("Failed to connect to Wayland display");
    throw std::runtime_error("Failed to connect to Wayland display");
  }

  debug("Connected to Wayland display");

  // Get the registry and add listener
  wl_registry = wl_display_get_registry(wl_display);
  if (!wl_registry) {
    wl_display_disconnect(wl_display);
    wl_display = nullptr;
    error("Failed to get Wayland registry");
    throw std::runtime_error("Failed to get Wayland registry");
  }

  debug("Got Wayland registry, adding listener...");
  wl_registry_add_listener(wl_registry, &registry_listener, nullptr);

  // Process initial registry events
  debug("Processing initial registry events...");
  if (wl_display_roundtrip(wl_display) == -1) {
    wl_registry_destroy(wl_registry);
    wl_display_disconnect(wl_display);
    wl_display = nullptr;
    error("Failed to process initial registry events");
    throw std::runtime_error("Failed to process initial registry events");
  }

  // Check if we have the required protocols
  if (!gamma_control_manager) {
    error("Compositor doesn't support wlr-gamma-control protocol");
    if (wl_registry)
      wl_registry_destroy(wl_registry);
    if (wl_display)
      wl_display_disconnect(wl_display);
    wl_display = nullptr;
    throw std::runtime_error(
        "Compositor doesn't support wlr-gamma-control protocol");
  }

  debug("Found required Wayland protocols");

  // Set up XDG output for each output if available
  if (!wayland_outputs.empty()) {
    debug("Setting up XDG outputs...");
    for (auto &output : wayland_outputs) {
      if (xdg_output_manager && output.wl_output) {
        output.xdg_output = zxdg_output_manager_v1_get_xdg_output(
            xdg_output_manager, output.wl_output);
        if (output.xdg_output) {
          zxdg_output_v1_add_listener(output.xdg_output, &xdg_output_listener,
                                      &output);
          debug("Added XDG output listener");
        }
      }
    }

    // Wait for all outputs to be configured
    debug("Waiting for output configuration...");
    if (wl_display_roundtrip(wl_display) == -1) {
      error("Failed to configure outputs");
      // Don't fail here, we might still be able to work without XDG output
    }
  } else {
    debug("No Wayland outputs found");
  }

  displayMethod = "wayland";
  debug("Wayland backend initialized successfully");
}
#endif

// === KELVIN TO RGB CONVERSION ===
// Based on algorithms used in color temperature calculations, similar to Redshift/gammastep
BrightnessManager::RGBColor BrightnessManager::kelvinToRGB(int kelvin) const {
    kelvin = std::clamp(kelvin, MIN_TEMPERATURE, MAX_TEMPERATURE);
    double temp = static_cast<double>(kelvin);

    double r, g, b;

    if (temp <= 6600.0) {
        r = 1.0;
        g = 0.390081578769 * ::log(temp) - 0.631841443788;
    } else {
        r = 1.292936186062 * pow(temp - 6000.0, -0.1332047592);
        g = 1.129890860895 * pow(temp - 6000.0, -0.0755148492);
    }
    
    if (temp <= 1900.0) {
        b = 0.0;
    } else if (temp <= 6600.0) {
        b = 0.543206789110 * ::log(temp - 1000.0) - 1.19625408914;
    } else {
        b = 1.0;
    }

    // Clamp values
    r = std::clamp(r, 0.0, 1.0);
    g = std::clamp(g, 0.0, 1.0);
    b = std::clamp(b, 0.0, 1.0);
    
    return {r, g, b};
}


// === X11 BACKEND IMPLEMENTATION
// ===
// Shadow lift with highlight protection and midtone bias
BrightnessManager::RGBColor BrightnessManager::applyShadowLift(const BrightnessManager::RGBColor& input, double lift) {
  // Early return for no lift
  if (lift <= 0.0001) {
      return input;
  }

  // Get config values with sensible defaults
  const bool doHighlightProtect = Configs::Get().Get<bool>("Brightness.DoHighlightProtect", true);
  const double highlightThreshold = Configs::Get().Get<double>("Brightness.HighlightProtect", 0.9);
  const bool useMidtoneBias = Configs::Get().Get<bool>("Brightness.MidtoneBias", true);
  const double midtoneBiasAmount = Configs::Get().Get<double>("Brightness.MidtoneBiasAmount", 0.5);

  auto processChannel = [&](double channel) -> double {
      const double normalized = std::clamp(channel, 0.0, 1.0);
      double effectiveLift = lift;

      // Apply highlight protection if enabled
      if (doHighlightProtect && normalized > highlightThreshold) {
          const double protectionFactor = (1.0 - normalized) / (1.0 - highlightThreshold);
          effectiveLift *= protectionFactor;
      }

      // Apply midtone bias if enabled
      if (useMidtoneBias) {
          const double bias = std::pow(normalized, midtoneBiasAmount);
          effectiveLift *= bias;
      }

      // Apply shadow lift with gamma adjustment
      const double gammaAdjust = 1.0 / (1.0 + effectiveLift);
      const double gammaLifted = std::pow(normalized, gammaAdjust);
      const double additiveLifted = effectiveLift * (1.0 - normalized);
      
      return std::clamp(gammaLifted + additiveLifted, 0.0, 1.0);
  };

  return {
      processChannel(input.red),
      processChannel(input.green),
      processChannel(input.blue)
  };
}

bool BrightnessManager::setBrightnessAndShadowLift(double brightness, double shadowLift) {
  return setBrightness(brightness) && setShadowLift(shadowLift);
}

bool BrightnessManager::setBrightnessAndShadowLift(const string& monitor, double brightness, double shadowLift){
  return setBrightness(monitor, brightness) && setShadowLift(monitor, shadowLift);
}
// === SHADOW LIFT METHODS ===
bool BrightnessManager::setShadowLift(const std::string& monitor, double lift) {
  if (lift < 0.001) lift = 0.0;
  if (lift > 4.0) lift = 4.0;
  if (lift < 0.0 || lift > 4.0) {
      error("Shadow lift must be between 0.0 and 4.0, got: {:.3f}", lift);
      return false;
  }
  
  // Store the shadow lift value for this monitor
  shadowLift[monitor] = lift;
  
  // Get current gamma values
  RGBColor currentGamma = getGammaRGB(monitor);
  
  // Apply shadow lift
  RGBColor liftedGamma = applyShadowLift(currentGamma, lift);
  
  // Set the modified gamma
  return setGammaRGB(monitor, liftedGamma.red, liftedGamma.green, liftedGamma.blue);
}

bool BrightnessManager::setShadowLift(double lift) {
  bool success = true;
  for (const auto& monitor : getConnectedMonitors()) {
      success = setShadowLift(monitor, lift) && success;
  }
  return success;
}
double BrightnessManager::getShadowLift(int monitorIndex) {
  auto monitorName = getMonitor(monitorIndex);
  if (monitorName.empty()) return 0.0;
  return getShadowLift(monitorName);
}
double BrightnessManager::getShadowLift(const string& monitor) {
  // Return the stored shadow lift value, or 0.0 if not set
  auto it = shadowLift.find(monitor);
  if (it != shadowLift.end()) {
      return it->second;
  }
  // Initialize to 0.0 if not set
  shadowLift[monitor] = 0.0;
  return 0.0;
}

// All monitors version
double BrightnessManager::getShadowLift() {
  if (primaryMonitor.empty()) {
      vector<string> monitors = getConnectedMonitors();
      if (monitors.empty()) {
          error("No monitors found");
          return 0.0;
      }
      return getShadowLift(monitors[0]);
  }
  return getShadowLift(primaryMonitor);
}
double BrightnessManager::getBrightnessGamma(const std::string &monitor) {
  if (!x11_display)
    return 1.0;

  DisplayManager::MonitorInfo monitorInfo = DisplayManager::GetMonitorByName(monitor);
  if (monitorInfo.id == 0 || monitorInfo.crtc_id == 0) {
      error("Monitor '{}' not found or is not active for getBrightnessGamma.", monitor);
      return 1.0;
  }

  int gamma_size = XRRGetCrtcGammaSize(x11_display, monitorInfo.crtc_id);
  if (gamma_size > 0) {
    XRRCrtcGamma *crtc_gamma = XRRGetCrtcGamma(x11_display, monitorInfo.crtc_id);
    if (crtc_gamma) {
      // Calculate average of all gamma values for a more accurate brightness
      unsigned long long total = 0;
      for (int j = 0; j < gamma_size; j++) {
        total += crtc_gamma->red[j] + crtc_gamma->green[j] + crtc_gamma->blue[j];
      }
      double brightness = (double)total / (gamma_size * 3 * 65535.0);
      XRRFreeGamma(crtc_gamma);
      return brightness;
    }
  }

  return 1.0;
}

/**
 * Safely extracts brightness value from gamma ramp using proper bounds checking
 * and error handling. Uses the middle value of the red channel as brightness
 * estimate.
 *
 * @param gamma - XRandR gamma ramp structure (must not be null)
 * @param monitor_name - Monitor identifier for logging purposes
 * @return brightness value [0.0, 1.0] or -1.0 on error
 */
double BrightnessManager::extractBrightnessFromGammaRamp(
    XRRCrtcGamma *gamma, const std::string &monitor_name) const {
  // Validate input parameters
  if (!gamma) {
    error("Gamma ramp is null for monitor: {}", monitor_name);
    return -1.0;
  }

  if (!gamma->red) {
    error("Gamma red channel is null for monitor: {}", monitor_name);
    return -1.0;
  }

  if (gamma->size <= 0) {
    error("Invalid gamma ramp size ({}) for monitor: {}", gamma->size,
                  monitor_name);
    return -1.0;
  }

  // Use middle index of gamma ramp for brightness estimation
  const int middle_index = gamma->size / 2;
  const uint16_t raw_gamma_value = gamma->red[middle_index];

  // Convert from 16-bit gamma value to normalized brightness [0.0, 1.0]
  // Max gamma value is 65535 (2^16 - 1)
  constexpr double MAX_GAMMA_VALUE = 65535.0;
  const double normalized_brightness =
      static_cast<double>(raw_gamma_value) / MAX_GAMMA_VALUE;

  // Clamp to valid range (paranoid safety check)
  const double clamped_brightness = std::clamp(normalized_brightness, 0.0, 1.0);

  debug("Monitor {}: gamma_size={}, middle_index={}, raw_value={}, "
                "brightness={:.3f}",
                monitor_name, gamma->size, middle_index, raw_gamma_value,
                clamped_brightness);

  return clamped_brightness;
}

/**
 * Gets current brightness for specified monitor using X11/XRandR
 * Handles all resource cleanup automatically via RAII wrapper
 */
double
BrightnessManager::getCurrentBrightnessX11(const std::string &monitor_name) {
  if (!x11_display) {
    error("X11 display not initialized");
    return -1.0;
  }

  DisplayManager::MonitorInfo monitorInfo = DisplayManager::GetMonitorByName(monitor_name);
  if (monitorInfo.id == 0 || monitorInfo.crtc_id == 0) {
      error("Monitor '{}' not found or is not active for getCurrentBrightnessX11.", monitor_name);
      return -1.0;
  }
  
  XRRCrtcGamma *gamma = XRRGetCrtcGamma(x11_display, monitorInfo.crtc_id);
  if (!gamma) {
    error("Failed to get gamma ramp for monitor '{}'", monitor_name);
    return -1.0;
  }
  
  const double brightness =
      extractBrightnessFromGammaRamp(gamma, monitor_name);
  XRRFreeGamma(gamma);

  if (brightness >= 0.0) {
    info("Successfully retrieved brightness {:.3f} for monitor '{}'",
                 brightness, monitor_name);
  }

  return brightness;
}
bool BrightnessManager::setBrightnessXrandr(const std::string &monitor,
                                            double brightness) {
  if (!x11_display)
    return false;

  brightness = std::clamp(brightness, 0.0, 1.0);

  DisplayManager::MonitorInfo monitorInfo = DisplayManager::GetMonitorByName(monitor);
  if (monitorInfo.id == 0) {
      error("Monitor '{}' not found for setBrightnessXrandr.", monitor);
      return false;
  }

  // Try different brightness property names
  const char *property_names[] = {"Brightness", "brightness", "Backlight",
                                  "BACKLIGHT", nullptr};

  for (int prop_idx = 0; property_names[prop_idx] != nullptr; prop_idx++) {
    Atom brightness_atom =
        XInternAtom(x11_display, property_names[prop_idx], x11::XFalse);

    if (brightness_atom != x11::XNone) {
      XRRPropertyInfo *prop_info = XRRQueryOutputProperty(
          x11_display, monitorInfo.id, brightness_atom);

      if (prop_info) {
        if (prop_info->num_values >= 2) {
          // Has min/max range
          int32_t brightness_value = static_cast<int32_t>(
              brightness * (prop_info->values[1] - prop_info->values[0]) +
              prop_info->values[0]);

          XRRChangeOutputProperty(x11_display, monitorInfo.id,
                                  brightness_atom, XA_INTEGER, 32,
                                  PropModeReplace,
                                  (unsigned char *)&brightness_value, 1);
        } else {
          // No range - assume it's a float value
          if (strcmp(property_names[prop_idx], "Brightness") == 0) {
            // Store float as int bits
            float float_brightness = (float)brightness;
            int32_t int_bits = *((int32_t *)&float_brightness);
            XRRChangeOutputProperty(x11_display, monitorInfo.id,
                                    brightness_atom, XA_INTEGER, 32,
                                    PropModeReplace,
                                    (unsigned char *)&int_bits, 1);
          }
        }

        XFlush(x11_display);
        XFree(prop_info);
        return true;
      }
    }
  }

  return false;
}
bool BrightnessManager::setBrightnessGamma(const std::string &monitor, double brightness) {
  if (!x11_display)
      return false;

  brightness = std::clamp(brightness, 0.0, 2.0);
  
  // Store brightness for this monitor
  this->brightness[monitor] = brightness;
  
  return applyAllSettings(monitor);
}

bool BrightnessManager::setGammaXrandrRGB(const std::string &monitor, double red, double green, double blue) {
  if (!x11_display)
      return false;

  red = std::clamp(red, 0.1, 10.0);
  green = std::clamp(green, 0.1, 10.0);
  blue = std::clamp(blue, 0.1, 10.0);
  
  // Store gamma values for this monitor (you'll need to add this map)
  gammaRGB[monitor] = {red, green, blue};
  
  return applyAllSettings(monitor);
}

bool BrightnessManager::applyAllSettings(const std::string &monitor) {
  if (!x11_display)
      return false;

  DisplayManager::MonitorInfo monitorInfo = DisplayManager::GetMonitorByName(monitor);
  if (monitorInfo.id == 0 || monitorInfo.crtc_id == 0) {
      error("Monitor '{}' not found or is not active.", monitor);
      return false;
  }

  // Get current settings for this monitor
  double currentBrightness = brightness.count(monitor) ? brightness[monitor] : 1.0;
  double currentShadowLift = shadowLift.count(monitor) ? shadowLift[monitor] : 0.0;
  RGBColor currentGamma = gammaRGB.count(monitor) ? gammaRGB[monitor] : RGBColor{1.0, 1.0, 1.0};
  RGBColor tempColor = {1.0, 1.0, 1.0};
  
  // Apply temperature if set
  if (temperature.count(monitor)) {
      tempColor = kelvinToRGB(temperature[monitor]);
  }

  int gamma_size = XRRGetCrtcGammaSize(x11_display, monitorInfo.crtc_id);
  if (gamma_size > 0) {
      XRRCrtcGamma *gamma = XRRAllocGamma(gamma_size);
      if (gamma) {
          // Generate combined gamma ramps
          for (int j = 0; j < gamma_size; ++j) {
              double normalized = (double)j / (gamma_size - 1);
              
              // Apply gamma curve first
              RGBColor gammaValue = {
                  std::pow(normalized, 1.0 / currentGamma.red),
                  std::pow(normalized, 1.0 / currentGamma.green), 
                  std::pow(normalized, 1.0 / currentGamma.blue)
              };
              
              // Apply shadow lift (lifts dark areas, preserves highlights)
              RGBColor liftedValue = {
                  gammaValue.red + currentShadowLift * (1.0 - gammaValue.red),
                  gammaValue.green + currentShadowLift * (1.0 - gammaValue.green),
                  gammaValue.blue + currentShadowLift * (1.0 - gammaValue.blue)
              };
              
              // Apply temperature tint and brightness
              double redValue = liftedValue.red * tempColor.red * currentBrightness;
              double greenValue = liftedValue.green * tempColor.green * currentBrightness;
              double blueValue = liftedValue.blue * tempColor.blue * currentBrightness;
              
              // Convert to 16-bit values and clamp
              gamma->red[j] = (unsigned short)std::clamp(redValue * 65535.0, 0.0, 65535.0);
              gamma->green[j] = (unsigned short)std::clamp(greenValue * 65535.0, 0.0, 65535.0);
              gamma->blue[j] = (unsigned short)std::clamp(blueValue * 65535.0, 0.0, 65535.0);
          }

          XRRSetCrtcGamma(x11_display, monitorInfo.crtc_id, gamma);
          XFlush(x11_display);
          XRRFreeGamma(gamma);
          return true;
      }
  }

  return false;
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
  
  bool success = true;
  for(const auto& monitor : getConnectedMonitors()){
      temperature[monitor] = kelvin;
      success &= applyAllSettings(monitor);
  }
  return success;
}

bool BrightnessManager::setTemperature(const std::string &monitor, int kelvin) {
  kelvin = std::clamp(kelvin, MIN_TEMPERATURE, MAX_TEMPERATURE);
  RGBColor rgb = kelvinToRGB(kelvin);

  // setGammaRGB will call applyAllSettings
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
  bool success = true;
  for (const auto& monitor : getConnectedMonitors()) {
    success &= setGammaRGB(monitor, red, green, blue);
  }
  return success;
}

bool BrightnessManager::setGammaRGB(const std::string &monitor, double red,
                                    double green, double blue) {
  if (displayMethod == "wayland") {
#ifdef __WAYLAND__
    return setGammaWaylandRGB(monitor, red, green, blue);
#else
    error("Wayland support not compiled in!");
    return false;
#endif
  } else {
    return setGammaXrandrRGB(monitor, red, green, blue);
  }
}

// === TEMPERATURE INCREMENT METHODS ===
bool BrightnessManager::increaseTemperature(int amount) {
  int newTemp = std::min(
      MAX_TEMPERATURE, static_cast<int>(temperature[primaryMonitor]) + amount);
  return setTemperature(newTemp);
}

bool BrightnessManager::increaseTemperature(const std::string &monitor,
                                            int amount) {
  int newTemp = std::min(MAX_TEMPERATURE,
                         static_cast<int>(temperature[monitor]) + amount);
  return setTemperature(monitor, newTemp);
}

// === BRIGHTNESS CONTROL METHODS ===
bool BrightnessManager::setBrightness(const std::string &monitor,
                                      double brightness) {
  brightness = std::clamp(brightness, 0.0, 2.0); // Allow over-brightening
  bool success = false;

  if (displayMethod == "wayland") {
#ifdef __WAYLAND__
    success = setBrightnessWayland(monitor, brightness);
#else
    error("Wayland support not compiled in!");
    return false;
#endif
  } else {
    success = setBrightnessGamma(monitor, brightness);
  }

  if (success) {
    this->brightness[monitor] = brightness;
  }

  return success;
}

bool BrightnessManager::setBrightness(double brightness) {
  brightness = std::clamp(brightness, 0.0, 2.0);
  bool success = true;

  for (const auto &monitor : getConnectedMonitors()) {
      success &= setBrightness(monitor, brightness);
  }
  return success;
}

// === BRIGHTNESS GETTERS ===
double BrightnessManager::getBrightness() {
  // Get brightness from primary/first monitor
  auto monitors = getConnectedMonitors();
  if (monitors.empty())
    return 0.0;
  return getBrightness(monitors[0]);
}

double BrightnessManager::getBrightness(int monitorIndex) {
  auto monitorName = getMonitor(monitorIndex);
  if (monitorName.empty()) return 0.0;
  return getBrightness(monitorName);
}
double BrightnessManager::getBrightness(const std::string &monitor) {
  if (WindowManagerDetector::IsX11()) {
    return getBrightnessGamma(monitor);
  }
#ifdef __WAYLAND__
  else if (WindowManagerDetector::IsWayland()) {
    // Wayland doesn't have a direct brightness query API
    // We'd need to track our last-set values or use sysfs
    auto it = brightness.find(monitor);
    if (it != brightness.end()) {
        return it->second;
    }
    return 1.0; // Default
  }
#endif
  return 0.0;
}

// === GAMMA GETTERS ===
BrightnessManager::RGBColor BrightnessManager::getGammaRGB() {
  auto monitors = getConnectedMonitors();
  if (monitors.empty())
    return {1.0, 1.0, 1.0};
  return getGammaRGB(monitors[0]);
}

// === TEMPERATURE GETTERS ===
int BrightnessManager::getTemperature() {
  auto monitors = getConnectedMonitors();
  if (monitors.empty())
    return 6500;
  return getTemperature(monitors[0]);
}
int BrightnessManager::getTemperature(int monitorIndex){
  auto monitorName = getMonitor(monitorIndex);
  if (monitorName.empty()) return 6500;
  return getTemperature(monitorName);
}
int BrightnessManager::getTemperature(const std::string &monitor) {
  // Get current RGB gamma values and reverse-engineer the temperature
  auto it = temperature.find(monitor);
  if (it != temperature.end()) {
      return it->second;
  }
  return 6500; // Default
}
bool BrightnessManager::decreaseGamma(int amount) {
    bool success = true;
    for (const auto &monitor : getConnectedMonitors()) {
        success = decreaseGamma(monitor, amount) && success;
    }
    return success;
}
bool BrightnessManager::decreaseGamma(const string& monitor, int amount) {
  RGBColor currentGamma = getGammaRGB(monitor);
  double reduction = amount / 1000.0;  // Convert to 0.0-1.0 range
  
  double newRed = std::max(0.1, currentGamma.red - reduction);
  double newGreen = std::max(0.1, currentGamma.green - reduction);
  double newBlue = std::max(0.1, currentGamma.blue - reduction);
  
  return setGammaRGB(monitor, newRed, newGreen, newBlue);
}

bool BrightnessManager::increaseGamma(int amount) {
    bool success = true;
    for (const auto &monitor : getConnectedMonitors()) {
        success = increaseGamma(monitor, amount) && success;
    }
    return success;
}

bool BrightnessManager::increaseGamma(const string& monitor, int amount) {
    try {
        RGBColor currentGamma = getGammaRGB(monitor);
        double factor = amount / 1000.0;
        
        double newRed = std::min(10.0, currentGamma.red + factor);
        double newGreen = std::min(10.0, currentGamma.green + factor);
        double newBlue = std::min(10.0, currentGamma.blue + factor);
        
        return setGammaRGB(monitor, newRed, newGreen, newBlue);
    } catch (const std::exception& e) {
        error("Error in increaseGamma: {}", e.what());
        return false;
    }
}

bool BrightnessManager::increaseShadowLift(int monitorIndex, int amount) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return setShadowLift(monitorName, shadowLift[monitorName] + amount);
}
bool BrightnessManager::decreaseShadowLift(int monitorIndex, int amount) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return setShadowLift(monitorName, shadowLift[monitorName] - amount);
}

// === SYSFS BRIGHTNESS FALLBACK ===
    double
    BrightnessManager::getBrightnessSysfs(const std::string &monitor) {
  // Try common backlight paths
  std::vector<std::string> backlight_paths = {
      "/sys/class/backlight/intel_backlight/brightness",
      "/sys/class/backlight/acpi_video0/brightness",
      "/sys/class/backlight/amdgpu_bl0/brightness"};

  for (const auto &brightness_path : backlight_paths) {
    std::ifstream brightness_file(brightness_path);
    std::ifstream max_brightness_file(
        brightness_path.substr(0, brightness_path.rfind('/')) +
        "/max_brightness");

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
double BrightnessManager::getGammaXrandr(const std::string &monitor) {
  if (!x11_display)
    return 1.0;

  DisplayManager::MonitorInfo monitorInfo = DisplayManager::GetMonitorByName(monitor);
  if (monitorInfo.id == 0 || monitorInfo.crtc_id == 0) {
      error("Monitor '{}' not found or is not active for getGammaXrandr.", monitor);
      return 1.0;
  }

  int gamma_size = XRRGetCrtcGammaSize(x11_display, monitorInfo.crtc_id);
  if (gamma_size > 0) {
    XRRCrtcGamma *crtc_gamma =
        XRRGetCrtcGamma(x11_display, monitorInfo.crtc_id);
    if (crtc_gamma) {
      // Calculate average gamma from the ramp (simplified)
      // In reality, you'd want to analyze the curve more carefully
      double red_gamma =
          (double)crtc_gamma->red[static_cast<int>(gamma_size / 2)] /
          65535.0;
      double green_gamma =
          (double)crtc_gamma->green[static_cast<int>(gamma_size / 2)] /
          65535.0;
      double blue_gamma =
          (double)crtc_gamma->blue[static_cast<int>(gamma_size / 2)] /
          65535.0;

      double gamma = (red_gamma + green_gamma + blue_gamma) / 3.0;

      XRRFreeGamma(crtc_gamma);
      return gamma;
    }
  }

  return 1.0;
}

// === RGB TO KELVIN CONVERSION ===
int BrightnessManager::rgbToKelvin(const RGBColor &rgb) const {
  // Convert RGB to XYZ color space
  double r = (rgb.red > 0.04045) ? pow((rgb.red + 0.055) / 1.055, 2.4) : rgb.red / 12.92;
  double g = (rgb.green > 0.04045) ? pow((rgb.green + 0.055) / 1.055, 2.4) : rgb.green / 12.92;
  double b = (rgb.blue > 0.04045) ? pow((rgb.blue + 0.055) / 1.055, 2.4) : rgb.blue / 12.92;

  // Convert to XYZ
  double X = r * 0.4124 + g * 0.3576 + b * 0.1805;
  double Y = r * 0.2126 + g * 0.7152 + b * 0.0722;
  double Z = r * 0.0193 + g * 0.1192 + b * 0.9505;

  // Calculate chromaticity coordinates
  double x = X / (X + Y + Z);
  double y = Y / (X + Y + Z);

  // Calculate CCT using McCamy's approximation
  double n = (x - 0.3320) / (0.1858 - y);
  double cct = 437 * pow(n, 3) + 3601 * pow(n, 2) + 6861 * n + 5517;

  return static_cast<int>(cct);
}

// === GET CURRENT RGB GAMMA VALUES ===
BrightnessManager::RGBColor
BrightnessManager::getGammaRGB(const std::string &monitor) {
  if(gammaRGB.count(monitor))   { 
    return gammaRGB[monitor];
  }
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

BrightnessManager::RGBColor
BrightnessManager::getGammaXrandrRGB(const std::string &monitor) {
  if (!x11_display)
    return {1.0, 1.0, 1.0};

  DisplayManager::MonitorInfo monitorInfo = DisplayManager::GetMonitorByName(monitor);
  if (monitorInfo.id == 0 || monitorInfo.crtc_id == 0) {
      error("Monitor '{}' not found or is not active for getGammaXrandrRGB.", monitor);
      return {1.0, 1.0, 1.0};
  }

  int gamma_size = XRRGetCrtcGammaSize(x11_display, monitorInfo.crtc_id);
  if (gamma_size > 0) {
    XRRCrtcGamma *crtc_gamma =
        XRRGetCrtcGamma(x11_display, monitorInfo.crtc_id);
    if (crtc_gamma) {
      // Get RGB values from middle of gamma ramp
      int mid = gamma_size / 2;
      RGBColor rgb;
      rgb.red = (double)crtc_gamma->red[mid] / 65535.0;
      rgb.green = (double)crtc_gamma->green[mid] / 65535.0;
      rgb.blue = (double)crtc_gamma->blue[mid] / 65535.0;

      XRRFreeGamma(crtc_gamma);
      return rgb;
    }
  }

  return {1.0, 1.0, 1.0};
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
  int newTemp = std::max(
      MIN_TEMPERATURE, static_cast<int>(temperature[primaryMonitor]) - amount);
  return setTemperature(newTemp);
}

bool BrightnessManager::decreaseTemperature(const std::string &monitor,
                                            int amount) {
  int newTemp = std::max(MIN_TEMPERATURE,
                         static_cast<int>(temperature[monitor]) - amount);
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
  bool success = true;
  for (const auto &monitor : getConnectedMonitors()) {
    if (!setBrightnessWayland(monitor, brightness)) {
      error("Failed to set brightness for monitor: " + monitor);
      success = false;
    }
  }
  return success;
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
  bool success = true;
  for (const auto &monitor : getConnectedMonitors()) {
    if (!setGammaWaylandRGB(monitor, red, green, blue)) {
      error("Failed to set gamma for monitor: " + monitor);
      success = false;
    }
  }
  return success;
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

// === INDEX-BASED OVERLOADS IMPLEMENTATION ===

bool BrightnessManager::setBrightness(int monitorIndex, double brightness) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return setBrightness(monitorName, brightness);
}

bool BrightnessManager::setGammaRGB(int monitorIndex, double red, double green, double blue) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return setGammaRGB(monitorName, red, green, blue);
}

bool BrightnessManager::setTemperature(int monitorIndex, int kelvin) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return setTemperature(monitorName, kelvin);
}

bool BrightnessManager::setShadowLift(int monitorIndex, double lift) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return setShadowLift(monitorName, lift);
}

bool BrightnessManager::decreaseGamma(int monitorIndex, int amount) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return decreaseGamma(monitorName, amount);
}

bool BrightnessManager::increaseGamma(int monitorIndex, int amount) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return increaseGamma(monitorName, amount);
}

bool BrightnessManager::setBrightnessAndRGB(int monitorIndex, double brightness, double red, double green, double blue) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return setBrightnessAndRGB(monitorName, brightness, red, green, blue);
}

bool BrightnessManager::setBrightnessAndTemperature(int monitorIndex, double brightness, int kelvin) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return setBrightnessAndTemperature(monitorName, brightness, kelvin);
}

bool BrightnessManager::setBrightnessAndShadowLift(int monitorIndex, double brightness, double shadowLift) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return setBrightnessAndShadowLift(monitorName, brightness, shadowLift);
}

bool BrightnessManager::increaseBrightness(int monitorIndex, double amount) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return increaseBrightness(monitorName, amount);
}

bool BrightnessManager::decreaseBrightness(int monitorIndex, double amount) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return decreaseBrightness(monitorName, amount);
}

bool BrightnessManager::increaseTemperature(int monitorIndex, int amount) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return increaseTemperature(monitorName, amount);
}

bool BrightnessManager::decreaseTemperature(int monitorIndex, int amount) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return decreaseTemperature(monitorName, amount);
}

bool BrightnessManager::switchToDay(int monitorIndex) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return switchToDay(monitorName);
}

bool BrightnessManager::switchToNight(int monitorIndex) {
    auto monitorName = getMonitor(monitorIndex);
    if (monitorName.empty()) return false;
    return switchToNight(monitorName);
}

bool BrightnessManager::setBrightnessWayland(const std::string& output, double brightness) {
    // This is a stub implementation - will be filled in later
    return true;  // Return success for now
}

bool BrightnessManager::setGammaWaylandRGB(const std::string& output, double r, double g, double b) {
    // This is a stub implementation - will be filled in later
    return true;  // Return success for now
}

} // namespace havel