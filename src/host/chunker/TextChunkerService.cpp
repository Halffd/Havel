/*
 * TextChunkerService.cpp
 *
 * Text chunking service implementation.
 */
#include "TextChunkerService.hpp"
#include <algorithm>

namespace havel::host {

TextChunkerService::TextChunkerService() {
}

TextChunkerService::~TextChunkerService() {
}

void TextChunkerService::setText(const std::string& text) {
    text_ = text;
    currentChunk_ = tailMode_ ? 0 : 0;  // Start from beginning or end based on tail mode
    recalculateChunks();
}

const std::string& TextChunkerService::getText() const {
    return text_;
}

void TextChunkerService::setChunkSize(size_t size) {
    if (size < 100) size = 100;  // Minimum chunk size
    chunkSize_ = size;
    recalculateChunks();
}

size_t TextChunkerService::getChunkSize() const {
    return chunkSize_;
}

int TextChunkerService::getTotalChunks() const {
    return static_cast<int>(chunks_.size());
}

int TextChunkerService::getCurrentChunk() const {
    return currentChunk_;
}

void TextChunkerService::setCurrentChunk(int index) {
    if (index < 0) index = 0;
    if (index >= getTotalChunks()) index = getTotalChunks() - 1;
    currentChunk_ = index;
}

std::string TextChunkerService::getChunk(int pos) const {
    if (pos < 0 || pos >= static_cast<int>(chunks_.size())) {
        return "";
    }
    return chunks_[pos];
}

std::string TextChunkerService::getCurrentChunkText() const {
    return getChunk(currentChunk_);
}

std::string TextChunkerService::getNextChunk() {
    if (currentChunk_ < getTotalChunks() - 1) {
        currentChunk_++;
    }
    return getCurrentChunkText();
}

std::string TextChunkerService::getPreviousChunk() {
    if (currentChunk_ > 0) {
        currentChunk_--;
    }
    return getCurrentChunkText();
}

void TextChunkerService::goToFirst() {
    currentChunk_ = 0;
}

void TextChunkerService::goToLast() {
    currentChunk_ = getTotalChunks() - 1;
}

void TextChunkerService::setTailMode(bool tail) {
    tailMode_ = tail;
    if (tail) {
        goToLast();
    } else {
        goToFirst();
    }
}

bool TextChunkerService::isTailMode() const {
    return tailMode_;
}

void TextChunkerService::setInverted(bool inverted) {
    inverted_ = inverted;
}

bool TextChunkerService::isInverted() const {
    return inverted_;
}

void TextChunkerService::clear() {
    text_.clear();
    chunks_.clear();
    currentChunk_ = 0;
    tailMode_ = false;
    inverted_ = false;
}

bool TextChunkerService::hasText() const {
    return !text_.empty();
}

void TextChunkerService::recalculateChunks() {
    chunks_.clear();
    
    if (text_.empty()) {
        return;
    }
    
    // Split text into chunks of chunkSize_
    size_t pos = 0;
    while (pos < text_.size()) {
        size_t end = std::min(pos + chunkSize_, text_.size());
        
        // Try to break at word boundary
        if (end < text_.size() && end > pos) {
            // Look for space or newline in the last 100 characters
            size_t searchStart = end > 100 ? end - 100 : pos;
            size_t breakPos = text_.find_last_of(" \n\r", end, end - searchStart);
            
            if (breakPos != std::string::npos && breakPos > pos) {
                end = breakPos + 1;  // Include the space/newline
            }
        }
        
        chunks_.push_back(text_.substr(pos, end - pos));
        pos = end;
    }
    
    // Ensure at least one chunk
    if (chunks_.empty()) {
        chunks_.push_back(text_);
    }
    
    // Set initial position based on tail mode
    if (tailMode_) {
        currentChunk_ = getTotalChunks() - 1;
    } else {
        currentChunk_ = 0;
    }
}

} // namespace havel::host
