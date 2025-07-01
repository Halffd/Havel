#pragma once

#include "FileManager.hpp"
#include <string>
#include <vector>
#include <list>
#include <map>
#include <regex>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include "utils/Util.hpp"
namespace havel {

class File : public FileManager {
public:
    explicit File(const std::string& filePath);
    
    // Content assignment
    File& set(const std::string& content);
    File& set(const std::string& content, const std::string& encoding);
    
    // Content concatenation
    File& concat(const std::string& additionalContent);
    File& concat(const std::string& additionalContent, bool addNewline);
    
    // Fluent API methods
    File& add(const std::string& content);
    File& plus(const std::string& content);
    File& newLine(const std::string& content);
    File& newLine();
    File& clear();
    
    // Content analysis
    bool isEmpty() const;
    size_t length() const;
    size_t wordCount() const;
    size_t lineCount() const;
    std::vector<std::string> lines() const;
    std::list<std::string> linesAsList() const;
    
    // Text processing
    File& replace(const std::string& target, const std::string& replacement);
    File& replaceRegex(const std::string& regex, const std::string& replacement);
    File& toUpperCase();
    File& toLowerCase();
    File& trim();
    
    // Search methods
    bool contains(const std::string& text) const;
    bool containsIgnoreCase(const std::string& text) const;
    bool matches(const std::string& regex) const;
    std::vector<std::string> findMatches(const std::string& regex) const;
    int count(const std::string& text) const;
    
    // Line manipulation
    File& insertLineAt(int lineNumber, const std::string& text);
    File& removeLineAt(int lineNumber);
    File& replaceLineAt(int lineNumber, const std::string& newText);
    std::string getLineAt(int lineNumber) const;
    
    // Content validation
    bool isValidJson() const;
    bool isValidXml() const;
    
    // Statistics
    std::map<std::string, std::string> getContentStatistics() const;
    
    // Override toString equivalent
    std::string toString() const;
    
private:
    void createParentDirectories(const std::string& filepath) const;
    std::string readContent() const;
    void writeContent(const std::string& content) const;
};

} // namespace havel