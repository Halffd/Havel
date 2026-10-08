// FileWatcher — the Linux (inotify) file event backend.
//
// Dependency direction (architecture doc, never reversed):
//   EventSource (core) <- FileWatcher (host/platform) <- inotify
//
// inotify callback -> this watcher -> normalize -> coalesce -> publish.
// Coalescing matters: one editor save generates OPEN/MODIFY/MODIFY/
// CLOSE_WRITE; the script's `on file.changed` handler must not run four
// times. Events are queued in the EventRuntime (never touching the VM).
// Watch lifecycle belongs to the subscription: the EventBridge calls
// watch(path) per subscription and unwatch(id) when it dies.
//
// Glob patterns: `on file.created("./plugins/*.hv")` — the pattern's
// directory part is the inotify watch; the basename pattern filters the
// events (fnmatch). Non-glob paths watch the file/dir as-is.

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "../../havel-lang/runtime/events/EventRuntime.hpp"
#include "../../havel-lang/runtime/events/EventSource.hpp"

namespace havel::compiler {

class FileWatcher : public EventSource {
public:
    using WatchId = int;

    FileWatcher() = default;
    ~FileWatcher() override { stop(); }

    // EventSource interface
    void start(EventRuntime &runtime) override;
    void stop() override;

    // Watch a path (file, directory, or glob like "./plugins/*.hv").
    // Returns the watch id, or -1 when inotify is unavailable or the
    // watched directory does not exist.
    // The event name is fixed: "file.changed" (subscribers filter by kind).
    WatchId watch(const std::string &path);
    bool unwatch(WatchId id);
    size_t watchCount() const;

    bool isRunning() const { return running_.load(); }

private:
    void loop();
    // Normalize inotify events and coalesce the OPEN/MODIFY/CLOSE_WRITE
    // cascade into one "file.changed" publish per settle window.
    void handleNativeEvents(
        const std::vector<std::tuple<int, std::string, uint32_t>> &events);

    EventRuntime *runtime_ = nullptr;
    int inotify_fd_ = -1;
    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    struct WatchEntry {
        std::string directory; // the watched dir
        std::string pattern;   // empty = no filtering (non-glob path)
        std::string exactPath; // non-glob: the exact file/dir watched
    };
    // wd -> watch entry (inotify watch descriptors are per-fd)
    std::unordered_map<int, WatchEntry> watch_entries_;

    // Coalescing state: path -> (kind, pending)
    struct PendingChange {
        std::string kind;
        bool pending = false;
    };
    std::unordered_map<std::string, PendingChange> pending_;
};

} // namespace havel::compiler
