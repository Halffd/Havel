#pragma once

namespace havel::compiler {

class EventRuntime;

/**
 * EventSource - the interface every native event producer implements.
 * (Architecture doc: "Event sources should be independently registered".)
 *
 * A source owns its native machinery (inotify, X11 connection, timer fd,
 * ...) and publishes normalized events into the EventRuntime. It never
 * calls into the VM: publish() only queues; the VM's pump dispatches.
 *
 * The language (`on <event> { ... }`) sees none of this.
 */
class EventSource {
public:
    virtual ~EventSource() = default;

    // Begin producing events into the given runtime. Idempotent.
    virtual void start(EventRuntime &runtime) = 0;
    // Stop producing and release native resources. Idempotent.
    virtual void stop() = 0;
};

} // namespace havel::compiler
