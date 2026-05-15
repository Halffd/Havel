#include "MapManagerService.hpp"
#include "core/io/MapManager.hpp"

namespace havel::host {

MapManagerService::MapManagerService(std::shared_ptr<IO> io)
    : m_manager(std::make_shared<havel::MapManager>(io.get())) {}

MapManagerService::~MapManagerService() {}

bool MapManagerService::map(const std::string& sourceKey, const std::string& targetKey, int id) {
  auto* profile = m_manager->GetActiveProfile();
  if (!profile) {
    havel::Profile p;
    p.id = "default";
    p.name = "Default";
    m_manager->AddProfile(p);
    m_manager->SetActiveProfile("default");
    profile = m_manager->GetActiveProfile();
  }

  havel::Mapping mapping;
  mapping.id = std::to_string(id);
  mapping.sourceKey = sourceKey;
  mapping.sourceKeys = {sourceKey};
  mapping.targetKeys = {targetKey};
  mapping.mappingType = havel::MappingType::KeyToKey;
  mapping.actionType = havel::ActionType::Hold;

  m_manager->AddMapping(profile->id, mapping);
  return true;
}

bool MapManagerService::remap(const std::string& key1, const std::string& key2) {
  if (!map(key1, key2)) return false;
  return map(key2, key1);
}

bool MapManagerService::unmap(const std::string& sourceKey) {
  auto* profile = m_manager->GetActiveProfile();
  if (!profile) return false;
  auto* mapping = profile->FindMapping(sourceKey);
  if (!mapping) return false;
  m_manager->RemoveMapping(profile->id, mapping->id);
  return true;
}

void MapManagerService::clearAll() {
  m_manager->ClearAllMappings();
}

bool MapManagerService::enableAutofire(const std::string& sourceKey, int intervalMs) {
  auto* profile = m_manager->GetActiveProfile();
  if (!profile) return false;
  auto* mapping = profile->FindMapping(sourceKey);
  if (!mapping) return false;
  mapping->autofire = true;
  mapping->autofireInterval = intervalMs;
  m_manager->UpdateMapping(profile->id, *mapping);
  return true;
}

bool MapManagerService::disableAutofire(const std::string& sourceKey) {
  auto* profile = m_manager->GetActiveProfile();
  if (!profile) return false;
  auto* mapping = profile->FindMapping(sourceKey);
  if (!mapping) return false;
  mapping->autofire = false;
  m_manager->UpdateMapping(profile->id, *mapping);
  return true;
}

bool MapManagerService::setAutofireRate(const std::string& sourceKey, int rateMs) {
  auto* profile = m_manager->GetActiveProfile();
  if (!profile) return false;
  auto* mapping = profile->FindMapping(sourceKey);
  if (!mapping) return false;
  m_manager->SetAutofireRate(profile->id, mapping->id, rateMs);
  return true;
}

bool MapManagerService::enableTurbo(const std::string& sourceKey, int intervalMs) {
  auto* profile = m_manager->GetActiveProfile();
  if (!profile) return false;
  auto* mapping = profile->FindMapping(sourceKey);
  if (!mapping) return false;
  mapping->turbo = true;
  mapping->turboInterval = intervalMs;
  m_manager->UpdateMapping(profile->id, *mapping);
  return true;
}

bool MapManagerService::disableTurbo(const std::string& sourceKey) {
  auto* profile = m_manager->GetActiveProfile();
  if (!profile) return false;
  auto* mapping = profile->FindMapping(sourceKey);
  if (!mapping) return false;
  mapping->turbo = false;
  m_manager->UpdateMapping(profile->id, *mapping);
  return true;
}

bool MapManagerService::createProfile(const std::string& name) {
  havel::Profile p;
  p.id = name;
  p.name = name;
  m_manager->AddProfile(p);
  return true;
}

bool MapManagerService::switchProfile(const std::string& name) {
  if (!m_manager->GetProfile(name)) return false;
  m_manager->SetActiveProfile(name);
  m_manager->ApplyProfile(name);
  return true;
}

std::string MapManagerService::getCurrentProfile() const {
  return m_manager->GetActiveProfileId();
}

std::vector<std::string> MapManagerService::getProfileNames() const {
  return m_manager->GetProfileIds();
}

bool MapManagerService::addConditionalMapping(const std::string& sourceKey,
                                              const std::string& targetKey,
                                              const std::string& windowPattern) {
  auto* profile = m_manager->GetActiveProfile();
  if (!profile) {
    havel::Profile p;
    p.id = "default";
    p.name = "Default";
    m_manager->AddProfile(p);
    m_manager->SetActiveProfile("default");
    profile = m_manager->GetActiveProfile();
  }

  havel::Mapping mapping;
  mapping.id = sourceKey + "->" + targetKey;
  mapping.sourceKey = sourceKey;
  mapping.sourceKeys = {sourceKey};
  mapping.targetKeys = {targetKey};
  mapping.mappingType = havel::MappingType::KeyToKey;
  mapping.actionType = havel::ActionType::Hold;

  havel::MappingCondition cond;
  cond.type = havel::ConditionType::WindowTitle;
  cond.pattern = windowPattern;
  mapping.conditions.push_back(cond);

  m_manager->AddMapping(profile->id, mapping);
  return true;
}

bool MapManagerService::isMapped(const std::string& sourceKey) const {
  auto* profile = m_manager->GetActiveProfile();
  if (!profile) return false;
  return profile->FindMapping(sourceKey) != nullptr;
}

int MapManagerService::getMappingCount() const {
  auto* profile = m_manager->GetActiveProfile();
  if (!profile) return 0;
  return static_cast<int>(profile->mappings.size());
}

} // namespace havel::host
