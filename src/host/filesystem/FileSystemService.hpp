/*
 * FileSystemService.hpp
 *
 * File system service.
 * Provides file and directory operations.
 * 
 * Pure C++ implementation using std::filesystem.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel::host {

/**
 * File info structure
 */
struct FileInfo {
    std::string name;
    std::string path;
    bool isFile = false;
    bool isDirectory = false;
    int64_t size = 0;  // bytes
    int64_t modifiedTime = 0;  // timestamp
};

/**
 * FileSystemService - File and directory operations
 * 
 * Provides:
 * - read/write files
 * - list directories
 * - create/delete files and directories
 * - file info (size, modified time)
 * - path operations (join, exists, etc.)
 */
class FileSystemService {
public:
    FileSystemService();
    ~FileSystemService();

    // =========================================================================
    // File operations
    // =========================================================================

    /// Read entire file contents
    /// @param path File path
    /// @return File contents or empty if error
    std::string readFile(const std::string& path);

    /// Write contents to file
    /// @param path File path
    /// @param content Content to write
    /// @return true if successful
    bool writeFile(const std::string& path, const std::string& content);

    /// Append content to file
    bool appendFile(const std::string& path, const std::string& content);

    /// Delete a file
    bool deleteFile(const std::string& path);

    /// Copy a file
    bool copyFile(const std::string& from, const std::string& to);

    /// Move/rename a file
    bool moveFile(const std::string& from, const std::string& to);

    // =========================================================================
    // Directory operations
    // =========================================================================

    /// List directory contents
    /// @param path Directory path
    /// @return List of file info
    std::vector<FileInfo> listDirectory(const std::string& path);

    /// Create a directory
    bool createDirectory(const std::string& path);

    /// Create directories recursively
    bool createDirectories(const std::string& path);

    /// Delete a directory
    bool deleteDirectory(const std::string& path);

    // =========================================================================
    // File info
    // =========================================================================

    /// Get file info
    FileInfo getFileInfo(const std::string& path) const;

    /// Check if path exists
    bool exists(const std::string& path) const;

    /// Check if path is a file
    bool isFile(const std::string& path) const;

    /// Check if path is a directory
    bool isDirectory(const std::string& path) const;

    /// Get file size in bytes
    int64_t getFileSize(const std::string& path) const;

    /// Get file modified time
    int64_t getModifiedTime(const std::string& path) const;

    // =========================================================================
    // Path operations
    // =========================================================================

    /// Join path components
    static std::string joinPath(const std::string& base, const std::string& path);

    /// Get absolute path
    static std::string absolutePath(const std::string& path);

    /// Get parent directory
    static std::string parentPath(const std::string& path);

    /// Get file name from path
    static std::string fileName(const std::string& path);

    /// Get file extension
    static std::string extension(const std::string& path);

    /// Get current working directory
    static std::string currentDirectory();

    /// Change current working directory
    static bool setCurrentDirectory(const std::string& path);

    /// Get home directory
    static std::string homeDirectory();

    /// Get temp directory
    static std::string tempDirectory();
};

} // namespace havel::host
