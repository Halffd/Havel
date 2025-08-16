#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <functional>
#include <regex>
#include <cctype>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <openssl/evp.h>
#include <zip.h>
#include <minizip/zip.h>
#include <minizip/unzip.h>
#include "types.hpp"
#include "utils/Util.hpp"

namespace fs = std::filesystem;

class FileException : public std::runtime_error {
public:
    explicit FileException(const std::string& msg) : std::runtime_error(msg) {}
};

class FileManager {
public:
    enum class WriteMode {
        Overwrite,
        Append
    };

    explicit FileManager(const std::string& filePath);
    
    // Core file operations
    std::string read() const;
    void write(const std::string& content, WriteMode mode = WriteMode::Overwrite) const;
    bool exists() const;
    bool deleteFile() const;
    bool rename(const std::string& newName);
    bool copy(const std::string& destination) const;
    bool move(const std::string& destination);
    bool create() const;
    bool createDirectories() const;
    void append(const std::string& content) const;
    
    // Content manipulation
    void replace(const std::string& oldStr, const std::string& newStr) const;
    void replaceRegex(const std::string& pattern, const std::string& replacement) const;
    void toUpperCase() const;
    void toLowerCase() const;
    void trimWhitespace() const;
    
    // File information
    uintmax_t size() const;
    size_t wordCount() const;
    size_t lineCount() const;
    std::vector<std::string> lines() const;
    std::map<std::string, size_t> wordFrequency() const;
    std::string getChecksum(const std::string& algorithm = "SHA-256") const;
    std::string getMimeType() const;
    std::map<std::string, std::string> getMetadata() const;
    std::string getFilePath() const;
    std::string getFileName() const;
    std::string getFileExtension() const;
    fs::file_time_type getLastModified() const;
    
    // Compression
    bool compress(const std::string& outputPath, int compressionLevel = 6) const;
    bool decompress(const std::string& outputPath) const;
    std::vector<std::string> listZipContents() const;
    
    // Parsing
    enum class FileType { JSON, XML, INI };
    std::map<std::string, std::string> parseKeyValue(FileType type) const;
    bool isValid(FileType type) const;
    
    // Utility
    static std::string joinPaths(const std::vector<std::string>& paths);
    static std::string getCurrentDirectory();
    static bool changeDirectory(const std::string& path);
    static std::vector<std::string> glob(const std::string& pattern);
    
    // Watch functionality
    void watch(const std::function<void(const std::string&, const std::string&)>& callback) const;
    static std::string globToRegex(const std::string& glob);

private:
    std::string filePath;
    std::string fileName;
    std::string fileExtension;
    mutable std::string contentCache;
    mutable bool contentLoaded = false;

    void updateContentCache() const;
    std::string detectMimeType() const;
    std::string formatSize(uintmax_t bytes) const;
};

// JSON Parser
class JsonParser {
public:
    static std::map<std::string, std::string> parse(const std::string& content);
    static bool validate(const std::string& content);
};

// XML Parser
class XmlParser {
public:
    static std::map<std::string, std::string> parse(const std::string& content);
    static bool validate(const std::string& content);
};

// INI Parser
class IniParser {
public:
    static std::map<std::string, std::string> parse(const std::string& content);
    static bool validate(const std::string& content);
};