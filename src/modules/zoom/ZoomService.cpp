#include "ZoomService.hpp"
#include "utils/Logger.hpp"
#include "utils/SafeExec.hpp"
#include "core/window/WindowManagerDetector.hpp"
#include "core/window/CompositorBridge.hpp"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <unistd.h>

#ifdef HAVE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

namespace havel::modules::zoom {

// --- KWinZoomBackend ---

bool KWinZoomBackend::zoomIn(float) {
	return CompositorBridge::SendKWinZoomCommand(
		"org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.zoomInDBus");
}

bool KWinZoomBackend::zoomOut(float) {
	return CompositorBridge::SendKWinZoomCommand(
		"org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.zoomOutDBus");
}

bool KWinZoomBackend::zoomTo(float level) {
	std::string cmd = "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.zoomToValueDBus " +
		std::to_string(level);
	return CompositorBridge::SendKWinZoomCommand(cmd);
}

bool KWinZoomBackend::zoomReset() {
	return CompositorBridge::SendKWinZoomCommand(
		"org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.resetZoomDBus");
}

float KWinZoomBackend::getZoomLevel() {
	auto out = CompositorBridge::SendKWinZoomCommandWithOutput(
		"org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.getZoomLevelDBus");
	if (out.empty()) return 1.0f;
	try { return std::stof(out); }
	catch (...) { return 1.0f; }
}

bool KWinZoomBackend::isAvailable() {
	return CompositorBridge::IsKDERunning();
}

// --- HyprlandZoomBackend ---

bool HyprlandZoomBackend::zoomIn(float step) {
	float level = currentLevel_.load() + step;
	return zoomTo(level);
}

bool HyprlandZoomBackend::zoomOut(float step) {
	float level = currentLevel_.load() - step;
	return zoomTo(level);
}

bool HyprlandZoomBackend::zoomTo(float level) {
	if (level < 1.0f) level = 1.0f;
	if (level > 10.0f) level = 10.0f;
	auto result = havel::utils::execSync(
		{"hyprctl", "keyword", "cursor:zoom_factor", std::to_string(level)});
	bool ok = result && result->exitCode == 0;
	if (ok) currentLevel_.store(level);
	return ok;
}

bool HyprlandZoomBackend::zoomReset() {
	return zoomTo(1.0f);
}

float HyprlandZoomBackend::getZoomLevel() {
	return currentLevel_.load();
}

bool HyprlandZoomBackend::isAvailable() {
	return havel::utils::processExistsByName("hyprland") ||
	       havel::utils::processExistsByName("Hyprland");
}

// --- GNOMEMagnifierBackend ---

static bool gnomeMagnifierCall(const std::string &method,
                               const std::vector<std::string> &args = {}) {
	std::vector<std::string> cmd = {"gdbus", "call", "--session",
		"--dest", "org.gnome.Magnifier",
		"--object-path", "/org/gnome/Magnifier",
		"--method", method};
	for (auto &a : args) cmd.push_back(a);
	auto r = havel::utils::execSync(cmd);
	return r && r->exitCode == 0;
}

static std::optional<std::string> gnomeMagnifierCallWithOutput(const std::string &method,
                                                                const std::vector<std::string> &args = {}) {
	std::vector<std::string> cmd = {"gdbus", "call", "--session",
		"--dest", "org.gnome.Magnifier",
		"--object-path", "/org/gnome/Magnifier",
		"--method", method};
	for (auto &a : args) cmd.push_back(a);
	return havel::utils::execCapture(cmd);
}

static bool gnomeZoomRegionCall(const std::string &path,
                                const std::string &method,
                                const std::vector<std::string> &args = {}) {
	std::vector<std::string> cmd = {"gdbus", "call", "--session",
		"--dest", "org.gnome.Magnifier",
		"--object-path", path,
		"--method", method};
	for (auto &a : args) cmd.push_back(a);
	auto r = havel::utils::execSync(cmd);
	return r && r->exitCode == 0;
}

static std::optional<std::string> gnomeZoomRegionCallWithOutput(const std::string &path,
                                                                 const std::string &method,
                                                                 const std::vector<std::string> &args = {}) {
	std::vector<std::string> cmd = {"gdbus", "call", "--session",
		"--dest", "org.gnome.Magnifier",
		"--object-path", path,
		"--method", method};
	for (auto &a : args) cmd.push_back(a);
	return havel::utils::execCapture(cmd);
}

bool GNOMEMagnifierBackend::activate() {
	return gnomeMagnifierCall("org.gnome.Magnifier.setActive", {"true"});
}

bool GNOMEMagnifierBackend::deactivate() {
	return gnomeMagnifierCall("org.gnome.Magnifier.setActive", {"false"});
}

bool GNOMEMagnifierBackend::setMagFactor(float x, float y) {
	return gnomeMagnifierCall("org.gnome.Magnifier.setMagFactor",
		{std::to_string(x), std::to_string(y)});
}

bool GNOMEMagnifierBackend::zoomIn(float step) {
	float level = currentLevel_.load() + step;
	return zoomTo(level);
}

bool GNOMEMagnifierBackend::zoomOut(float step) {
	float level = currentLevel_.load() - step;
	return zoomTo(level);
}

bool GNOMEMagnifierBackend::zoomTo(float level) {
	if (level < 1.0f) level = 1.0f;
	if (level > 15.0f) level = 15.0f;
	if (level <= 1.0f) return zoomReset();
	if (!activate()) return false;
	bool ok = setMagFactor(level, level);
	if (ok) currentLevel_.store(level);
	return ok;
}

bool GNOMEMagnifierBackend::zoomReset() {
	bool ok = deactivate();
	if (ok) currentLevel_.store(1.0f);
	return ok;
}

float GNOMEMagnifierBackend::getZoomLevel() {
	auto out = gnomeMagnifierCallWithOutput("org.gnome.Magnifier.isActive");
	if (!out || out->find("true") == std::string::npos) return 1.0f;
	out = gnomeZoomRegionCallWithOutput(
		"/org/gnome/Magnifier/ZoomRegion/zoomer0",
		"org.gnome.Magnifier.ZoomRegion.getMagFactor");
	if (!out || out->empty()) return currentLevel_.load();
	auto lparen = out->find('(');
	auto comma = out->find(',');
	if (lparen == std::string::npos || comma == std::string::npos) return currentLevel_.load();
	std::string val = out->substr(lparen + 1, comma - lparen - 1);
	try { return std::stof(val); }
	catch (...) { return currentLevel_.load(); }
}

bool GNOMEMagnifierBackend::isAvailable() {
	auto r = gnomeMagnifierCallWithOutput("org.gnome.Magnifier.isActive");
	return r.has_value();
}

// --- CinnamonZoomBackend ---

static bool cinnamonZoomCall(const std::string &method,
                             const std::vector<std::string> &args = {}) {
	std::vector<std::string> cmd = {"gdbus", "call", "--session",
		"--dest", "org.cinnamon.Muffin.Zoom",
		"--object-path", "/org/cinnamon/Muffin/Zoom",
		"--method", method};
	for (auto &a : args) cmd.push_back(a);
	auto r = havel::utils::execSync(cmd);
	return r && r->exitCode == 0;
}

static std::optional<std::string> cinnamonZoomCallWithOutput(const std::string &method,
                                                              const std::vector<std::string> &args = {}) {
	std::vector<std::string> cmd = {"gdbus", "call", "--session",
		"--dest", "org.cinnamon.Muffin.Zoom",
		"--object-path", "/org/cinnamon/Muffin/Zoom",
		"--method", method};
	for (auto &a : args) cmd.push_back(a);
	return havel::utils::execCapture(cmd);
}

bool CinnamonZoomBackend::zoomIn(float step) {
	float level = currentLevel_.load() + step;
	if (level < 1.0f) level = 1.0f;
	if (level > 16.0f) level = 16.0f;
	bool ok = cinnamonZoomCall("org.cinnamon.Muffin.Zoom.ZoomIn", {std::to_string(step)});
	if (ok) currentLevel_.store(level);
	return ok;
}

bool CinnamonZoomBackend::zoomOut(float step) {
	float level = currentLevel_.load() - step;
	if (level < 1.0f) level = 1.0f;
	bool ok = cinnamonZoomCall("org.cinnamon.Muffin.Zoom.ZoomOut", {std::to_string(step)});
	if (ok) currentLevel_.store(level);
	return ok;
}

bool CinnamonZoomBackend::zoomTo(float level) {
	if (level < 1.0f) level = 1.0f;
	if (level > 16.0f) level = 16.0f;
	bool ok = cinnamonZoomCall("org.cinnamon.Muffin.Zoom.SetZoomLevel", {std::to_string(level)});
	if (ok) currentLevel_.store(level);
	return ok;
}

bool CinnamonZoomBackend::zoomReset() {
	bool ok = cinnamonZoomCall("org.cinnamon.Muffin.Zoom.ResetZoom");
	if (ok) currentLevel_.store(1.0f);
	return ok;
}

float CinnamonZoomBackend::getZoomLevel() {
	auto out = cinnamonZoomCallWithOutput("org.cinnamon.Muffin.Zoom.GetZoomLevel");
	if (!out || out->empty()) return currentLevel_.load();
	try { return std::stof(*out); }
	catch (...) { return currentLevel_.load(); }
}

bool CinnamonZoomBackend::isAvailable() {
	auto r = cinnamonZoomCallWithOutput("org.cinnamon.Muffin.Zoom.GetZoomActive");
	return r.has_value();
}

// --- GenericZoomBackend (keyboard fallback) ---

static bool sendKeys(const std::string &keys) {
	return havel::utils::execDetached({"xdotool", "key", keys});
}

bool GenericZoomBackend::zoomIn(float) {
	bool ok = sendKeys("ctrl+super+plus");
	if (ok) {
		float level = currentLevel_.load() + 0.2f;
		currentLevel_.store(level);
	}
	return ok;
}

bool GenericZoomBackend::zoomOut(float) {
	bool ok = sendKeys("ctrl+super+minus");
	if (ok) {
		float level = currentLevel_.load() - 0.2f;
		if (level < 1.0f) level = 1.0f;
		currentLevel_.store(level);
	}
	return ok;
}

bool GenericZoomBackend::zoomTo(float level) {
	if (level <= 1.0f) return zoomReset();
	float current = currentLevel_.load();
	float diff = level - current;
	if (diff > 0) {
		int steps = static_cast<int>(std::ceil(diff / 0.2f));
		for (int i = 0; i < steps; i++) {
			sendKeys("ctrl+super+plus");
		}
	} else {
		int steps = static_cast<int>(std::ceil(-diff / 0.2f));
		for (int i = 0; i < steps; i++) {
			sendKeys("ctrl+super+minus");
		}
	}
	currentLevel_.store(level);
	return true;
}

bool GenericZoomBackend::zoomReset() {
	bool ok = sendKeys("ctrl+super+0");
	if (ok) currentLevel_.store(1.0f);
	return ok;
}

float GenericZoomBackend::getZoomLevel() {
	return currentLevel_.load();
}

bool GenericZoomBackend::isAvailable() {
	return havel::utils::processExistsByName("xdotool");
}

// --- ZoomService ---

ZoomService::ZoomService() {
	detectBackend();
}

ZoomService::~ZoomService() {
	stop();
}

void ZoomService::detectBackend() {
	std::lock_guard<std::mutex> lock(backendMutex_);
	if (backend_) return;

	// Try KWin first (works on both X11 and Wayland KDE)
	KWinZoomBackend kwin;
	if (kwin.isAvailable()) {
		backend_ = std::make_unique<KWinZoomBackend>();
		return;
	}

	bool isWayland = WindowManagerDetector::IsWayland();
	auto wmName = WindowManagerDetector::GetWMName();

	// Cinnamon (org.cinnamon.Muffin.Zoom D-Bus)
	if (wmName.find("Cinnamon") != std::string::npos ||
	    wmName.find("cinnamon") != std::string::npos ||
	    havel::utils::processExistsByName("cinnamon")) {
		CinnamonZoomBackend cinnamon;
		if (cinnamon.isAvailable()) {
			backend_ = std::make_unique<CinnamonZoomBackend>();
			return;
		}
	}

	if (isWayland) {
		// Hyprland
		HyprlandZoomBackend hypr;
		if (hypr.isAvailable()) {
			backend_ = std::make_unique<HyprlandZoomBackend>();
			return;
		}

		// GNOME/Mutter magnifier
		if (wmName.find("GNOME") != std::string::npos ||
		    wmName.find("Mutter") != std::string::npos ||
		    havel::utils::processExistsByName("gnome-shell")) {
			GNOMEMagnifierBackend gnome;
			if (gnome.isAvailable()) {
				backend_ = std::make_unique<GNOMEMagnifierBackend>();
				return;
			}
		}
	} else {
		// X11 GNOME/Mutter
		if (wmName.find("GNOME") != std::string::npos ||
		    wmName.find("Mutter") != std::string::npos) {
			GNOMEMagnifierBackend gnome;
			if (gnome.isAvailable()) {
				backend_ = std::make_unique<GNOMEMagnifierBackend>();
				return;
			}
		}
	}

	// Generic xdotool fallback
	GenericZoomBackend generic;
	if (generic.isAvailable()) {
		backend_ = std::make_unique<GenericZoomBackend>();
		return;
	}

	havel::Logger::getInstance().warning("ZoomService: no zoom backend available");
}

IZoomBackend* ZoomService::backend() {
	std::lock_guard<std::mutex> lock(backendMutex_);
	return backend_.get();
}

bool ZoomService::checkUinputPrereqs() {
    if (access("/dev/uinput", F_OK) != 0) {
        havel::Logger::getInstance().error("ZoomService: /dev/uinput not found — kernel uinput module required");
        return false;
    }
    if (access("/dev/uinput", W_OK) != 0) {
        havel::Logger::getInstance().error("ZoomService: no write access to /dev/uinput — run: sudo usermod -aG input $USER");
        return false;
    }
    return true;
}

bool ZoomService::start() {
    if (running_.load()) return true;
    if (!backend()) {
        havel::Logger::getInstance().error("ZoomService: no backend available, cannot start");
        return false;
    }
    auto *b = backend();
    if (b && b->name() == "Generic" && !checkUinputPrereqs()) {
        return false;
    }
    running_.store(true);
    havel::Logger::getInstance().debug("ZoomService: started with " + backend()->name() + " backend");
    return true;
}

void ZoomService::stop() {
	if (!running_.load()) return;
	running_.store(false);
	havel::Logger::getInstance().debug("ZoomService: stopped");
}

void ZoomService::setRegion(int x, int y, int w, int h) {
	std::lock_guard<std::mutex> lock(regionMutex_);
	region_.x = x;
	region_.y = y;
	region_.width = w;
	region_.height = h;
}

ZoomRegion ZoomService::getRegion() const {
	std::lock_guard<std::mutex> lock(regionMutex_);
	return region_;
}

bool ZoomService::zoomIn(float step) {
	auto *b = backend();
	if (!b) return false;
	return b->zoomIn(step);
}

bool ZoomService::zoomOut(float step) {
	auto *b = backend();
	if (!b) return false;
	return b->zoomOut(step);
}

bool ZoomService::zoomTo(float level) {
	auto *b = backend();
	if (!b) return false;
	return b->zoomTo(level);
}

bool ZoomService::zoomReset() {
	auto *b = backend();
	if (!b) return false;
	return b->zoomReset();
}

float ZoomService::getZoomLevel() {
	auto *b = backend();
	if (!b) return 1.0f;
	return b->getZoomLevel();
}

std::string ZoomService::getBackendName() const {
	std::lock_guard<std::mutex> lock(backendMutex_);
	if (!backend_) return "none";
	return backend_->name();
}

int ZoomService::getScreenWidth() const {
#ifdef HAVE_X11
	Display *disp = XOpenDisplay(nullptr);
	if (disp) {
		int screen = DefaultScreen(disp);
		int width = DisplayWidth(disp, screen);
		XCloseDisplay(disp);
		return width;
	}
#endif
	std::lock_guard<std::mutex> lock(regionMutex_);
	return region_.width;
}

int ZoomService::getScreenHeight() const {
#ifdef HAVE_X11
	Display *disp = XOpenDisplay(nullptr);
	if (disp) {
		int screen = DefaultScreen(disp);
		int height = DisplayHeight(disp, screen);
		XCloseDisplay(disp);
		return height;
	}
#endif
	std::lock_guard<std::mutex> lock(regionMutex_);
	return region_.height;
}

std::vector<int> ZoomService::getPixelColor(int x, int y) {
#ifdef HAVE_X11
	Display *disp = XOpenDisplay(nullptr);
	if (disp) {
		XColor color;
		XImage *img = XGetImage(disp, RootWindow(disp, DefaultScreen(disp)),
			x, y, 1, 1, AllPlanes, ZPixmap);
		if (img) {
			unsigned long pixel = XGetPixel(img, 0, 0);
			color.pixel = pixel;
			XQueryColor(disp, DefaultColormap(disp, DefaultScreen(disp)), &color);
			XDestroyImage(img);
			XCloseDisplay(disp);
			return {static_cast<int>(color.red >> 8),
				static_cast<int>(color.green >> 8),
				static_cast<int>(color.blue >> 8)};
		}
		XCloseDisplay(disp);
	}
#endif
	(void)x;
	(void)y;
	return {0, 0, 0};
}

std::string ZoomService::getPixelColorHex(int x, int y) {
	auto rgb = getPixelColor(x, y);
	char buf[8];
	std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
	return std::string(buf);
}

} // namespace havel::modules::zoom
