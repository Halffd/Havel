/*
 * ClipboardInfo.hpp
 *
 * Complete clipboard information structure with querying/filtering/mapping
 */
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <algorithm>

namespace havel::host {

/**
 * ClipboardInfo - Complete clipboard information
 * 
 * Contains type, content, and metadata about clipboard data.
 */
struct ClipboardInfo {
    enum class Type {
        EMPTY,   // No content
        TEXT,    // Text content
        IMAGE,   // Image content (base64 PNG)
        FILES    // File paths
    };
    
    Type type = Type::EMPTY;
    std::string content;           // Raw content (text or base64 image)
    std::vector<std::string> files; // File paths if Type::FILES
    size_t size = 0;               // Size in bytes
    std::string mimeType;          // MIME type if available
    
    // =========================================================================
    // Type checking helpers
    // =========================================================================
    
    bool isEmpty() const { return type == Type::EMPTY; }
    bool isText() const { return type == Type::TEXT; }
    bool isImage() const { return type == Type::IMAGE; }
    bool isFiles() const { return type == Type::FILES; }
    
    // =========================================================================
    // Content getters (type-safe)
    // =========================================================================
    
    std::string getText() const {
        return type == Type::TEXT ? content : "";
    }
    
    std::string getImage() const {
        return type == Type::IMAGE ? content : "";
    }
    
    std::vector<std::string> getFiles() const {
        return type == Type::FILES ? files : std::vector<std::string>{};
    }
    
    // =========================================================================
    // Querying operations (static - work on collections)
    // =========================================================================
    
    /// Filter clipboard infos by type
    static std::vector<ClipboardInfo> filterByType(
        const std::vector<ClipboardInfo>& infos, 
        Type type);
    
    /// Filter clipboard infos by predicate
    static std::vector<ClipboardInfo> filter(
        const std::vector<ClipboardInfo>& infos,
        const std::function<bool(const ClipboardInfo&)>& predicate);
    
    /// Map clipboard infos to transform content
    static std::vector<ClipboardInfo> map(
        const std::vector<ClipboardInfo>& infos,
        const std::function<ClipboardInfo(const ClipboardInfo&)>& transform);
    
    /// Find first matching clipboard info
    static ClipboardInfo find(
        const std::vector<ClipboardInfo>& infos,
        const std::function<bool(const ClipboardInfo&)>& predicate);
    
    /// Query: get all text entries
    static std::vector<ClipboardInfo> getAllText(
        const std::vector<ClipboardInfo>& infos);
    
    /// Query: get all image entries
    static std::vector<ClipboardInfo> getAllImages(
        const std::vector<ClipboardInfo>& infos);
    
    /// Query: get all file entries
    static std::vector<ClipboardInfo> getAllFiles(
        const std::vector<ClipboardInfo>& infos);
    
    /// Query: find by content substring
    static ClipboardInfo findByContent(
        const std::vector<ClipboardInfo>& infos,
        const std::string& substring);
    
    /// Query: filter by file extension
    static std::vector<ClipboardInfo> filterByExtension(
        const std::vector<ClipboardInfo>& infos,
        const std::string& extension);
    
    /// Query: filter by size range
    static std::vector<ClipboardInfo> filterBySize(
        const std::vector<ClipboardInfo>& infos,
        size_t minSize, 
        size_t maxSize);
    
    /// Aggregate: count by type
    static size_t countByType(
        const std::vector<ClipboardInfo>& infos,
        Type type);
    
    /// Aggregate: total size
    static size_t totalSize(
        const std::vector<ClipboardInfo>& infos);
    
    /// Sort: by size (ascending)
    static std::vector<ClipboardInfo> sortBySize(
        const std::vector<ClipboardInfo>& infos,
        bool ascending = true);
    
    /// Sort: by type (TEXT, IMAGE, FILES, EMPTY)
    static std::vector<ClipboardInfo> sortByType(
        const std::vector<ClipboardInfo>& infos);
};

} // namespace havel::host
