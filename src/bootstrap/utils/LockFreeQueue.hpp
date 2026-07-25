#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace havel::utils {

template<typename T>
class LockFreeQueue {
    struct Node {
        T value;
        std::atomic<Node*> next_;
        Node() : next_(nullptr) {}
        Node(const T& val) : value(val), next_(nullptr) {}
        Node(T&& val) : value(std::move(val)), next_(nullptr) {}
    };

public:
    LockFreeQueue() {
        dummy_ = new Node();
        head_.store(dummy_, std::memory_order_relaxed);
        tail_.store(dummy_, std::memory_order_relaxed);
    }

    ~LockFreeQueue() {
        T item;
        while (pop(item)) {}
        Node* h = head_.load(std::memory_order_acquire);
        delete h;
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    void push(const T& value) {
        Node* node = new Node(value);
        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next_.store(node, std::memory_order_release);
    }

    void push(T&& value) {
        Node* node = new Node(std::move(value));
        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next_.store(node, std::memory_order_release);
    }

    bool pop(T& result) {
        Node* head_node = head_.load(std::memory_order_acquire);
        Node* next_node = head_node->next_.load(std::memory_order_acquire);
        if (next_node == nullptr) {
            return false;
        }
        result = std::move(next_node->value);
        head_.store(next_node, std::memory_order_release);
        delete head_node;
        return true;
    }

    bool empty() const {
        Node* h = head_.load(std::memory_order_acquire);
        return h->next_.load(std::memory_order_acquire) == nullptr;
    }

    size_t unsafe_size() const {
        size_t count = 0;
        const Node* node = head_.load(std::memory_order_acquire);
        while (true) {
            const Node* next = node->next_.load(std::memory_order_acquire);
            if (!next) break;
            count++;
            node = next;
        }
        return count;
    }

    void clear() {
        T item;
        while (pop(item)) {}
    }

    auto pop_all() {
        struct Drain {
            Node* head;
            Node* tail;
        };

        Node* h = head_.load(std::memory_order_acquire);
        Node* next = h->next_.load(std::memory_order_acquire);
        if (!next) return Drain{nullptr, nullptr};

        Node* new_dummy = new Node();
        head_.store(new_dummy, std::memory_order_release);
        Node* old_tail = tail_.exchange(new_dummy, std::memory_order_acq_rel);

        return Drain{next, old_tail};
    }

private:
    Node* dummy_ = nullptr;
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
};

} // namespace havel::utils
