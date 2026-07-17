#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace havel::utils {

template<typename Key, typename Value, typename Hash = std::hash<Key>,
         typename KeyEqual = std::equal_to<Key>>
class RobinHoodHashMap {
    struct Entry {
        alignas(Key) unsigned char key_storage[sizeof(Key)];
        alignas(Value) unsigned char val_storage[sizeof(Value)];
        int32_t dib = -1;

        Key& key() { return *reinterpret_cast<Key*>(key_storage); }
        Value& value() { return *reinterpret_cast<Value*>(val_storage); }
        const Key& key() const { return *reinterpret_cast<const Key*>(key_storage); }
        const Value& value() const { return *reinterpret_cast<const Value*>(val_storage); }
    };

    static constexpr float DEFAULT_MAX_LOAD = 0.7f;
    static constexpr size_t MIN_CAP = 8;

    Entry* entries_ = nullptr;
    size_t capacity_ = 0;
    size_t size_ = 0;
    float max_load_ = DEFAULT_MAX_LOAD;
    Hash hasher_;
    KeyEqual key_equal_;

    size_t mask() const { return capacity_ - 1; }

    void alloc_entries(size_t n) {
        entries_ = static_cast<Entry*>(::operator new[](n * sizeof(Entry)));
        for (size_t i = 0; i < n; i++)
            entries_[i].dib = -1;
    }

    void free_entries() {
        ::operator delete[](entries_);
    }

    void destroy_all() {
        for (size_t i = 0; i < capacity_; i++) {
            if (entries_[i].dib >= 0) {
                entries_[i].key().~Key();
                entries_[i].value().~Value();
                entries_[i].dib = -1;
            }
        }
        size_ = 0;
    }

    void init_entry(Entry& e, Key&& k, Value&& v, int32_t d) {
        new (&e.key()) Key(std::move(k));
        new (&e.value()) Value(std::move(v));
        e.dib = d;
    }

    void init_entry(Entry& e, const Key& k, const Value& v, int32_t d) {
        new (&e.key()) Key(k);
        new (&e.value()) Value(v);
        e.dib = d;
    }

    template<typename K, typename V>
    void init_entry_fwd(Entry& e, K&& k, V&& v, int32_t d) {
        new (&e.key()) Key(std::forward<K>(k));
        new (&e.value()) Value(std::forward<V>(v));
        e.dib = d;
    }

    void destroy_entry(Entry& e) {
        e.key().~Key();
        e.value().~Value();
        e.dib = -1;
    }

    void rehash(size_t new_cap) {
        Entry* old = entries_;
        size_t old_cap = capacity_;

        capacity_ = new_cap;
        alloc_entries(new_cap);
        size_ = 0;

        for (size_t i = 0; i < old_cap; i++) {
            if (old[i].dib >= 0) {
                insert_or_die(std::move(old[i].key()), std::move(old[i].value()));
                old[i].key().~Key();
                old[i].value().~Value();
            }
        }

        ::operator delete[](old);
    }

    void maybe_grow() {
        if (size_ + 1 > static_cast<size_t>(capacity_ * max_load_)) {
            rehash(capacity_ * 2);
        }
    }

    void insert_or_die(Key&& key, Value&& value) {
        size_t index = hasher_(key) & mask();
        size_t pos = index;
        int32_t dib = 0;

        while (true) {
            Entry& e = entries_[pos];

            if (e.dib < 0) {
                init_entry(e, std::move(key), std::move(value), dib);
                size_++;
                return;
            }

            if (e.dib < dib) {
                std::swap(e.key(), key);
                std::swap(e.value(), value);
                std::swap(e.dib, dib);
            }

            pos = (pos + 1) & mask();
            dib++;
        }
    }

public:
    class const_iterator;

    class iterator {
        friend class RobinHoodHashMap;
        Entry* ptr_ = nullptr;
        Entry* end_ = nullptr;

        void skip_empty() {
            while (ptr_ != end_ && ptr_->dib < 0) ++ptr_;
        }

        iterator(Entry* ptr, Entry* end) : ptr_(ptr), end_(end) { skip_empty(); }

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const Key, Value>;
        using reference = std::pair<const Key&, Value&>;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        struct ArrowProxy {
            const Key& first;
            Value& second;

            ArrowProxy* operator->() { return this; }
            const ArrowProxy* operator->() const { return this; }
        };

        reference operator*() const { return {ptr_->key(), ptr_->value()}; }
        ArrowProxy operator->() const { return {ptr_->key(), ptr_->value()}; }

        iterator& operator++() { ++ptr_; skip_empty(); return *this; }
        bool operator==(const iterator& o) const { return ptr_ == o.ptr_; }
        bool operator!=(const iterator& o) const { return ptr_ != o.ptr_; }

        const Key& key() const { return ptr_->key(); }
        Value& value() const { return ptr_->value(); }
    };

    class const_iterator {
        friend class RobinHoodHashMap;
        const Entry* ptr_ = nullptr;
        const Entry* end_ = nullptr;

        void skip_empty() {
            while (ptr_ != end_ && ptr_->dib < 0) ++ptr_;
        }

        const_iterator(const Entry* ptr, const Entry* end) : ptr_(ptr), end_(end) { skip_empty(); }

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const Key, const Value>;
        using reference = std::pair<const Key&, const Value&>;
        using difference_type = std::ptrdiff_t;

        const_iterator() = default;
        const_iterator(const iterator& it) : ptr_(it.ptr_), end_(it.end_) {}

        struct ArrowProxy {
            const Key& first;
            const Value& second;

            ArrowProxy* operator->() { return this; }
            const ArrowProxy* operator->() const { return this; }
        };

        reference operator*() const { return {ptr_->key(), ptr_->value()}; }
        ArrowProxy operator->() const { return {ptr_->key(), ptr_->value()}; }

        const_iterator& operator++() { ++ptr_; skip_empty(); return *this; }
        bool operator==(const const_iterator& o) const { return ptr_ == o.ptr_; }
        bool operator!=(const const_iterator& o) const { return ptr_ != o.ptr_; }
    };

    RobinHoodHashMap(size_t initial_cap = MIN_CAP, float max_load = DEFAULT_MAX_LOAD)
        : max_load_(max_load)
    {
        if (max_load_ < 0.1f) max_load_ = 0.1f;
        if (max_load_ > 0.95f) max_load_ = 0.95f;
        size_t cap = MIN_CAP;
        while (cap < initial_cap) cap *= 2;
        capacity_ = cap;
        alloc_entries(cap);
    }

    ~RobinHoodHashMap() {
        if (entries_) {
            destroy_all();
            free_entries();
        }
    }

    RobinHoodHashMap(const RobinHoodHashMap& other)
        : capacity_(other.capacity_), size_(other.size_),
          max_load_(other.max_load_), hasher_(other.hasher_),
          key_equal_(other.key_equal_)
    {
        alloc_entries(capacity_);
        for (size_t i = 0; i < capacity_; i++) {
            if (other.entries_[i].dib >= 0) {
                Entry& e = entries_[i];
                new (&e.key()) Key(other.entries_[i].key());
                new (&e.value()) Value(other.entries_[i].value());
                e.dib = other.entries_[i].dib;
            }
        }
    }

    RobinHoodHashMap& operator=(const RobinHoodHashMap& other) {
        if (this != &other) {
            RobinHoodHashMap tmp(other);
            swap(tmp);
        }
        return *this;
    }

    RobinHoodHashMap(RobinHoodHashMap&& other) noexcept
        : entries_(other.entries_), capacity_(other.capacity_),
          size_(other.size_), max_load_(other.max_load_),
          hasher_(std::move(other.hasher_)),
          key_equal_(std::move(other.key_equal_))
    {
        other.entries_ = nullptr;
        other.capacity_ = 0;
        other.size_ = 0;
    }

    RobinHoodHashMap& operator=(RobinHoodHashMap&& other) noexcept {
        if (this != &other) {
            if (entries_) {
                destroy_all();
                free_entries();
            }
            entries_ = other.entries_;
            capacity_ = other.capacity_;
            size_ = other.size_;
            max_load_ = other.max_load_;
            hasher_ = std::move(other.hasher_);
            key_equal_ = std::move(other.key_equal_);
            other.entries_ = nullptr;
            other.capacity_ = 0;
            other.size_ = 0;
        }
        return *this;
    }

    void swap(RobinHoodHashMap& other) noexcept {
        std::swap(entries_, other.entries_);
        std::swap(capacity_, other.capacity_);
        std::swap(size_, other.size_);
        std::swap(max_load_, other.max_load_);
        std::swap(hasher_, other.hasher_);
        std::swap(key_equal_, other.key_equal_);
    }

    void reserve(size_t n) {
        size_t needed = static_cast<size_t>(n / max_load_) + 1;
        if (needed > capacity_) {
            size_t new_cap = capacity_;
            while (new_cap < needed) new_cap *= 2;
            rehash(new_cap);
        }
    }

    std::pair<iterator, bool> insert(const Key& key, const Value& value) {
        maybe_grow();
        size_t index = hasher_(key) & mask();
        size_t pos = index;
        int32_t dib = 0;
        Key k = key;
        Value v = value;

        while (true) {
            Entry& e = entries_[pos];

            if (e.dib < 0) {
                init_entry(e, std::move(k), std::move(v), dib);
                size_++;
                auto it = find(key);
                return {it, true};
            }

            if (key_equal_(e.key(), k)) {
                return {iterator(&e, entries_ + capacity_), false};
            }

            if (e.dib < dib) {
                std::swap(e.key(), k);
                std::swap(e.value(), v);
                std::swap(e.dib, dib);
            }

            pos = (pos + 1) & mask();
            dib++;
        }
    }

    std::pair<iterator, bool> insert(std::pair<Key, Value>&& p) {
        return insert_or_assign(std::move(p.first), std::move(p.second));
    }

    template<typename K, typename V>
    std::pair<iterator, bool> insert_or_assign(K&& key, V&& value) {
        maybe_grow();

        size_t index = hasher_(key) & mask();
        size_t pos = index;
        int32_t dib = 0;

        while (true) {
            Entry& e = entries_[pos];

            if (e.dib < 0) {
                init_entry_fwd(e, std::forward<K>(key), std::forward<V>(value), dib);
                size_++;
                auto it = find(e.key());
                return {it, true};
            }

            if (key_equal_(e.key(), key)) {
                e.value() = std::forward<V>(value);
                return {iterator(&e, entries_ + capacity_), false};
            }

            if (e.dib < dib) {
                std::swap(e.key(), key);
                std::swap(e.value(), value);
                std::swap(e.dib, dib);
            }

            pos = (pos + 1) & mask();
            dib++;
        }
    }

    template<typename... Args>
    std::pair<iterator, bool> emplace(const Key& key, Args&&... args) {
        maybe_grow();
        size_t index = hasher_(key) & mask();
        size_t pos = index;
        int32_t dib = 0;
        Key k = key;
        Value v(std::forward<Args>(args)...);

        while (true) {
            Entry& e = entries_[pos];

            if (e.dib < 0) {
                e.key() = std::move(k);
                e.value() = std::move(v);
                e.dib = dib;
                size_++;
                auto it = find(e.key());
                return {it, true};
            }

            if (key_equal_(e.key(), k)) {
                return {iterator(&e, entries_ + capacity_), false};
            }

            if (e.dib < dib) {
                std::swap(e.key(), k);
                std::swap(e.value(), v);
                std::swap(e.dib, dib);
            }

            pos = (pos + 1) & mask();
            dib++;
        }
    }

    template<typename... Args>
    std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
        return emplace(key, std::forward<Args>(args)...);
    }

    iterator find(const Key& key) {
        size_t index = hasher_(key) & mask();
        size_t pos = index;
        int32_t dist = 0;

        while (true) {
            Entry& e = entries_[pos];
            if (e.dib < 0) return end();
            if (e.dib < dist) return end();
            if (key_equal_(e.key(), key)) return iterator(&e, entries_ + capacity_);
            pos = (pos + 1) & mask();
            dist++;
        }
    }

    const_iterator find(const Key& key) const {
        size_t index = hasher_(key) & mask();
        size_t pos = index;
        int32_t dist = 0;

        while (true) {
            const Entry& e = entries_[pos];
            if (e.dib < 0) return end();
            if (e.dib < dist) return end();
            if (key_equal_(e.key(), key)) return const_iterator(&e, entries_ + capacity_);
            pos = (pos + 1) & mask();
            dist++;
        }
    }

    bool contains(const Key& key) const {
        return find(key) != end();
    }

    Value& at(const Key& key) {
        auto it = find(key);
        if (it == end()) throw std::out_of_range("RobinHoodHashMap key not found");
        return it->second;
    }

    const Value& at(const Key& key) const {
        auto it = find(key);
        if (it == end()) throw std::out_of_range("RobinHoodHashMap key not found");
        return it->second;
    }

    Value& operator[](const Key& key) {
        auto it = find(key);
        if (it != end()) return it->second;
        insert(key, Value());
        return find(key)->second;
    }

    bool erase(const Key& key) {
        size_t index = hasher_(key) & mask();
        size_t pos = index;
        int32_t dist = 0;

        while (true) {
            Entry& e = entries_[pos];
            if (e.dib < 0) return false;
            if (e.dib < dist) return false;
            if (key_equal_(e.key(), key)) break;
            pos = (pos + 1) & mask();
            dist++;
        }

        destroy_entry(entries_[pos]);
        size_--;

        size_t shift = pos;
        while (true) {
            size_t next = (shift + 1) & mask();
            if (entries_[next].dib <= 0) break;

            new (&entries_[shift].key()) Key(std::move(entries_[next].key()));
            new (&entries_[shift].value()) Value(std::move(entries_[next].value()));
            entries_[shift].dib = entries_[next].dib - 1;

            entries_[next].key().~Key();
            entries_[next].value().~Value();
            entries_[next].dib = -1;

            shift = next;
        }

        return true;
    }

    void clear() {
        destroy_all();
        for (size_t i = 0; i < capacity_; i++) {
            entries_[i].dib = -1;
        }
        size_ = 0;
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    size_t capacity() const { return capacity_; }
    float load_factor() const { return static_cast<float>(size_) / capacity_; }

    iterator begin() { return iterator(entries_, entries_ + capacity_); }
    iterator end() { return iterator(entries_ + capacity_, entries_ + capacity_); }
    const_iterator begin() const { return const_iterator(entries_, entries_ + capacity_); }
    const_iterator end() const { return const_iterator(entries_ + capacity_, entries_ + capacity_); }
};

} // namespace havel::utils
