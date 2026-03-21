/*
 * AppService.hpp
 *
 * Application service.
 * Provides application lifecycle and info.
 * 
 * Pure C++ implementation.
 */
#pragma once

#include <string>
#include <vector>

namespace havel::host {

/**
 * AppService - Application lifecycle and info
 * 
 * Provides:
 * - Application info (name, version, etc.)
 * - Command line arguments
 * - Exit functionality
 * - Environment variables
 */
class AppService {
public:
    AppService();
    ~AppService();

    // =========================================================================
    // Application info
    // =========================================================================

    /// Get application name
    static std::string getName();

    /// Get application version
    static std::string getVersion();

    /// Get application description
    static std::string getDescription();

    // =========================================================================
    // Command line
    // =========================================================================

    /// Get command line arguments
    static std::vector<std::string> getArgs();

    /// Get argument at index
    static std::string getArg(int index);

    /// Get number of arguments
    static int getArgCount();

    /// Check if argument exists
    static bool hasArg(const std::string& arg);

    // =========================================================================
    // Exit
    // =========================================================================

    /// Exit application
    /// @param code Exit code
    static void exit(int code = 0);

    /// Restart application
    static void restart();

    // =========================================================================
    // Environment
    // =========================================================================

    /// Get environment variable
    /// @param name Variable name
    /// @return Variable value or empty if not found
    static std::string getEnv(const std::string& name);

    /// Set environment variable
    /// @param name Variable name
    /// @param value Variable value
    /// @return true if successful
    static bool setEnv(const std::string& name, const std::string& value);

    /// Get all environment variables
    static std::vector<std::pair<std::string, std::string>> getAllEnv();

    // =========================================================================
    // Process info
    // =========================================================================

    /// Get current process ID
    static int getPid();

    /// Get parent process ID
    static int getPpid();

    /// Get current working directory
    static std::string getCwd();
};

} // namespace havel::host
