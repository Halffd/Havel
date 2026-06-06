#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace havel::modules::zoom {

enum class ZoomFilter : int { Nearest = 0, Bilinear = 1, Sharpen = 2, Lanczos = 3 };

struct ZoomRegion {
	int x = 0;
	int y = 0;
	int width = 1920;
	int height = 1080;
};

class IZoomBackend {
public:
	virtual ~IZoomBackend() = default;
	virtual bool zoomIn(float step) = 0;
	virtual bool zoomOut(float step) = 0;
	virtual bool zoomTo(float level) = 0;
	virtual bool zoomReset() = 0;
	virtual float getZoomLevel() = 0;
	virtual bool isAvailable() = 0;
	virtual std::string name() const = 0;
};

class KWinZoomBackend : public IZoomBackend {
public:
	bool zoomIn(float step) override;
	bool zoomOut(float step) override;
	bool zoomTo(float level) override;
	bool zoomReset() override;
	float getZoomLevel() override;
	bool isAvailable() override;
	std::string name() const override { return "KWin"; }
};

class HyprlandZoomBackend : public IZoomBackend {
public:
	bool zoomIn(float step) override;
	bool zoomOut(float step) override;
	bool zoomTo(float level) override;
	bool zoomReset() override;
	float getZoomLevel() override;
	bool isAvailable() override;
	std::string name() const override { return "Hyprland"; }
private:
	std::atomic<float> currentLevel_{1.0f};
};

class GNOMEMagnifierBackend : public IZoomBackend {
public:
	bool zoomIn(float step) override;
	bool zoomOut(float step) override;
	bool zoomTo(float level) override;
	bool zoomReset() override;
	float getZoomLevel() override;
	bool isAvailable() override;
	std::string name() const override { return "GNOME"; }
private:
	std::atomic<float> currentLevel_{1.0f};
	bool activate();
	bool deactivate();
	bool setMagFactor(float x, float y);
};

class CinnamonZoomBackend : public IZoomBackend {
public:
	bool zoomIn(float step) override;
	bool zoomOut(float step) override;
	bool zoomTo(float level) override;
	bool zoomReset() override;
	float getZoomLevel() override;
	bool isAvailable() override;
	std::string name() const override { return "Cinnamon"; }
private:
	std::atomic<float> currentLevel_{1.0f};
};

class GenericZoomBackend : public IZoomBackend {
public:
	bool zoomIn(float step) override;
	bool zoomOut(float step) override;
	bool zoomTo(float level) override;
	bool zoomReset() override;
	float getZoomLevel() override;
	bool isAvailable() override;
	std::string name() const override { return "Generic"; }
private:
	std::atomic<float> currentLevel_{1.0f};
};

class ZoomService {
public:
	static ZoomService& instance() {
		static ZoomService inst;
		return inst;
	}

	bool start();
	void stop();
	bool isRunning() const { return running_.load(); }

	void setScale(float s) { scale_.store(s); }
	float getScale() const { return scale_.load(); }

	void setRegion(int x, int y, int w, int h);
	ZoomRegion getRegion() const;

	void setFilter(ZoomFilter f) { filter_.store(f); }
	ZoomFilter getFilter() const { return filter_.load(); }

	void setLocked(bool l) { locked_.store(l); }
	bool isLocked() const { return locked_.load(); }

	void setFollowCursor(bool f) { followCursor_.store(f); }
	bool isFollowCursor() const { return followCursor_.load(); }

	void setColorInvert(bool i) { colorInvert_.store(i); }
	bool isColorInvert() const { return colorInvert_.load(); }

	void setBrightness(float b) { brightness_.store(b); }
	float getBrightness() const { return brightness_.load(); }

	void setContrast(float c) { contrast_.store(c); }
	float getContrast() const { return contrast_.load(); }

	bool zoomIn(float step = 0.2f);
	bool zoomOut(float step = 0.2f);
	bool zoomTo(float level);
	bool zoomReset();
	float getZoomLevel();

	std::vector<int> getPixelColor(int x, int y);
	std::string getPixelColorHex(int x, int y);

	int getScreenWidth() const;
	int getScreenHeight() const;

	std::string getBackendName() const;

private:
	ZoomService();
	~ZoomService();

	void detectBackend();
	IZoomBackend* backend();

	std::atomic<bool> running_{false};
	std::atomic<float> scale_{2.0f};
	std::atomic<ZoomFilter> filter_{ZoomFilter::Bilinear};
	std::atomic<bool> locked_{false};
	std::atomic<bool> followCursor_{true};
	std::atomic<bool> colorInvert_{false};
	std::atomic<float> brightness_{1.0f};
	std::atomic<float> contrast_{1.0f};

	mutable std::mutex regionMutex_;
	ZoomRegion region_;

	mutable std::mutex backendMutex_;
	std::unique_ptr<IZoomBackend> backend_;
};

} // namespace havel::modules::zoom
