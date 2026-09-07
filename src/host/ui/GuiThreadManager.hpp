/*
 * GuiThreadManager.hpp - Manages Qt event loop on dedicated GUI thread
 *
 * Runs the Qt event loop on a dedicated thread, allowing the VM thread
 * to continue executing while GUI operations happen on the GUI thread.
 * Communication happens via thread-safe message passing.
 */
#pragma once

#ifdef HAVE_QT_EXTENSION

#include <QApplication>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <functional>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <future>

namespace havel::host {

/**
 * GuiThreadManager - Runs Qt event loop on dedicated thread
 *
 * Architecture:
 * - GUI thread: runs QApplication::exec(), processes GUI events
 * - VM thread: schedules work on GUI thread via postTask()
 * - Idle callback: called periodically during Qt event loop for goroutine scheduling
 */
class GuiThreadManager {
public:
    using Task = std::function<void()>;
    using Callback = std::function<void()>;

    static GuiThreadManager& instance();

    GuiThreadManager(const GuiThreadManager&) = delete;
    GuiThreadManager& operator=(const GuiThreadManager&) = delete;

    ~GuiThreadManager();

    // Initialize and start the GUI thread with QApplication
    bool initialize(int argc = 1, char* argv[] = nullptr);

    // Shutdown the GUI thread
    void shutdown();

    // Check if GUI thread is running
    bool isRunning() const;

    // Post a task to run on the GUI thread (thread-safe)
    // Returns a future that completes when the task finishes
    template<typename F>
    auto postTask(F&& task) -> std::future<decltype(task())>;

    // Post a void task (convenience)
    void postTaskVoid(std::function<void()> task);

    // Set idle callback - called periodically during Qt event loop
    // This is where VM can schedule goroutines, process events, etc.
    void setIdleCallback(std::function<void()> cb);

    // Process tasks from the queue (called from GUI thread)
    void processTasks();

    // Quit the event loop
    void quitEventLoop(int exitCode = 0);

    // Check if GUI thread is running

    // Get the GUI thread ID
    std::thread::id guiThreadId() const;

private:
    GuiThreadManager() = default;

    std::atomic<bool> running_{false};
    std::atomic<bool> quitting_{false};
    
    std::thread guiThread_;
    std::thread::id guiThreadId_;
    
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::queue<std::function<void()>> taskQueue_;
    
    std::function<void()> idleCallback_;
    std::atomic<bool> idleCallbackSet_{false};

    int argc_ = 1;
    char* argv_[2] = {const_cast<char*>("havel"), nullptr};
    
    int exitCode_ = 0;

    // Internal thread function
    void runGuiThread(int argc, char* argv[]);

    // Process pending tasks (called from GUI thread)
    void processPendingTasks();
};

} // namespace havel::host

#endif // HAVE_QT_EXTENSION
