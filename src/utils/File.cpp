#include "File.hpp"
#include "utils/Util.hpp"
#include <filesystem>
#include <stdexcept>
#include <cctype>

namespace havel {

File::File(const std::string& filePath) : FileManager(filePath) {}

File& File::set(const std::string& content) {
    return set(content, "UTF-8");
}

File& File::set(const std::string& content, const std::string& encoding) {
    try {
        std::filesystem::path filePath(this->getFilePath());
        
        // Auto-create parent directories
        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path());
        }
        
        // Check if target is a directory
        if (std::filesystem::exists(filePath) && std::filesystem::is_directory(filePath)) {
            throw std::invalid_argument("Cannot write file - path is a directory: " + filePath.string());
        }
        
        write(content);
    } catch (const std::filesystem::filesystem_error& e) {
        throw std::runtime_error("Write failed: " + std::string(e.what()));
    }
    
    return *this;
}

File& File::concat(const std::string& additionalContent) {
    return concat(additionalContent, false);
}

File& File::concat(const std::string& additionalContent, bool addNewline) {
    std::string currentContent = readContent();
    std::string separator = addNewline ? "\n" : "";
    
    std::string newContent = currentContent + separator + additionalContent;
    write(newContent);
    
    return *this;
}

File& File::add(const std::string& content) {
    return concat(content);
}

File& File::plus(const std::string& content) {
    append(content);
    return *this;
}

File& File::newLine(const std::string& content) {
    return concat(content, true);
}

File& File::newLine() {
    return concat("", true);
}

File& File::clear() {
    return set("");
}

bool File::isEmpty() const {
    std::string content = readContent();
    if (content.empty()) return true;
    
    // Check if only whitespace
    return std::all_of(content.begin(), content.end(), ::isspace);
}

size_t File::length() const {
    return readContent().length();
}

size_t File::wordCount() const {
    std::string content = readContent();
    if (content.empty()) return 0;
    
    std::istringstream iss(content);
    std::string word;
    size_t count = 0;
    
    while (iss >> word) {
        count++;
    }
    
    return count;
}

size_t File::lineCount() const {
    std::string content = readContent();
    if (content.empty()) return 0;
    
    return std::count(content.begin(), content.end(), '\n') + 1;
}

std::vector<std::string> File::lines() const {
    std::vector<std::string> result;
    std::string content = readContent();
    
    if (content.empty()) return result;
    
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        result.push_back(line);
    }
    
    return result;
}

std::list<std::string> File::linesAsList() const {
    auto vec = lines();
    return std::list<std::string>(vec.begin(), vec.end());
}

File& File::replace(const std::string& target, const std::string& replacement) {
    std::string content = readContent();
    
    size_t pos = 0;
    while ((pos = content.find(target, pos)) != std::string::npos) {
        content.replace(pos, target.length(), replacement);
        pos += replacement.length();
    }
    
    return set(content);
}

File& File::replaceRegex(const std::string& regex, const std::string& replacement) {
    std::string content = readContent();
    std::regex regexPattern(regex);
    std::string result = std::regex_replace(content, regexPattern, replacement);
    return set(result);
}

File& File::toUpperCase() {
    std::string content = readContent();
    return set(havel::toUpper(content));
}

File& File::toLowerCase() {
    std::string content = readContent();
    return set(havel::toLower(content));
}

File& File::trim() {
    std::string content = readContent();
    return set(havel::trim(content));
}

bool File::contains(const std::string& text) const {
    return readContent().find(text) != std::string::npos;
}

bool File::containsIgnoreCase(const std::string& text) const {
    std::string content = toLower(readContent());
    std::string searchText = toLower(text);
    return content.find(searchText) != std::string::npos;
}

bool File::matches(const std::string& regex) const {
    std::string content = readContent();
    std::regex pattern(regex);
    return std::regex_match(content, pattern);
}

std::vector<std::string> File::findMatches(const std::string& regex) const {
    std::vector<std::string> matches;
    std::string content = readContent();
    std::regex pattern(regex);
    std::sregex_iterator iter(content.begin(), content.end(), pattern);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        matches.push_back(iter->str());
    }
    
    return matches;
}

int File::count(const std::string& text) const {
    if (text.empty()) return 0;
    
    std::string content = readContent();
    int count = 0;
    size_t pos = 0;
    
    while ((pos = content.find(text, pos)) != std::string::npos) {
        count++;
        pos += text.length();
    }
    
    return count;
}

File& File::insertLineAt(int lineNumber, const std::string& text) {
    auto linesList = linesAsList();
    
    if (lineNumber < 0 || lineNumber > static_cast<int>(linesList.size())) {
        throw std::out_of_range("Line number out of range: " + std::to_string(lineNumber));
    }
    
    auto it = linesList.begin();
    std::advance(it, lineNumber);
    linesList.insert(it, text);
    
    std::string result;
    for (auto it = linesList.begin(); it != linesList.end(); ++it) {
        if (it != linesList.begin()) result += "\n";
        result += *it;
    }
    
    return set(result);
}

File& File::removeLineAt(int lineNumber) {
    auto linesList = linesAsList();
    
    if (lineNumber < 0 || lineNumber >= static_cast<int>(linesList.size())) {
        throw std::out_of_range("Line number out of range: " + std::to_string(lineNumber));
    }
    
    auto it = linesList.begin();
    std::advance(it, lineNumber);
    linesList.erase(it);
    
    std::string result;
    for (auto it = linesList.begin(); it != linesList.end(); ++it) {
        if (it != linesList.begin()) result += "\n";
        result += *it;
    }
    
    return set(result);
}

File& File::replaceLineAt(int lineNumber, const std::string& newText) {
    auto linesList = linesAsList();
    
    if (lineNumber < 0 || lineNumber >= static_cast<int>(linesList.size())) {
        throw std::out_of_range("Line number out of range: " + std::to_string(lineNumber));
    }
    
    auto it = linesList.begin();
    std::advance(it, lineNumber);
    *it = newText;
    
    std::string result;
    for (auto it = linesList.begin(); it != linesList.end(); ++it) {
        if (it != linesList.begin()) result += "\n";
        result += *it;
    }
    
    return set(result);
}

std::string File::getLineAt(int lineNumber) const {
    auto linesVec = lines();
    
    if (lineNumber < 0 || lineNumber >= static_cast<int>(linesVec.size())) {
        throw std::out_of_range("Line number out of range: " + std::to_string(lineNumber));
    }
    
    return linesVec[lineNumber];
}

bool File::isValidJson() const {
    try {
        std::string content = havel::trim(readContent());
        if (content.empty()) return false;
        
        return (content.front() == '{' && content.back() == '}') ||
               (content.front() == '[' && content.back() == ']');
    } catch (...) {
        return false;
    }
}

bool File::isValidXml() const {
    try {
        std::string content = havel::trim(readContent());
        if (content.empty()) return false;
        
        return content.find("<?xml") == 0 ||
               (content.front() == '<' && content.back() == '>');
    } catch (...) {
        return false;
    }
}

std::map<std::string, std::string> File::getContentStatistics() const {
    std::map<std::string, std::string> stats;
    std::string content = readContent();
    
    if (content.empty()) {
        stats["error"] = "Could not read file";
        return stats;
    }
    
    stats["characters"] = std::to_string(content.length());
    
    // Characters without spaces
    std::string noSpaces = content;
    noSpaces.erase(std::remove_if(noSpaces.begin(), noSpaces.end(), ::isspace), noSpaces.end());
    stats["charactersNoSpaces"] = std::to_string(noSpaces.length());
    
    stats["words"] = std::to_string(wordCount());
    stats["lines"] = std::to_string(lineCount());
    
    // Count paragraphs (double newlines)
    std::regex paragraphRegex("\n\\s*\n");
    auto paragraphCount = std::distance(
        std::sregex_iterator(content.begin(), content.end(), paragraphRegex),
        std::sregex_iterator()
    ) + 1;
    stats["paragraphs"] = std::to_string(paragraphCount);
    
    // Count sentences
    std::regex sentenceRegex("[.!?]+");
    auto sentenceCount = std::distance(
        std::sregex_iterator(content.begin(), content.end(), sentenceRegex),
        std::sregex_iterator()
    );
    stats["sentences"] = std::to_string(sentenceCount);
    
    // Average words per line
    double avgWordsPerLine = static_cast<double>(wordCount()) / std::max(1UL, lineCount());
    stats["averageWordsPerLine"] = std::to_string(avgWordsPerLine);
    
    // Most common character
    std::map<char, int> charFreq;
    for (char c : content) {
        charFreq[c]++;
    }
    
    auto mostCommon = std::max_element(charFreq.begin(), charFreq.end(),
        [](const std::pair<char, int>& a, const std::pair<char, int>& b) {
            return a.second < b.second;
        });
    
    if (mostCommon != charFreq.end()) {
        stats["mostCommonChar"] = std::string(1, mostCommon->first);
    }
    
    return stats;
}

std::string File::toString() const {
    std::string content = readContent();
    return content.empty() ? "" : content;
}

// Private methods
void File::createParentDirectories(const std::string& filepath) const {
    std::filesystem::path path(filepath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
}

std::string File::readContent() const {
    std::string content = read();
    return content;
}
}; // namespace havel