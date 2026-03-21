/*
 * MediaService.cpp
 *
 * Media playback service implementation (stub).
 */
#include "MediaService.hpp"
// MediaService is a stub - full implementation requires MPVController

namespace havel::host {

MediaService::MediaService() {
}

MediaService::~MediaService() {
}

bool MediaService::play(const std::string& path) {
    (void)path;
    return false;  // Stub
}

void MediaService::pause() {}
void MediaService::resume() {}
void MediaService::togglePause() {}
void MediaService::stop() {}

bool MediaService::isPlaying() const { return false; }
bool MediaService::isPaused() const { return false; }

int MediaService::getVolume() const { return 50; }
void MediaService::setVolume(int volume) { (void)volume; }
void MediaService::setMute(bool mute) { (void)mute; }
bool MediaService::isMuted() const { return false; }

double MediaService::getPosition() const { return 0.0; }
void MediaService::setPosition(double seconds) { (void)seconds; }
double MediaService::getDuration() const { return 0.0; }
double MediaService::getProgress() const { return 0.0; }

void MediaService::next() {}
void MediaService::previous() {}
std::string MediaService::getTitle() const { return ""; }

} // namespace havel::host
