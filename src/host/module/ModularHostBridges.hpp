#pragma once

/**
 * ModularHostBridges.hpp - Modular bridge components
 *
 * Each bridge handles a specific domain:
 * - IOBridge: keyboard, mouse input
 * - SystemBridge: filesystem, processes
 * - UIBridge: windows, clipboard, screenshots
 * - InputBridge: hotkeys, input remapping, AltTab
 * - MediaBridge: audio, brightness
 * - AsyncBridge: timers, async operations
 * - AutomationBridge: automation tasks
 * - BrowserBridge: browser automation
 * - ToolsBridge: text chunker
 */

#include "havel-lang/host/ExecutionPolicy.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include "havel-lang/runtime/Modules.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace havel::compiler {

/**
 * BridgeModule - Base interface for bridge components
 */
class BridgeModule {
public:
  virtual ~BridgeModule() = default;
  virtual void install(compiler::PipelineOptions &options) = 0;
};

/**
 * IOBridge - Keyboard and mouse input control
 */
class IOBridge : public BridgeModule {
public:
    explicit IOBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;

    const HostContext *ctx_;
    static Value handleSend(const std::vector<Value> &args,
                          const HostContext *ctx);
    static Value handleSendKey(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleSendText(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleWait(const std::vector<Value> &args,
                          const HostContext *ctx);
    static Value handleMouseClick(const std::vector<Value> &args,
                                const HostContext *ctx);
    static Value handleMouseMoveTo(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleMouseMoveRel(const std::vector<Value> &args,
                                  const HostContext *ctx);
    static Value handleMouseScroll(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleMouseDown(const std::vector<Value> &args,
                               const HostContext *ctx);
    static Value handleMouseUp(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleMousePos(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleMouseSetSpeed(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleMouseSetAccel(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleMouseSetDPI(const std::vector<Value> &args,
                                 const HostContext *ctx);
    static Value handleSuspend(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleKeyDown(const std::vector<Value> &args,
                               const HostContext *ctx);
    static Value handleKeyUp(const std::vector<Value> &args,
                             const HostContext *ctx);
    static Value handleGetKey(const std::vector<Value> &args,
                              const HostContext *ctx);
    static Value handleIsKeyPressed(const std::vector<Value> &args,
                                    const HostContext *ctx);
    static Value handleGetExecutorMode(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleSetExecutorMode(const std::vector<Value> &args,
                                       const HostContext *ctx);
    static Value handleMouseState(const std::vector<Value> &args,
        const HostContext *ctx);
    static Value handleMouseLastButton(const std::vector<Value> &args,
        const HostContext *ctx);
    static Value handleMouseLastState(const std::vector<Value> &args,
        const HostContext *ctx);
    static Value handleMouseButtons(const std::vector<Value> &args,
        const HostContext *ctx);
    static Value handleModifiers(const std::vector<Value> &args,
                                  const HostContext *ctx);

private:
    const HostContext *ctx_;
};

/**
 * SystemBridge - Filesystem and process operations
 */
class SystemBridge : public BridgeModule {
public:
    explicit SystemBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * UIBridge - Windows, clipboard, screenshots, AltTab
 */
class UIBridge : public BridgeModule {
public:
    explicit UIBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * InputBridge - Hotkeys, input remapping, AltTab
 */
class InputBridge : public BridgeModule {
public:
    explicit InputBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * MediaBridge - Audio, brightness
 */
class MediaBridge : public BridgeModule {
public:
    explicit MediaBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * AsyncBridge - Timers, async operations
 */
class AsyncBridge : public BridgeModule {
public:
    explicit AsyncBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * AutomationBridge - Automation tasks
 */
class AutomationBridge : public BridgeModule {
public:
    explicit AutomationBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * BrowserBridge - Browser automation
 */
class BrowserBridge : public BridgeModule {
public:
    explicit BrowserBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * ToolsBridge - Text chunker
 */
class ToolsBridge : public BridgeModule {
public:
    explicit ToolsBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * MediaBridge - Audio, brightness
 */
class MediaBridge : public BridgeModule {
public:
    explicit MediaBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * AsyncBridge - Timers, async operations
 */
class AsyncBridge : public BridgeModule {
public:
    explicit AsyncBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * AutomationBridge - Automation tasks
 */
class AutomationBridge : public BridgeModule {
public:
    explicit AutomationBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * BrowserBridge - Browser automation
 */
class BrowserBridge : public BridgeModule {
public:
    explicit BrowserBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

/**
 * ToolsBridge - Text chunker
 */
class ToolsBridge : public BridgeModule {
public:
    explicit ToolsBridge(const HostContext *ctx) : ctx_(ctx) {}
    void install(compiler::PipelineOptions &options) override;
private:
    const HostContext *ctx_;
};

} // namespace havel::compiler
