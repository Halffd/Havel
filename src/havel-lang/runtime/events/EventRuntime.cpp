#include "EventRuntime.hpp"
#include <iostream>
#include <cstdlib>

namespace havel::compiler {

EventRuntime::SubscriptionId
EventRuntime::subscribe(const std::string &name, HandlerFn handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    SubscriptionId id = next_id_.fetch_add(1);
    subscriptions_[name].push_back(Subscription{id, name, std::move(handler)});
    return id;
}

bool EventRuntime::unsubscribe(SubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &[name, subs] : subscriptions_) {
        for (auto it = subs.begin(); it != subs.end(); ++it) {
            if (it->id == id) {
                subs.erase(it);
                if (subs.empty()) {
                    subscriptions_.erase(name);
                }
                return true;
            }
        }
    }
    return false;
}

void EventRuntime::clearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_.clear();
    queue_.clear();
}

void EventRuntime::publish(const std::string &name, EventPayload payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Snapshot the matching handlers at publish time: a later
    // unsubscribe/cancel must not retroactively drop events that were
    // published while the subscription existed.
    QueuedEvent queued{name, std::move(payload), {}};
    auto it = subscriptions_.find(name);
    if (it != subscriptions_.end()) {
        for (const auto &sub : it->second) {
            queued.handlers.push_back(sub.handler);
        }
    }
    queue_.push_back(std::move(queued));
}

void EventRuntime::dispatch() {
    // Snapshot the queue under the lock, then run handlers unlocked so a
    // handler that publishes does not recurse into this dispatch.
    std::deque<QueuedEvent> local;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        local.swap(queue_);
    }
    while (!local.empty()) {
        QueuedEvent event = std::move(local.front());
        local.pop_front();
        // The matching handlers were snapshotted at publish time — a
        // cancel after publish cannot retroactively drop the event.
        for (auto &handler : event.handlers) {
            if (handler) {
                handler(event.payload);
            }
        }
    }
}

size_t EventRuntime::subscriptionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto &[name, subs] : subscriptions_) {
        count += subs.size();
    }
    return count;
}

size_t EventRuntime::queuedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void EventRuntime::setSubscriptionArg(SubscriptionId id, std::string arg) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscription_args_[id] = std::move(arg);
}

std::string EventRuntime::getSubscriptionArg(SubscriptionId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscription_args_.find(id);
    return it != subscription_args_.end() ? it->second : std::string();
}

} // namespace havel::compiler
