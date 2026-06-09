/*
 * ModeService.hpp
 *
 * Pure C++ mode service - no VM, no interpreter, no HavelValue.
 * This is the business logic layer for mode operations.
 * 
 * Uses CallbackId for all callbacks - VM owns closures, Service stores IDs only.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel { class ModeManager; class IHostAPI; }
namespace havel::compiler { class VM; using CallbackId = uint32_t; }

namespace havel::host {

class ModeService {
public:
ModeService(havel::IHostAPI* hostAPI, havel::ModeManager* manager);
~ModeService() = default;

std::string getCurrentMode() const;
std::string getPreviousMode() const;
void setMode(const std::string& modeName);

    void defineMode(const std::string& name,
                    compiler::CallbackId enterId,
                    compiler::CallbackId exitId);

private:
havel::IHostAPI* m_hostAPI;
havel::ModeManager* m_manager;
};

} // namespace havel::host
