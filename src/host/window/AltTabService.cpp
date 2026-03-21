/*
 * AltTabService.cpp
 *
 * Alt-Tab window switcher service implementation (stub).
 */
#include "AltTabService.hpp"
// AltTabService is a stub - full implementation requires AltTabWindow

namespace havel::host {

AltTabService::AltTabService() {
}

AltTabService::~AltTabService() {
}

void AltTabService::show() {
}

void AltTabService::hide() {
}

void AltTabService::toggle() {
}

void AltTabService::next() {
}

void AltTabService::previous() {
}

void AltTabService::select() {
}

void AltTabService::refresh() {
}

std::vector<AltTabInfo> AltTabService::getWindows() const {
    return {};  // Stub
}

int AltTabService::getWindowCount() const {
    return 0;  // Stub
}

void AltTabService::setThumbnailSize(int width, int height) {
    (void)width; (void)height;
}

int AltTabService::getThumbnailWidth() const {
    return 100;  // Stub
}

int AltTabService::getThumbnailHeight() const {
    return 75;  // Stub
}

void AltTabService::setMaxVisibleWindows(int count) {
    (void)count;
}

int AltTabService::getMaxVisibleWindows() const {
    return 10;  // Stub
}

void AltTabService::setAnimationsEnabled(bool enabled) {
    (void)enabled;
}

bool AltTabService::isAnimationsEnabled() const {
    return true;  // Stub
}

} // namespace havel::host
