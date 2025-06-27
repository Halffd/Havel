#include "File.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cctype>
#include <locale>

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
        
        setFileContent(content);
        writeContent(content);
        updateMetadata();
        
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
    setFileContent(newContent);
    writeContent(newContent);
    
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
    std::transform(content.begin(), content.end(), content.begin(), ::toupper);
    return set(content);
}

File& File::toLowerCase() {
    std::string content = readContent();
    std::transform(content.begin(), content.end(), content.begin(), ::tolower);
    return set(content);
}

File& File::trim() {
    std::string content = readContent();
    return set(trimString(content));
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
        std::string content = trimString(readContent());
        if (content.empty()) return false;
        
        return (content.front() == '{' && content.back() == '}') ||
               (content.front() == '[' && content.back() == ']');
    } catch (...) {
        return false;
    }
}

bool File::isValidXml() const {
    try {
        std::string content = trimString(readContent());
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
    return content#include "File.h"
    #include <filesystem>
    #include <iostream>
    #include <stdexcept>
    #include <cctype>
    #include <locale>
    
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
            
            setFileContent(content);
            writeContent(content);
            updateMetadata();
            
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
        setFileContent(newContent);
        writeContent(newContent);
        
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
        std::transform(content.begin(), content.end(), content.begin(), ::toupper);
        return set(content);
    }
    
    File& File::toLowerCase() {
        std::string content = readContent();
        std::transform(content.begin(), content.end(), content.begin(), ::tolower);
        return set(content);
    }
    
    File& File::trim() {
        std::string content = readContent();
        return set(trimString(content));
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
            std::string content = trimString(readContent());
            if (content.empty()) return false;
            
            return (content.front() == '{' && content.back() == '}') ||
                   (content.front() == '[' && content.back() == ']');
        } catch (...) {
            return false;
        }
    }
    
    bool File::isValidXml() const {
        try {
            std::string content = trimString(readContent());
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
    
    #include "File.h"
    #include <iostream>
    #include <exception>
    
    namespace havel {
    
    void demonstrateFileOperations() {
        std::cout << "=== Enhanced File Class Demo ===\n\n";
        
        try {
            // Create and populate test file
            File textFile("enhanced_text_demo.txt");
            textFile.set("Welcome to the Enhanced File Manager!")
                    .newLine("This is line 2")
                    .newLine("This is line 3 with some UPPERCASE text")
                    .newLine("JSON example: {\"key\": \"value\"}")
                    .newLine("End of file");
            
            std::cout << "Content Analysis:\n";
            std::cout << "  Content: " << textFile.toString().substr(0, 50) << "...\n";
            std::cout << "  Characters: " << textFile.length() << "\n";
            std::cout << "  Words: " << textFile.wordCount() << "\n";
            std::cout << "  Lines: " << textFile.lineCount() << "\n";
            
            // Text processing
            std::cout << "\nText Processing:\n";
            File processedFile("processed_demo.txt");
            processedFile.set(textFile.toString())
                        .replace("Enhanced", "Super Enhanced")
                        .replaceRegex("line \\d+", "LINE X");
            std::cout << "  Processed content: " << processedFile.toString().substr(0, 60) << "...\n";
            
            // Line manipulation
            std::cout << "\nLine Manipulation:\n";
            textFile.insertLineAt(1, ">>> INSERTED LINE <<<");
            std::cout << "  Line at index 1: " << textFile.getLineAt(1) << "\n";
            
            textFile.replaceLineAt(2, ">>> REPLACED LINE <<<");
            std::cout << "  Replaced line 2: " << textFile.getLineAt(2) << "\n";
            
            // Search operations
            std::cout << "\nSearch Operations:\n";
            std::cout << "  Contains 'Enhanced': " << (textFile.contains("Enhanced") ? "true" : "false") << "\n";
            std::cout << "  Contains 'JSON' (ignore case): " << (textFile.containsIgnoreCase("json") ? "true" : "false") << "\n";
            std::cout << "  Count of 'line': " << textFile.count("line") << "\n";
            
            // Content validation
            std::cout << "\nContent Validation:\n";
            File jsonFile("test.json");
            jsonFile.set("{\"name\": \"test\", \"value\": 123}");
            std::cout << "  Is valid JSON: " << (jsonFile.isValidJson() ? "true" : "false") << "\n";
            
            // Statistics
            std::cout << "\nContent Statistics:\n";
            auto stats = textFile.getContentStatistics();
            for (const auto& pair : stats) {
                std::cout << "  " << pair.first << ": " << pair.second << "\n";
            }
            
            // Find regex matches
            std::cout << "\nRegex Matches:\n";
            auto matches = textFile.findMatches("line \\d+");
            std::cout << "  Found " << matches.size() << " matches for 'line \\d+':\n";
            for (const auto& match : matches) {
                std::cout << "    " << match << "\n";
            }
            
            // Text transformations
            std::cout << "\nText Transformations:\n";
            File transformFile("transform_test.txt");
            transformFile.set("  Hello World!  This is a TEST.  ")
                        .trim()
                        .toLowerCase();
            std::cout << "  Transformed text: '" << transformFile.toString() << "'\n";
            
            // Line operations
            std::cout << "\nLine Operations:\n";
            auto allLines = textFile.lines();
            std::cout << "  Total lines: " << allLines.size() << "\n";
            std::cout << "  First line: " << (allLines.empty() ? "N/A" : allLines[0]) << "\n";
            std::cout << "  Last line: " << (allLines.empty() ? "N/A" : allLines.back()) << "\n";
            
            // Content checks
            std::cout << "\nContent Checks:\n";
            File emptyFile("empty_test.txt");
            emptyFile.clear();
            std::cout << "  Empty file is empty: " << (emptyFile.isEmpty() ? "true" : "false") << "\n";
            std::cout << "  Text file is empty: " << (textFile.isEmpty() ? "true" : "false") << "\n";
            
            // Cleanup
            std::cout << "\nCleanup:\n";
            std::vector<std::string> filesToClean = {
                "enhanced_text_demo.txt", "processed_demo.txt", "test.json",
                "transform_test.txt", "empty_test.txt"
            };
            
            for (const auto& fileName : filesToClean) {
                File file(fileName);
                bool deleted = file.deleteFile();
                std::cout << "  " << fileName << ": " << (deleted ? "deleted" : "failed") << "\n";
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Demo failed: " << e.what() << std::endl;
        }
    }
    
    } // namespace havel
    #ifdef TEST_FILE_MANAGER
    int main() {
        havel::demonstrateFileOperations();
        return 0;
    }
    #endif