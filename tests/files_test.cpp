#include "../fs/File.hpp"
#include "../fs/FileManager.hpp"
#include <iostream>
#include <string>
using namespace havel;

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
int main() {
    demonstrateFileOperations();
    FileManager::changeDirectory("/tmp");
    std::cout << "Current directory: " << FileManager::getCurrentDirectory() << std::endl;
    std::vector<std::string> files = FileManager::glob("*");
    for (const auto& file : files) {
        std::cout << file << std::endl;
    }
    std::cout << "File count: " << files.size() << std::endl;
    auto file = FileManager("test.txt");
    file.create();
    file.write(file.getFilePath() + "\n" + "test\n");
    std::cout << "File size: " << file.size() << std::endl;
    std::cout << "File word count: " << file.wordCount() << std::endl;
    std::cout << "File line count: " << file.lineCount() << std::endl;
    std::cout << "File content: " << file.read() << std::endl;
    std::cout << "File metadata: ";
    for (const auto& pair : file.getMetadata()) {
        std::cout << pair.first << ": " << pair.second << ", ";
    }
    std::cout << std::endl;
    std::cout << "File checksum: " << file.getChecksum() << std::endl;
    std::cout << "File MIME type: " << file.getMimeType() << std::endl;
    std::cout << "File last modified: " << file.getLastModified() << std::endl;
    std::cout << "File word frequency: ";
    for (const auto& pair : file.wordFrequency()) {
        std::cout << pair.first << ": " << pair.second << ", ";
    }
    std::cout << std::endl;
    std::cout << "File path: " << file.getFilePath() << std::endl;
    std::cout << "File name: " << file.getFileName() << std::endl;
    std::cout << "File extension: " << file.getFileExtension() << std::endl;
    return 0;
}