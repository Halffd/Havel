/*
 * ClipboardInfo.cpp
 *
 * ClipboardInfo querying/filtering/mapping implementation
 */
#include "ClipboardInfo.hpp"

namespace havel::host {

// ============================================================================
// Static Query Operations
// ============================================================================

std::vector<ClipboardInfo> ClipboardInfo::filterByType(
    const std::vector<ClipboardInfo>& infos, 
    Type type) {
    std::vector<ClipboardInfo> result;
    for (const auto& info : infos) {
        if (info.type == type) {
            result.push_back(info);
        }
    }
    return result;
}

std::vector<ClipboardInfo> ClipboardInfo::filter(
    const std::vector<ClipboardInfo>& infos,
    const std::function<bool(const ClipboardInfo&)>& predicate) {
    std::vector<ClipboardInfo> result;
    for (const auto& info : infos) {
        if (predicate(info)) {
            result.push_back(info);
        }
    }
    return result;
}

std::vector<ClipboardInfo> ClipboardInfo::map(
    const std::vector<ClipboardInfo>& infos,
    const std::function<ClipboardInfo(const ClipboardInfo&)>& transform) {
    std::vector<ClipboardInfo> result;
    result.reserve(infos.size());
    for (const auto& info : infos) {
        result.push_back(transform(info));
    }
    return result;
}

ClipboardInfo ClipboardInfo::find(
    const std::vector<ClipboardInfo>& infos,
    const std::function<bool(const ClipboardInfo&)>& predicate) {
    for (const auto& info : infos) {
        if (predicate(info)) {
            return info;
        }
    }
    return ClipboardInfo{}; // Return empty info
}

std::vector<ClipboardInfo> ClipboardInfo::getAllText(
    const std::vector<ClipboardInfo>& infos) {
    return filterByType(infos, Type::TEXT);
}

std::vector<ClipboardInfo> ClipboardInfo::getAllImages(
    const std::vector<ClipboardInfo>& infos) {
    return filterByType(infos, Type::IMAGE);
}

std::vector<ClipboardInfo> ClipboardInfo::getAllFiles(
    const std::vector<ClipboardInfo>& infos) {
    return filterByType(infos, Type::FILES);
}

ClipboardInfo ClipboardInfo::findByContent(
    const std::vector<ClipboardInfo>& infos,
    const std::string& substring) {
    for (const auto& info : infos) {
        if (info.content.find(substring) != std::string::npos) {
            return info;
        }
        for (const auto& file : info.files) {
            if (file.find(substring) != std::string::npos) {
                return info;
            }
        }
    }
    return ClipboardInfo{};
}

std::vector<ClipboardInfo> ClipboardInfo::filterByExtension(
    const std::vector<ClipboardInfo>& infos,
    const std::string& extension) {
    std::vector<ClipboardInfo> result;
    for (const auto& info : infos) {
        if (info.type == Type::FILES) {
            for (const auto& file : info.files) {
                if (file.size() >= extension.size() &&
                    file.compare(file.size() - extension.size(), extension.size(), extension) == 0) {
                    result.push_back(info);
                    break;
                }
            }
        }
    }
    return result;
}

std::vector<ClipboardInfo> ClipboardInfo::filterBySize(
    const std::vector<ClipboardInfo>& infos,
    size_t minSize, 
    size_t maxSize) {
    std::vector<ClipboardInfo> result;
    for (const auto& info : infos) {
        if (info.size >= minSize && info.size <= maxSize) {
            result.push_back(info);
        }
    }
    return result;
}

size_t ClipboardInfo::countByType(
    const std::vector<ClipboardInfo>& infos,
    Type type) {
    size_t count = 0;
    for (const auto& info : infos) {
        if (info.type == type) {
            ++count;
        }
    }
    return count;
}

size_t ClipboardInfo::totalSize(
    const std::vector<ClipboardInfo>& infos) {
    size_t total = 0;
    for (const auto& info : infos) {
        total += info.size;
    }
    return total;
}

std::vector<ClipboardInfo> ClipboardInfo::sortBySize(
    const std::vector<ClipboardInfo>& infos,
    bool ascending) {
    std::vector<ClipboardInfo> result = infos;
    if (ascending) {
        std::sort(result.begin(), result.end(),
            [](const ClipboardInfo& a, const ClipboardInfo& b) {
                return a.size < b.size;
            });
    } else {
        std::sort(result.begin(), result.end(),
            [](const ClipboardInfo& a, const ClipboardInfo& b) {
                return a.size > b.size;
            });
    }
    return result;
}

std::vector<ClipboardInfo> ClipboardInfo::sortByType(
    const std::vector<ClipboardInfo>& infos) {
    std::vector<ClipboardInfo> result = infos;
    std::sort(result.begin(), result.end(),
        [](const ClipboardInfo& a, const ClipboardInfo& b) {
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        });
    return result;
}

} // namespace havel::host
