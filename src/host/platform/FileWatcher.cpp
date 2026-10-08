#include "FileWatcher.hpp"

#include <cerrno>
#include <cstring>
#include <fnmatch.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>

#include "utils/Logger.hpp"
#include <iostream>
#include <cstdlib>

namespace havel::compiler {
namespace {

// Normalize inotify masks to the subscriber-facing kinds.
std::string maskToKind(uint32_t mask) {
    if (mask & (IN_CREATE | IN_MOVED_TO)) {
        return "created";
    }
    if (mask & (IN_DELETE | IN_MOVED_FROM)) {
        return "deleted";
    }
    // IN_MODIFY / IN_ATTRIB / IN_CLOSE_WRITE / mixed
    return "modified";
}

// Whether the path contains glob metacharacters.
bool hasGlobMeta(const std::string &path) {
    return path.find_first_of("*?[") != std::string::npos;
}

// Coalescing settle window: inotify cascades from one save land inside it.
constexpr auto COALESCE_WINDOW = std::chrono::milliseconds(50);

} // namespace

void FileWatcher::start(EventRuntime &runtime) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load()) {
        return;
    }
    runtime_ = &runtime;
    inotify_fd_ = inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ < 0) {
        ::havel::error("[FileWatcher] inotify_init failed: {}", strerror(errno));
        return;
    }
    running_.store(true);
    thread_ = std::thread(&FileWatcher::loop, this);
}

void FileWatcher::stop() {
    running_.store(false);
    // Always join: a thread whose loop exited early is still joinable —
    // destroying a joinable thread is std::terminate.
    if (thread_.joinable()) {
        thread_.join();
    }
    if (inotify_fd_ >= 0) {
        close(inotify_fd_);
        inotify_fd_ = -1;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    watch_entries_.clear();
    pending_.clear();
}

FileWatcher::WatchId FileWatcher::watch(const std::string &path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (inotify_fd_ < 0) {
        return -1;
    }
    namespace fs = std::filesystem;

    if (hasGlobMeta(path)) {
        // Glob: watch the pattern's directory; the basename pattern filters.
        fs::path p(path);
        fs::path dir = p.parent_path();
        std::string pattern = p.filename().string();
        std::string dirStr = dir.empty() ? std::string(".") : dir.string();
        // Duplicate watch: same dir+pattern already watched.
        for (const auto &[wd, existing] : watch_entries_) {
            if (existing.directory == dirStr && existing.pattern == pattern) {
                return wd;
            }
        }
        int wd = inotify_add_watch(inotify_fd_, dirStr.c_str(),
                                   IN_CREATE | IN_DELETE | IN_MODIFY |
                                       IN_ATTRIB | IN_CLOSE_WRITE |
                                       IN_MOVED_TO | IN_MOVED_FROM);
        if (wd < 0) {
            ::havel::error("[FileWatcher] inotify_add_watch('{}') failed: {}",
                           dirStr, strerror(errno));
            return -1;
        }
        watch_entries_[wd] = WatchEntry{dirStr, pattern, ""};
        return wd;
    }

    // Non-glob: watch the file or directory as-is.
    for (const auto &[wd, existing] : watch_entries_) {
        if (existing.exactPath == path) {
            return wd;
        }
    }
    int wd = inotify_add_watch(inotify_fd_, path.c_str(),
                               IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB |
                                   IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM);
    if (wd < 0) {
        ::havel::error("[FileWatcher] inotify_add_watch('{}') failed: {}", path,
                       strerror(errno));
        return -1;
    }
    watch_entries_[wd] = WatchEntry{path, "", path};
    return wd;
}

bool FileWatcher::unwatch(WatchId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = watch_entries_.find(id);
    if (it == watch_entries_.end()) {
        return false;
    }
    inotify_rm_watch(inotify_fd_, id);
    watch_entries_.erase(it);
    return true;
}

size_t FileWatcher::watchCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return watch_entries_.size();
}

void FileWatcher::loop() {
    std::vector<char> buffer(64 * 1024);
    auto lastFlush = std::chrono::steady_clock::now();

    while (running_.load()) {
        std::vector<std::tuple<int, std::string, uint32_t>> nativeEvents;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (inotify_fd_ < 0) {
                break;
            }
            ssize_t len;
            while ((len = read(inotify_fd_, buffer.data(), buffer.size())) > 0) {
                ssize_t offset = 0;
                while (offset + sizeof(struct inotify_event) <= len) {
                    auto *ev =
                        reinterpret_cast<struct inotify_event *>(buffer.data() + offset);
                    // Self-events from inotify_add_watch are noise.
                    if (!(ev->mask & IN_IGNORED)) {
                        // ev->name holds the changed entry's name when len > 0
                        std::string name =
                            ev->len > 0 ? std::string(ev->name) : std::string();
                        nativeEvents.emplace_back(ev->wd, std::move(name), ev->mask);
                    }
                    offset += sizeof(struct inotify_event) + ev->len;
                }
                if (len < static_cast<ssize_t>(buffer.size())) {
                    break;
                }
            }
        }

        if (!nativeEvents.empty()) {
            handleNativeEvents(nativeEvents);
            lastFlush = std::chrono::steady_clock::now();
        }

        // Flush coalesced changes after the settle window.
        auto now = std::chrono::steady_clock::now();
        if (now - lastFlush >= COALESCE_WINDOW) {
            std::vector<std::pair<std::string, std::string>> flush;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto it = pending_.begin(); it != pending_.end();) {
                    if (it->second.pending) {
                        flush.emplace_back(it->first, it->second.kind);
                        it = pending_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            if (runtime_ && !flush.empty()) {
                for (const auto &[path, kind] : flush) {
                    EventPayload payload;
                    payload.fields["path"] = path;
                    payload.fields["kind"] = kind;
                    // Kind-specific name (file.created/modified/deleted)
                    // plus the generic umbrella (file.changed) so both
                    // `on file.created(...)` and `on file.changed(...)`
                    // subscriptions match.
                    if (std::getenv("HAVEL_FW_DIAG")) std::cerr << "[fw-diag] publishing file." << kind << " path=" << path << "\n";
                    runtime_->publish("file." + kind, payload);
                    runtime_->publish("file.changed", std::move(payload));
                }
                lastFlush = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

void FileWatcher::handleNativeEvents(
    const std::vector<std::tuple<int, std::string, uint32_t>> &events) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &[wd, entryName, mask] : events) {
        auto entryIt = watch_entries_.find(wd);
        if (entryIt == watch_entries_.end()) {
            continue;
        }
        const WatchEntry &entry = entryIt->second;

        std::string path;
        if (!entry.pattern.empty()) {
            // Glob watch: the event is for an entry of the watched dir.
            // Filter by the basename pattern (fnmatch).
            if (entryName.empty() ||
                fnmatch(entry.pattern.c_str(), entryName.c_str(), 0) != 0) {
                continue;
            }
            path = entry.directory + "/" + entryName;
        } else {
            // Non-glob: the exact path (the watched file/dir itself).
            path = entry.exactPath;
        }

        // Kind precedence: deleted > created > modified. A save cascade
        // (OPEN, MODIFY, MODIFY, CLOSE_WRITE) coalesces into one modified;
        // a delete wins over a trailing modify of the same file.
        std::string kind = maskToKind(mask);
        auto &pendingEntry = pending_[path];
        if (pendingEntry.pending) {
            if (kind == "deleted") {
                pendingEntry.kind = "deleted";
            } else if (pendingEntry.kind != "deleted" && kind == "created") {
                pendingEntry.kind = "created";
            }
            // modified keeps the existing kind otherwise
        } else {
            pendingEntry.kind = kind;
            pendingEntry.pending = true;
        }
    }
}

} // namespace havel::compiler
