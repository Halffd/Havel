/*
 * MapManagerService.cpp
 *
 * Input mapping service implementation (stub).
 */
#include "MapManagerService.hpp"
// MapManagerService is a stub - full implementation requires matching MapManager API

namespace havel::host {

MapManagerService::MapManagerService(std::shared_ptr<IO> io) {
    (void)io;  // Stub doesn't use IO yet
}

MapManagerService::~MapManagerService() {
}

bool MapManagerService::map(const std::string& sourceKey, const std::string& targetKey, int id) {
    (void)sourceKey; (void)targetKey; (void)id;
    return false;  // Stub
}

bool MapManagerService::remap(const std::string& key1, const std::string& key2) {
    (void)key1; (void)key2;
    return false;  // Stub
}

bool MapManagerService::unmap(const std::string& sourceKey) {
    (void)sourceKey;
    return false;  // Stub
}

void MapManagerService::clearAll() {
}

bool MapManagerService::enableAutofire(const std::string& sourceKey, int intervalMs) {
    (void)sourceKey; (void)intervalMs;
    return false;  // Stub
}

bool MapManagerService::disableAutofire(const std::string& sourceKey) {
    (void)sourceKey;
    return false;  // Stub
}

bool MapManagerService::setAutofireRate(const std::string& sourceKey, int rateMs) {
    (void)sourceKey; (void)rateMs;
    return false;  // Stub
}

bool MapManagerService::enableTurbo(const std::string& sourceKey, int intervalMs) {
    (void)sourceKey; (void)intervalMs;
    return false;  // Stub
}

bool MapManagerService::disableTurbo(const std::string& sourceKey) {
    (void)sourceKey;
    return false;  // Stub
}

bool MapManagerService::createProfile(const std::string& name) {
    (void)name;
    return false;  // Stub
}

bool MapManagerService::switchProfile(const std::string& name) {
    (void)name;
    return false;  // Stub
}

std::string MapManagerService::getCurrentProfile() const {
    return "";  // Stub
}

std::vector<std::string> MapManagerService::getProfileNames() const {
    return {};  // Stub
}

bool MapManagerService::addConditionalMapping(const std::string& sourceKey,
                                               const std::string& targetKey,
                                               const std::string& windowPattern) {
    (void)sourceKey; (void)targetKey; (void)windowPattern;
    return false;  // Stub
}

bool MapManagerService::isMapped(const std::string& sourceKey) const {
    (void)sourceKey;
    return false;  // Stub
}

int MapManagerService::getMappingCount() const {
    return 0;  // Stub
}

} // namespace havel::host
