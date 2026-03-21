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

namespace havel { class ModeManager; }
namespace havel::compiler { class VM; using CallbackId = uint32_t; }

namespace havel::host {

/**
 * ModeService - Pure mode business logic
 * 
 * Provides system-level mode operations without any language runtime coupling.
 * All callbacks are stored as CallbackId (opaque handles) - VM owns the closures.
 */
class ModeService {
public:
    ModeService(havel::compiler::VM* vm, havel::ModeManager* manager);
    ~ModeService() = default;

    // =========================================================================
    // Mode queries
    // =========================================================================

    /// Get current mode name
    std::string getCurrentMode() const;

    /// Get previous mode name
    std::string getPreviousMode() const;

    // =========================================================================
    // Mode control
    // =========================================================================

    /// Set mode explicitly
    void setMode(const std::string& modeName);

    // =========================================================================
    // Mode definition with VM-coupled callbacks
    // =========================================================================

    /// Define a mode with callbacks (stored as CallbackId in HostBridge)
    /// @param name Mode name
    /// @param conditionId Condition callback ID (VM-owned)
    /// @param enterId Enter callback ID (VM-owned)
    /// @param exitId Exit callback ID (VM-owned)
    void defineMode(const std::string& name,
                    compiler::CallbackId conditionId,
                    compiler::CallbackId enterId,
                    compiler::CallbackId exitId);

private:
    havel::compiler::VM* m_vm;  // Non-owning, for callback invocation
    havel::ModeManager* m_manager;  // Non-owning
};

} // namespace havel::host
