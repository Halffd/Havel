/*
 * TextChunkerService.hpp
 *
 * Text chunking service.
 * Splits large text into manageable chunks for display/copying.
 * 
 * Pure C++ logic - no Qt dependencies.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel::host {

/**
 * TextChunkerService - Split text into chunks
 * 
 * Provides text chunking logic for displaying large text in portions.
 * Pure C++ implementation - no GUI, no Qt dependencies.
 */
class TextChunkerService {
public:
    TextChunkerService();
    ~TextChunkerService();

    // =========================================================================
    // Core chunking operations
    // =========================================================================

    /// Set the text to chunk
    void setText(const std::string& text);

    /// Get the current text
    const std::string& getText() const;

    /// Set chunk size (characters per chunk)
    void setChunkSize(size_t size);

    /// Get chunk size
    size_t getChunkSize() const;

    /// Get total number of chunks
    int getTotalChunks() const;

    /// Get current chunk index (0-based)
    int getCurrentChunk() const;

    /// Set current chunk index
    void setCurrentChunk(int index);

    // =========================================================================
    // Chunk retrieval
    // =========================================================================

    /// Get chunk at position
    /// @param pos Chunk index (0-based)
    /// @return Chunk text or empty if invalid index
    std::string getChunk(int pos) const;

    /// Get current chunk text
    std::string getCurrentChunkText() const;

    /// Get next chunk (advances current position)
    std::string getNextChunk();

    /// Get previous chunk (moves back current position)
    std::string getPreviousChunk();

    /// Go to first chunk
    void goToFirst();

    /// Go to last chunk
    void goToLast();

    // =========================================================================
    // Mode operations
    // =========================================================================

    /// Enable/disable tail mode (start from end)
    void setTailMode(bool tail);

    /// Check if tail mode is enabled
    bool isTailMode() const;

    /// Enable/disable inverted mode
    void setInverted(bool inverted);

    /// Check if inverted mode is enabled
    bool isInverted() const;

    // =========================================================================
    // Utility
    // =========================================================================

    /// Clear all text and reset state
    void clear();

    /// Check if text is loaded
    bool hasText() const;

private:
    void recalculateChunks();

    std::string text_;
    std::vector<std::string> chunks_;
    size_t chunkSize_ = 20000;
    int currentChunk_ = 0;
    bool tailMode_ = false;
    bool inverted_ = false;
};

} // namespace havel::host
