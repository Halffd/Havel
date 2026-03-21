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

namespace fs = std::filesystem;

namespace havel::host {

FileSystemService::FileSystemService() {
}

FileSystemService::~FileSystemService() {
}

std::string FileSystemService::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool FileSystemService::writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) return false;
    
    file << content;
    return file.good();
}

bool FileSystemService::appendFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::app);
    if (!file) return false;
    
    file << content;
    return file.good();
}

bool FileSystemService::deleteFile(const std::string& path) {
    std::error_code ec;
    return fs::remove(path, ec);
}

bool FileSystemService::copyFile(const std::string& from, const std::string& to) {
    std::error_code ec;
    return fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
}

bool FileSystemService::moveFile(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    return !ec;
}

std::vector<FileInfo> FileSystemService::listDirectory(const std::string& path) {
    std::vector<FileInfo> result;
    
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        FileInfo info;
        info.name = entry.path().filename().string();
        info.path = entry.path().string();
        info.isFile = entry.is_regular_file(ec);
        info.isDirectory = entry.is_directory(ec);
        
        if (info.isFile) {
            info.size = entry.file_size(ec);
        }
        
        auto mtime = entry.last_write_time(ec);
        if (!ec) {
            info.modifiedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                mtime.time_since_epoch()).count();
        }
        
        result.push_back(info);
    }
    
    return result;
}

bool FileSystemService::createDirectory(const std::string& path) {
    std::error_code ec;
    return fs::create_directory(path, ec);
}

bool FileSystemService::createDirectories(const std::string& path) {
    std::error_code ec;
    return fs::create_directories(path, ec);
}

bool FileSystemService::deleteDirectory(const std::string& path) {
    std::error_code ec;
    return fs::remove_all(path, ec) > 0;
}

FileInfo FileSystemService::getFileInfo(const std::string& path) const {
    FileInfo info;
    info.path = path;
    
    std::error_code ec;
    info.name = fs::path(path).filename().string();
    info.isFile = fs::is_regular_file(path, ec);
    info.isDirectory = fs::is_directory(path, ec);
    
    if (info.isFile) {
        info.size = fs::file_size(path, ec);
    }
    
    auto mtime = fs::last_write_time(path, ec);
    if (!ec) {
        info.modifiedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            mtime.time_since_epoch()).count();
    }
    
    return info;
}

bool FileSystemService::exists(const std::string& path) const {
    std::error_code ec;
    return fs::exists(path, ec);
}

bool FileSystemService::isFile(const std::string& path) const {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool FileSystemService::isDirectory(const std::string& path) const {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

int64_t FileSystemService::getFileSize(const std::string& path) const {
    std::error_code ec;
    return static_cast<int64_t>(fs::file_size(path, ec));
}

int64_t FileSystemService::getModifiedTime(const std::string& path) const {
    std::error_code ec;
    auto mtime = fs::last_write_time(path, ec);
    if (ec) return 0;
    
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        mtime.time_since_epoch()).count();
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
