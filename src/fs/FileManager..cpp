#include "FileManager.hpp"
#include "utils/Logger.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <openssl/evp.h>
#include <cmath>
#include <zip.h>
#include <minizip/zip.h>
#include <minizip/unzip.h>
#include <cstring>
#include <thread>
#include <atomic>
#include <functional>

using namespace std;

FileManager::FileManager(const string& path) : filePath(path) {
    fs::path p(path);
    fileName = p.filename().string();
    
    if (auto pos = fileName.find_last_of('.'); pos != string::npos && pos < fileName.size() - 1) {
        fileExtension = fileName.substr(pos + 1);
        transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), ::tolower);
    }
}

void FileManager::updateContentCache() const {
    if (!contentLoaded) {
        if (!exists()) {
            contentCache.clear();
        } else {
            ifstream file(filePath, ios::binary);
            contentCache = string(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
        }
        contentLoaded = true;
    }
}

string FileManager::read() const {
    updateContentCache();
    return contentCache;
}

void FileManager::write(const string& content, WriteMode mode) const {
    fs::path p(filePath);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }
    
    ios_base::openmode openMode = ios::binary;
    if (mode == WriteMode::Append) {
        openMode |= ios::app;
    } else {
        openMode |= ios::trunc;
    }
    
    ofstream file(filePath, openMode);
    if (!file) throw FileException("Cannot write to file: " + filePath);
    file << content;
    
    // Update cache
    contentCache = content;
    contentLoaded = true;
}

bool FileManager::exists() const {
    return fs::exists(filePath);
}

bool FileManager::deleteFile() const {
    contentLoaded = false;
    contentCache.clear();
    return fs::remove(filePath);
}

bool FileManager::rename(const string& newName) {
    fs::path newPath = fs::path(filePath).parent_path() / newName;
    error_code ec;
    fs::rename(filePath, newPath, ec);
    if (ec) return false;
    
    filePath = newPath.string();
    fileName = newName;
    
    // Update extension
    if (auto pos = fileName.find_last_of('.'); pos != string::npos && pos < fileName.size() - 1) {
        fileExtension = fileName.substr(pos + 1);
        transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), ::tolower);
    } else {
        fileExtension.clear();
    }
    
    return true;
}

bool FileManager::copy(const string& destination) const {
    error_code ec;
    fs::copy(filePath, destination, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool FileManager::move(const string& destination) {
    if (copy(destination)) {
        return deleteFile();
    }
    return false;
}

bool FileManager::create() const {
    if (exists()) return true;
    
    ofstream file(filePath);
    return file.good();
}

bool FileManager::createDirectories() const {
    return fs::create_directories(filePath);
}

void FileManager::append(const string& content) const {
    write(content, WriteMode::Append);
}

void FileManager::replace(const string& oldStr, const string& newStr) const {
    updateContentCache();
    size_t pos = 0;
    while ((pos = contentCache.find(oldStr, pos)) != string::npos) {
        contentCache.replace(pos, oldStr.length(), newStr);
        pos += newStr.length();
    }
    write(contentCache);
}

void FileManager::replaceRegex(const string& pattern, const string& replacement) const {
    updateContentCache();
    regex reg(pattern);
    contentCache = regex_replace(contentCache, reg, replacement);
    write(contentCache);
}

void FileManager::toUpperCase() const {
    updateContentCache();
    transform(contentCache.begin(), contentCache.end(), contentCache.begin(), ::toupper);
    write(contentCache);
}

void FileManager::toLowerCase() const {
    updateContentCache();
    transform(contentCache.begin(), contentCache.end(), contentCache.begin(), ::tolower);
    write(contentCache);
}

void FileManager::trimWhitespace() const {
    updateContentCache();
    
    // Trim leading spaces
    contentCache.erase(contentCache.begin(), 
        find_if(contentCache.begin(), contentCache.end(), [](int ch) {
            return !isspace(ch);
        }));
    
    // Trim trailing spaces
    contentCache.erase(find_if(contentCache.rbegin(), contentCache.rend(), [](int ch) {
        return !isspace(ch);
    }).base(), contentCache.end());
    
    write(contentCache);
}

uintmax_t FileManager::size() const {
    if (exists()) {
        return fs::file_size(filePath);
    }
    return 0;
}

size_t FileManager::wordCount() const {
    updateContentCache();
    istringstream iss(contentCache);
    return distance(istream_iterator<string>(iss), istream_iterator<string>());
}

size_t FileManager::lineCount() const {
    updateContentCache();
    return count(contentCache.begin(), contentCache.end(), '\n') + 1;
}

vector<string> FileManager::lines() const {
    updateContentCache();
    vector<string> lines;
    istringstream iss(contentCache);
    string line;
    
    while (getline(iss, line)) {
        lines.push_back(line);
    }
    
    return lines;
}

map<string, size_t> FileManager::wordFrequency() const {
    updateContentCache();
    map<string, size_t> freq;
    istringstream iss(contentCache);
    string word;
    
    while (iss >> word) {
        // Remove punctuation
        word.erase(remove_if(word.begin(), word.end(), ::ispunct), word.end());
        transform(word.begin(), word.end(), word.begin(), ::tolower);
        freq[word]++;
    }
    
    return freq;
}

string FileManager::getChecksum(const string& algorithm) const {
    if (!exists()) return "";
    
    ifstream file(filePath, ios::binary);
    if (!file) throw FileException("Cannot open file: " + filePath);
    
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    const EVP_MD* md = EVP_get_digestbyname(algorithm.c_str());
    if (!md) throw FileException("Unknown algorithm: " + algorithm);
    
    EVP_DigestInit_ex(context, md, nullptr);
    
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer))) {
        EVP_DigestUpdate(context, buffer, file.gcount());
    }
    
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length;
    EVP_DigestFinal_ex(context, hash, &length);
    EVP_MD_CTX_free(context);
    
    ostringstream oss;
    for (unsigned int i = 0; i < length; ++i) {
        oss << hex << setw(2) << setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

string FileManager::detectMimeType() const {
    static const map<string, string> mimeTypes = {
        {"txt", "text/plain"}, {"json", "application/json"},
        {"xml", "application/xml"}, {"ini", "text/plain"},
        {"html", "text/html"}, {"csv", "text/csv"},
        {"js", "application/javascript"}, {"css", "text/css"},
        {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
        {"gif", "image/gif"}, {"pdf", "application/pdf"},
        {"zip", "application/zip"}, {"tar", "application/x-tar"},
        {"gz", "application/gzip"}, {"mp3", "audio/mpeg"},
        {"mp4", "video/mp4"}, {"doc", "application/msword"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xls", "application/vnd.ms-excel"}, {"ppt", "application/vnd.ms-powerpoint"}
    };
    
    auto it = mimeTypes.find(fileExtension);
    return (it != mimeTypes.end()) ? it->second : "application/octet-stream";
}

string FileManager::getMimeType() const {
    return detectMimeType();
}

map<string, string> FileManager::getMetadata() const {
    map<string, string> metadata;
    
    if (exists()) {
        try {
            fs::path p(filePath);
            metadata["path"] = filePath;
            metadata["filename"] = fileName;
            metadata["extension"] = fileExtension;
            metadata["size"] = to_string(size());
            metadata["size_human"] = formatSize(size());
            metadata["mime_type"] = getMimeType();
            
            auto ftime = fs::last_write_time(filePath);
            auto sctp = time_point_cast<std::chrono::system_clock::duration>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            time_t cftime = std::chrono::system_clock::to_time_t(sctp);
            tm* tm = localtime(&cftime);
            
            ostringstream oss;
            oss << put_time(tm, "%Y-%m-%d %H:%M:%S");
            metadata["modified"] = oss.str();
            
            // Permissions
            auto perms = fs::status(filePath).permissions();
            string permStr;
            permStr += (perms & fs::perms::owner_read) != fs::perms::none ? "r" : "-";
            permStr += (perms & fs::perms::owner_write) != fs::perms::none ? "w" : "-";
            permStr += (perms & fs::perms::owner_exec) != fs::perms::none ? "x" : "-";
            permStr += (perms & fs::perms::group_read) != fs::perms::none ? "r" : "-";
            permStr += (perms & fs::perms::group_write) != fs::perms::none ? "w" : "-";
            permStr += (perms & fs::perms::group_exec) != fs::perms::none ? "x" : "-";
            permStr += (perms & fs::perms::others_read) != fs::perms::none ? "r" : "-";
            permStr += (perms & fs::perms::others_write) != fs::perms::none ? "w" : "-";
            permStr += (perms & fs::perms::others_exec) != fs::perms::none ? "x" : "-";
            metadata["permissions"] = permStr;
            
        } catch (const fs::filesystem_error& e) {
            metadata["error"] = e.what();
        }
    }
    
    return metadata;
}

string FileManager::getFilePath() const {
    return filePath;
}

string FileManager::getFileName() const {
    return fileName;
}

string FileManager::getFileExtension() const {
    return fileExtension;
}

fs::file_time_type FileManager::getLastModified() const {
    return fs::last_write_time(filePath);
}

string FileManager::formatSize(uintmax_t bytes) const {
    const char* sizes[] = {"B", "KB", "MB", "GB", "TB"};
    if (bytes == 0) return "0 B";
    int i = floor(log(bytes) / log(1024));
    double count = bytes / pow(1024.0, i);
    return to_string(count).substr(0, 4) + " " + sizes[i];
}

// Compression implementation using minizip
bool FileManager::compress(const string& outputPath, int compressionLevel) const {
    if (!exists()) return false;
    
    zipFile zf = zipOpen(outputPath.c_str(), APPEND_STATUS_CREATE);
    if (!zf) return false;
    
    zip_fileinfo zi = {};
    int err = zipOpenNewFileInZip(zf, fileName.c_str(), &zi,
                                 nullptr, 0, nullptr, 0, nullptr,
                                 Z_DEFLATED, compressionLevel);
    
    if (err != ZIP_OK) {
        zipClose(zf, nullptr);
        return false;
    }
    
    ifstream file(filePath, ios::binary);
    vector<char> buffer(8192);
    
    while (file) {
        file.read(buffer.data(), buffer.size());
        zipWriteInFileInZip(zf, buffer.data(), file.gcount());
    }
    
    zipCloseFileInZip(zf);
    zipClose(zf, nullptr);
    return true;
}

bool FileManager::decompress(const string& outputPath) const {
    if (!exists()) return false;
    
    unzFile uf = unzOpen(filePath.c_str());
    if (!uf) return false;
    
    if (unzGoToFirstFile(uf) != UNZ_OK) {
        unzClose(uf);
        return false;
    }
    
    unz_file_info file_info;
    char filename[256];
    unzGetCurrentFileInfo(uf, &file_info, filename, sizeof(filename), nullptr, 0, nullptr, 0);
    
    if (unzOpenCurrentFile(uf) != UNZ_OK) {
        unzClose(uf);
        return false;
    }
    
    ofstream out(outputPath, ios::binary);
    vector<char> buffer(8192);
    int bytes_read;
    
    while ((bytes_read = unzReadCurrentFile(uf, buffer.data(), buffer.size())) > 0) {
        out.write(buffer.data(), bytes_read);
    }
    
    unzCloseCurrentFile(uf);
    unzClose(uf);
    return true;
}

vector<string> FileManager::listZipContents() const {
    vector<string> contents;
    if (!exists()) return contents;
    
    unzFile uf = unzOpen(filePath.c_str());
    if (!uf) return contents;
    
    if (unzGoToFirstFile(uf) == UNZ_OK) {
        do {
            unz_file_info file_info;
            char filename[256];
            unzGetCurrentFileInfo(uf, &file_info, filename, sizeof(filename), nullptr, 0, nullptr, 0);
            contents.push_back(filename);
        } while (unzGoToNextFile(uf) == UNZ_OK);
    }
    
    unzClose(uf);
    return contents;
}

map<string, string> FileManager::parseKeyValue(FileType type) const {
    updateContentCache();
    
    switch (type) {
        case FileType::JSON:
            return JsonParser::parse(contentCache);
        case FileType::XML:
            return XmlParser::parse(contentCache);
        case FileType::INI:
            return IniParser::parse(contentCache);
        default:
            return {};
    }
}

bool FileManager::isValid(FileType type) const {
    updateContentCache();
    
    switch (type) {
        case FileType::JSON:
            return JsonParser::validate(contentCache);
        case FileType::XML:
            return XmlParser::validate(contentCache);
        case FileType::INI:
            return IniParser::validate(contentCache);
        default:
            return false;
    }
}

string FileManager::joinPaths(const vector<string>& paths) {
    if (paths.empty()) return "";
    
    fs::path result(paths[0]);
    for (size_t i = 1; i < paths.size(); ++i) {
        result /= paths[i];
    }
    return result.string();
}

string FileManager::getCurrentDirectory() {
    return fs::current_path().string();
}

bool FileManager::changeDirectory(const string& path) {
    error_code ec;
    fs::current_path(path, ec);
    return !ec;
}
std::vector<std::string> FileManager::glob(const std::string& pattern) {
    std::vector<std::string> results;
    fs::path patternPath(pattern);
    fs::path root = patternPath.parent_path();
    
    // If no parent path, use current directory
    if (root.empty()) {
        root = ".";
    }
    
    string regexPattern = globToRegex(pattern);
    
    try {
        regex patternRegex(regexPattern);
        
        // Use error_code to handle permission errors gracefully
        std::error_code ec;
        
        for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
            // Check for errors after each iteration
            if (ec) {
                // Log the error but continue (don't crash)
                std::cerr << "Warning: Cannot access " << entry.path() 
                         << " - " << ec.message() << std::endl;
                ec.clear(); // Clear the error and continue
                continue;
            }
            
            // Skip if we can't get the filename
            std::error_code filename_ec;
            auto filename = entry.path().filename().string();
            
            if (filename_ec) {
                continue; // Skip this entry
            }
            
            if (regex_match(filename, patternRegex)) {
                results.push_back(entry.path().string());
            }
        }
        
        // Check if there was an error during construction/iteration
        if (ec) {
            std::cerr << "Directory iteration error: " << ec.message() << std::endl;
        }
        
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error in glob(): " << e.what() << std::endl;
        // Return empty results instead of crashing
        return results;
    } catch (const regex_error& e) {
        std::cerr << "Regex error in glob(): " << e.what() << std::endl;
        return results;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error in glob(): " << e.what() << std::endl;
        return results;
    }
    
    return results;
}
std::string FileManager::globToRegex(const std::string& glob) {
    std::string regex;
    regex.reserve(glob.size() * 2); // Pre-allocate space
    
    for (char c : glob) {
        switch (c) {
            case '*':
                regex += ".*";    // * matches any sequence of characters
                break;
            case '?':
                regex += ".";     // ? matches any single character
                break;
            case '.':
            case '^':
            case '$':
            case '+':
            case '{':
            case '}':
            case '[':
            case ']':
            case '(':
            case ')':
            case '|':
            case '\\':
                regex += "\\";    // Escape regex special characters
                regex += c;
                break;
            default:
                regex += c;       // Regular character
                break;
        }
    }
    
    return regex;
}

void FileManager::watch(const function<void(const string&, const string&)>& callback) const {
    thread([this, callback]() {
        auto lastModified = getLastModified();
        while (true) {
            this_thread::sleep_for(chrono::seconds(1));
            try {
                auto currentModified = getLastModified();
                if (currentModified > lastModified) {
                    lastModified = currentModified;
                    callback(filePath, "modified");
                }
            } catch (...) {
                callback(filePath, "error");
            }
        }
    }).detach();
}

// JSON Parser Implementation
map<string, string> JsonParser::parse(const string& content) {
    map<string, string> result;
    regex pattern(R"(\s*\"([^\"]+)\"\s*:\s*\"?([^\",\}]+)\"?\s*[,]?)");
    smatch match;
    
    auto it = content.cbegin();
    while (regex_search(it, content.cend(), match, pattern)) {
        result[match[1].str()] = match[2].str();
        it = match.suffix().first;
    }
    
    return result;
}

bool JsonParser::validate(const string& content) {
    int braceCount = 0;
    bool inString = false;
    
    for (char c : content) {
        if (c == '"' && (c == 0 || content[&c - &content[0] - 1] != '\\')) {
            inString = !inString;
        } else if (!inString) {
            if (c == '{') braceCount++;
            else if (c == '}') braceCount--;
        }
    }
    
    return braceCount == 0 && !inString;
}

// XML Parser Implementation
map<string, string> XmlParser::parse(const string& content) {
    map<string, string> result;
    regex pattern(R"(<([^>]+)>([^<]+)</\1>)");
    smatch match;
    
    auto it = content.cbegin();
    while (regex_search(it, content.cend(), match, pattern)) {
        result[match[1].str()] = match[2].str();
        it = match.suffix().first;
    }
    
    return result;
}

bool XmlParser::validate(const string& content) {
    stack<string> tagStack;
    regex tagPattern(R"(<(\/?)([^>]+)>)");
    smatch match;
    
    auto it = content.cbegin();
    while (regex_search(it, content.cend(), match, tagPattern)) {
        string tag = match[2].str();
        if (match[1].str().empty()) {
            // Opening tag
            tagStack.push(tag);
        } else {
            // Closing tag
            if (tagStack.empty() || tagStack.top() != tag) {
                return false;
            }
            tagStack.pop();
        }
        it = match.suffix().first;
    }
    
    return tagStack.empty();
}

// INI Parser Implementation
map<string, string> IniParser::parse(const string& content) {
    map<string, string> result;
    regex sectionPattern(R"(\[([^\]]+)\])");
    regex keyValuePattern(R"(([^=]+)=([^\n]+))");
    smatch match;
    string currentSection;
    
    istringstream iss(content);
    string line;
    
    while (getline(iss, line)) {
        if (regex_match(line, match, sectionPattern)) {
            currentSection = match[1].str() + ".";
        } else if (regex_search(line, match, keyValuePattern)) {
            string key = currentSection + match[1].str();
            string value = match[2].str();
            
            // Trim whitespace
            key.erase(key.find_last_not_of(" \t") + 1);
            key.erase(0, key.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            
            result[key] = value;
        }
    }
    
    return result;
}

bool IniParser::validate(const string& content) {
    // Basic validation: check for section headers and key-value pairs
    bool hasSection = false;
    bool hasKeyValue = false;
    
    istringstream iss(content);
    string line;
    
    while (getline(iss, line)) {
        if (line.empty()) continue;
        if (line[0] == '[' && line.back() == ']') {
            hasSection = true;
        } else if (line.find('=') != string::npos) {
            hasKeyValue = true;
        }
    }
    
    return hasSection && hasKeyValue;
}