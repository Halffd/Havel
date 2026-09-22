/*
 * GuiThreadManager.cpp - Qt event loop on dedicated GUI thread
 */
#ifdef HAVE_QT_EXTENSION

#include "GuiThreadManager.hpp"
#include <QApplication>
#include <QTimer>
#include <QEventLoop>
#include <QCoreApplication>
#include <QMetaObject>
#include <QDebug>
#include "utils/Logger.hpp"

namespace havel::host {

GuiThreadManager& GuiThreadManager::instance() {
    static GuiThreadManager inst;
    return inst;
}

GuiThreadManager::~GuiThreadManager() {
    shutdown();
}

bool GuiThreadManager::initialize(int argc, char* argv[]) {
    debug("GuiThreadManager: initialize called, running={}", running_.load());
    if (running_.load()) {
        debug("GuiThreadManager: already running");
        return true;
    }

    argc_ = argc;
    if (argc > 0 && argv) {
        argv_[0] = argv[0];
    }

    running_.store(true);
    quitting_.store(false);
    
    debug("GuiThreadManager: starting GUI thread");
    guiThread_ = std::thread(&GuiThreadManager::runGuiThread, this, argc_, argv_);
    guiThreadId_ = guiThread_.get_id();
    
    // Wait for GUI thread to start
    std::unique_lock<std::mutex> lock(queueMutex_);
    debug("GuiThreadManager: waiting for GUI thread to start");
    bool started = queueCondition_.wait_for(lock, std::chrono::seconds(5), [this] { return running_.load() && guiThreadId_ != std::thread::id(); });
    debug("GuiThreadManager: GUI thread started={}, running={}", started, running_.load());
    
    return started;
}

void GuiThreadManager::shutdown() {
    if (!running_.load()) {
        return;
    }
    
    quitting_.store(true);
    quitEventLoop(0);
    
    if (guiThread_.joinable()) {
        guiThread_.join();
    }
    
    running_.store(false);
}

void GuiThreadManager::runGuiThread(int argc, char* argv[]) {
    static int s_argc = 1;
    static char* s_argv[] = { const_cast<char*>("havel"), nullptr };
    
    int qtArgc = argc > 0 ? argc : s_argc;
    char** qtArgv = (argc > 0 && argv) ? argv : s_argv;
    
    QApplication app(qtArgc, qtArgv);
    app.setQuitOnLastWindowClosed(false);
    
    QTimer timer;
    timer.setInterval(16); // ~60 FPS
    timer.setSingleShot(false);
    
    QObject::connect(&timer, &QTimer::timeout, [this]() {
        processPendingTasks();
        
        if (idleCallbackSet_.load()) {
            idleCallback_();
        }
    });
    
    timer.start();
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        running_.store(true);
    }
    queueCondition_.notify_all();
    
    int result = app.exec();
    exitCode_ = result;
    
    running_.store(false);
    queueCondition_.notify_all();
}

void GuiThreadManager::processPendingTasks() {
    std::queue<std::function<void()>> localQueue;
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!taskQueue_.empty()) {
            localQueue.push(std::move(taskQueue_.front()));
            taskQueue_.pop();
        }
    }
    
    while (!localQueue.empty()) {
        auto task = std::move(localQueue.front());
        localQueue.pop();
        try {
            task();
        } catch (const std::exception& e) {
            debug("GuiThreadManager: Task exception: {}", e.what());
        } catch (...) {
            debug("GuiThreadManager: Task unknown exception");
        }
    }
}

void GuiThreadManager::processTasks() {
    processPendingTasks();
}

void GuiThreadManager::quitEventLoop(int exitCode) {
    if (!running_.load()) return;
    
    quitting_.store(true);
    exitCode_ = exitCode;
    
    QMetaObject::invokeMethod(QApplication::instance(), [this, exitCode]() {
        QApplication::quit();
        if (exitCode != 0) {
            QApplication::exit(exitCode);
        }
    }, Qt::QueuedConnection);
}

template<typename F>
auto GuiThreadManager::postTask(F&& task) -> std::future<decltype(task())> {
    using ResultType = decltype(task());
    using PackagedTask = std::packaged_task<ResultType()>;
    
    auto pTask = std::make_shared<std::packaged_task<decltype(task())()>>(std::forward<F>(task));
    auto future = pTask->get_future();
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        taskQueue_.emplace([pTask]() { (*pTask)(); });
    }
    queueCondition_.notify_one();
    
    return future;
}

void GuiThreadManager::postTaskVoid(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        taskQueue_.emplace(std::move(task));
    }
    queueCondition_.notify_one();
}

void GuiThreadManager::setIdleCallback(std::function<void()> cb) {
    idleCallback_ = std::move(cb);
    idleCallbackSet_.store(static_cast<bool>(idleCallback_));
}

bool GuiThreadManager::isRunning() const {
    return running_.load();
}

} // namespace havel::host

#endif // HAVE_QT_EXTENSION
