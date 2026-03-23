/*
 * RuntimeContext.cpp - STUBBED (interpreter removed)
 * Runtime context was part of interpreter runtime
 */
#include "RuntimeContext.hpp"

namespace havel {

// STUBBED - Runtime context requires migration to bytecode VM
RuntimeContext createPureContext(void* env) {
    RuntimeContext ctx;
    ctx.env = nullptr;
    ctx.io = std::shared_ptr<IO>();
    ctx.executeShell = nullptr;
    ctx.reportError = nullptr;
    ctx.reportWarning = nullptr;
    ctx.reportInfo = nullptr;
    return ctx;
}

RuntimeContext createFullContext(void* env, IO* io) {
    RuntimeContext ctx;
    ctx.env = nullptr;
    ctx.io = std::shared_ptr<IO>(io, [](IO*){});
    ctx.executeShell = nullptr;
    ctx.reportError = nullptr;
    ctx.reportWarning = nullptr;
    ctx.reportInfo = nullptr;
    return ctx;
}

} // namespace havel
