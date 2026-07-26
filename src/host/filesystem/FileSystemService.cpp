/*
 * FileSystemService.cpp
 *
 * File system service implementation.
 */
#include "FileSystemService.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>

namespace fs = std::filesystem;

namespace havel::host {

// ============================================================================
// Path validation helper
// ============================================================================

static bool isPathAllowed(const std::string& path) {
    // Resolve the path to canonical form
    std::error_code ec;
    fs::path resolved = fs::canonical(path, ec);
    if (ec) {
        // If canonical fails (e.g., path doesn't exist), try to resolve parent
        resolved = fs::absolute(path, ec);
        if (ec) return false;
    }
    
    // Get allowed base directories
    std::vector<fs::path> allowedBases;
    
    // Current working directory
    allowedBases.push_back(fs::current_path(ec));
    if (ec) allowedBases.clear();
    
    // Home directory
    const char* home = std::getenv("HOME");
    if (home) allowedBases.push_back(fs::path(home));
    
    // Temp directory
    allowedBases.push_back(fs::temp_directory_path());
    
    // Check if resolved path is within any allowed base
    for (const auto& base : allowedBases) {
        fs::path canonicalBase = fs::canonical(base, ec);
        if (ec) continue;
        
        // Check if resolved starts with canonicalBase
        auto baseStr = canonicalBase.string();
        auto resolvedStr = resolved.string();
        
        if (resolvedStr.rfind(baseStr, 0) == 0) {
            // Path is within allowed base
            return true;
        }
    }
    
    return false;
}

// Safe file open with O_NOFOLLOW to prevent symlink attacks
static int safeOpen(const std::string& path, int flags, mode_t mode = 0) {
    if (!isPathAllowed(path)) return -1;
    
    // Use O_NOFOLLOW to prevent following symlinks
    int fd = open(path.c_str(), flags | O_NOFOLLOW, mode);
    return fd;
}

// Safe stat that validates path before stat
static bool safeStat(const std::string& path, struct stat& st) {
    if (!isPathAllowed(path)) return false;
    
    // Use lstat to not follow symlinks
    return lstat(path.c_str(), &st) == 0;
}

FileSystemService::FileSystemService() {
}

FileSystemService::~FileSystemService() {
}

std::string FileSystemService::readFile(const std::string& path) {
    if (!isPathAllowed(path)) return "";
    
    // Open file safely
    int fd = safeOpen(path, O_RDONLY);
    if (fd < 0) return "";
    
    // Read file contents
    std::string result;
    char buffer[4096];
    ssize_t bytes;
    while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) {
        result.append(buffer, bytes);
    }
    close(fd);
    
    return result;
}

bool FileSystemService::writeFile(const std::string& path, const std::string& content) {
    if (!isPathAllowed(path)) return false;
    
    // Open file safely with O_CREAT | O_EXCL to prevent overwriting
    int fd = safeOpen(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) return false;
    
    // Write content
    ssize_t written = write(fd, content.c_str(), content.size());
    close(fd);
    
    return written == static_cast<ssize_t>(content.size());
}

bool FileSystemService::appendFile(const std::string& path, const std::string& content) {
    if (!isPathAllowed(path)) return false;
    
    int fd = safeOpen(path, O_WRONLY | O_APPEND | O_NOFOLLOW);
    if (fd < 0) return false;
    
    ssize_t written = write(fd, content.c_str(), content.size());
    close(fd);
    
    return written == static_cast<ssize_t>(content.size());
}

bool FileSystemService::deleteFile(const std::string& path) {
    if (!isPathAllowed(path)) return false;
    
    // Use unlink with O_NOFOLLOW equivalent
    struct stat st;
    if (!safeStat(path, st)) return false;
    if (!S_ISREG(st.st_mode)) return false;  // Only delete regular files
    
    std::error_code ec;
    return fs::remove(path, ec);
}

bool FileSystemService::copyFile(const std::string& from, const std::string& to) {
    if (!isPathAllowed(from) || !isPathAllowed(to)) return false;
    
    // Use file descriptors to avoid TOCTOU
    int srcFd = safeOpen(from, O_RDONLY);
    if (srcFd < 0) return false;
    
    int dstFd = safeOpen(to, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (dstFd < 0) {
        close(srcFd);
        return false;
    }
    
    char buffer[4096];
    ssize_t bytes;
    while ((bytes = read(srcFd, buffer, sizeof(buffer))) > 0) {
        if (write(dstFd, buffer, bytes) != bytes) {
            close(srcFd);
            close(dstFd);
            return false;
        }
    }
    
    close(srcFd);
    close(dstFd);
    return bytes >= 0;
}

bool FileSystemService::moveFile(const std::string& from, const std::string& to) {
    if (!isPathAllowed(from) || !isPathAllowed(to)) return false;
    
    struct stat fromSt, toSt;
    if (!safeStat(from, fromSt)) return false;
    if (!S_ISREG(fromSt.st_mode)) return false;  // Only move regular files
    
    // Check if destination exists and is a regular file
    if (safeStat(to, toSt) == 0) {
        if (!S_ISREG(toSt.st_mode)) return false;
    }
    
    std::error_code ec;
    fs::rename(from, to, ec);
    return !ec;
}

std::vector<FileInfo> FileSystemService::listDirectory(const std::string& path) {
    if (!isPathAllowed(path)) return {};
    
    struct stat st;
    if (!safeStat(path, st)) return {};
    if (!S_ISDIR(st.st_mode)) return {};
    
    std::vector<FileInfo> result;
    
    // Open directory safely
    int dirFd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dirFd < 0) return {};
    
    // Use fdopendir to get DIR* from fd
    DIR* dir = fdopendir(dirFd);
    if (!dir) {
        close(dirFd);
        return {};
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        FileInfo info;
        info.name = entry->d_name;
        info.path = fs::path(path) / entry->d_name;
        
        // Use fstatat to get file info without following symlinks
        struct stat entryStat;
        if (fstatat(dirFd, entry->d_name, &entryStat, AT_SYMLINK_NOFOLLOW) == 0) {
            info.isFile = S_ISREG(entryStat.st_mode);
            info.isDirectory = S_ISDIR(entryStat.st_mode);
            
            if (info.isFile) {
                info.size = entryStat.st_size;
            }
            
            info.modifiedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::from_time_t(entryStat.st_mtime).time_since_epoch()
            ).count();
        }
        
        result.push_back(info);
    }
    
    closedir(dir);
    return result;
}

bool FileSystemService::createDirectory(const std::string& path) {
    if (!isPathAllowed(path)) return false;
    
    struct stat st;
    if (safeStat(path, st) == 0) return false;  // Already exists
    
    std::error_code ec;
    return fs::create_directory(path, ec);
}

bool FileSystemService::createDirectories(const std::string& path) {
    if (!isPathAllowed(path)) return false;
    
    struct stat st;
    if (safeStat(path, st) == 0 && S_ISDIR(st.st_mode)) return true;  // Already exists
    
    std::error_code ec;
    return fs::create_directories(path, ec);
}

bool FileSystemService::deleteDirectory(const std::string& path) {
    if (!isPathAllowed(path)) return false;
    
    struct stat st;
    if (!safeStat(path, st)) return false;
    if (!S_ISDIR(st.st_mode)) return false;  // Only delete directories
    
    std::error_code ec;
    return fs::remove_all(path, ec) > 0;
}

FileInfo FileSystemService::getFileInfo(const std::string& path) const {
    if (!isPathAllowed(path)) return FileInfo{};
    
    FileInfo info;
    info.path = path;
    
    struct stat st;
    if (!safeStat(path, st)) return info;
    
    info.name = fs::path(path).filename().string();
    info.isFile = S_ISREG(st.st_mode);
    info.isDirectory = S_ISDIR(st.st_mode);
    
    if (info.isFile) {
        info.size = st.st_size;
    }
    
    info.modifiedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::from_time_t(st.st_mtime).time_since_epoch()
    ).count();
    
    return info;
}

bool FileSystemService::exists(const std::string& path) const {
    if (!isPathAllowed(path)) return false;
    
    struct stat st;
    return safeStat(path, st);
}

bool FileSystemService::isFile(const std::string& path) const {
    if (!isPathAllowed(path)) return false;
    
    struct stat st;
    if (!safeStat(path, st)) return false;
    return S_ISREG(st.st_mode);
}

bool FileSystemService::isDirectory(const std::string& path) const {
    if (!isPathAllowed(path)) return false;
    
    struct stat st;
    if (!safeStat(path, st)) return false;
    return S_ISDIR(st.st_mode);
}

int64_t FileSystemService::getFileSize(const std::string& path) const {
    if (!isPathAllowed(path)) return 0;
    
    struct stat st;
    if (!safeStat(path, st)) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    
    return st.st_size;
}

int64_t FileSystemService::getModifiedTime(const std::string& path) const {
    if (!isPathAllowed(path)) return 0;
    
    struct stat st;
    if (!safeStat(path, st)) return 0;
    
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::from_time_t(st.st_mtime).time_since_epoch()
    ).count();
}

std::string FileSystemService::joinPath(const std::string& base, const std::string& path) {
    return (fs::path(base) / path).string();
}

std::string FileSystemService::absolutePath(const std::string& path) {
    std::error_code ec;
    return fs::absolute(path, ec).string();
}

std::string FileSystemService::parentPath(const std::string& path) {
    return fs::path(path).parent_path().string();
}

std::string FileSystemService::fileName(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string FileSystemService::extension(const std::string& path) {
    return fs::path(path).extension().string();
}

std::string FileSystemService::currentDirectory() {
    std::error_code ec;
    return fs::current_path(ec).string();
}

bool FileSystemService::setCurrentDirectory(const std::string& path) {
    std::error_code ec;
    fs::current_path(path, ec);
    return !ec;
}

std::string FileSystemService::homeDirectory() {
    const char* home = std::getenv("HOME");
    if (home) return home;
    
    home = std::getenv("USERPROFILE");
    if (home) return home;
    
    return "";
}

std::string FileSystemService::tempDirectory() {
    return fs::temp_directory_path().string();
}

} // namespace havel::host
