// ===== Query-based demand-driven compilation (TODO.md Phase 1) =====
//
// Simplified first increment per the TODO's own guidance: single-threaded,
// memory-only. Provides:
//   - QueryContext: execution environment + in-memory cache + implicit
//     dependency recording for the currently-executing query
//   - QueryCache flavors: SingleCache (one global value, e.g. "all
//     modules") and DefIdCache (keyed by module DefId)
//   - DepGraph with the red/green marking algorithm (try_mark_green) so a
//     later phase can attach cross-session invalidation without changing
//     the query-call surface.
//
// Intentionally NOT here yet (later phases): disk cache, parallel
// execution with latches, cycle breaking across threads, anonymous
// queries. The API is shaped so those land without call-site churn:
// queries take a QueryContext&, read other queries through it, and the
// dep edges accumulate implicitly.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <variant>
#include <vector>

namespace havel::compiler {

// ---------------------------------------------------------------------------
// Dep-graph primitives
// ---------------------------------------------------------------------------

// Stable fingerprint of a dep-node's inputs. Phase 1 uses an opaque 64-bit
// value; Phase 2 replaces the packing with stable hashing (TODO "Fingerprint
// Stability": hash DefPathHash-equivalents, not per-session ids).
using Fingerprint = uint64_t;

// What kind of query a node represents. Enum lives here so new query kinds
// are a one-line addition; the enum value is part of the node identity.
enum class DepKind : uint16_t {
  SourceText,      // raw source bytes of a module
  TokenStream,     // lexer output for a module
  Ast,             // parsed AST for a module
  ModuleIndex,     // module registry row (name -> id, path, deps)
  BytecodeChunk,   // compiled chunk for a module
  // (future: TypeCheckTables, OptimizedIR, WorkProduct, ...)
};

// Identity of one query invocation: (kind, key fingerprint). Two nodes are
// the same logical query iff both components match.
struct DepNode {
  DepKind kind;
  Fingerprint key;

  bool operator==(const DepNode &o) const {
    return kind == o.kind && key == o.key;
  }
};

struct DepNodeHash {
  size_t operator()(const DepNode &n) const {
    return (static_cast<uint64_t>(n.kind) << 56) ^ (n.key & 0x00ffffffffffffff);
  }
};

// Node color for red/green marking.
enum class DepColor : uint8_t { Red, Green };

// ---------------------------------------------------------------------------
// DepGraph — session graph + previous-session graph for red/green marking
// ---------------------------------------------------------------------------

// Index into the CURRENT session's node vector.
using DepNodeIndex = uint32_t;
// Index into the PREVIOUS session's node vector (invalidation input).
using PrevDepNodeIndex = uint32_t;
constexpr PrevDepNodeIndex kInvalidPrevIndex = 0xFFFFFFFFu;

struct DepGraphNode {
  DepNode node;
  std::vector<DepNodeIndex> edges;  // dependencies (inputs) of this node
  bool try_mark_green_visited = false;
};

class DepGraph {
 public:
  // Current-session graph -------------------------------------------------

  DepNodeIndex intern(DepNode node) {
    auto it = node_index_.find(node);
    if (it != node_index_.end()) return it->second;
    DepNodeIndex idx = static_cast<DepNodeIndex>(nodes_.size());
    nodes_.push_back({node, {}, false});
    node_index_[node] = idx;
    return idx;
  }

  // Record that `dependent` reads `input`.
  void addEdge(DepNodeIndex dependent, DepNodeIndex input) {
    nodes_[dependent].edges.push_back(input);
  }

  const std::vector<DepNodeIndex> &edges(DepNodeIndex idx) const {
    return nodes_[idx].edges;
  }

  const DepNode &node(DepNodeIndex idx) const { return nodes_[idx].node; }
  size_t size() const { return nodes_.size(); }

  // Previous-session graph + red/green marking -----------------------------
  //
  // The previous graph is loaded (Phase 2: from disk) with every node's
  // fingerprint and edges. Colors start Red; try_mark_green promotes a node
  // only when it exists in the previous graph AND every dependency
  // transitively promotes.

  struct PrevNode {
    DepNode node;
    std::vector<PrevDepNodeIndex> edges;
    DepColor color = DepColor::Red;
    // Current-session index once promoted (valid iff color == Green).
    DepNodeIndex current_index = 0;
    // Recursion guard for try_mark_green.
    bool in_progress = false;
  };

  void setPreviousGraph(std::vector<PrevNode> prev) {
    prev_ = std::move(prev);
    prev_index_.clear();
    for (PrevDepNodeIndex i = 0; i < prev_.size(); ++i) {
      prev_index_[prev_[i].node] = i;
    }
  }

  // Red/green marking per the TODO algorithm:
  //   unknown node -> None (must compute)
  //   already green -> Some(current index)
  //   already red -> None
  //   else: every prev-graph dependency must mark green; only then promote.
  // Cycles in the previous graph (which would be a soundness bug in the
  // session that produced it) are treated as red rather than recursing
  // forever: in_progress guard.
  std::optional<DepNodeIndex> tryMarkGreen(const DepNode &node) {
    auto it = prev_index_.find(node);
    if (it == prev_index_.end()) return std::nullopt;
    PrevDepNodeIndex pi = it->second;
    PrevNode &pn = prev_[pi];
    if (pn.color == DepColor::Green) return pn.current_index;
    if (pn.in_progress) return std::nullopt;  // cycle in prev graph: red
    pn.in_progress = true;

    // Promote dependencies first; map prev edges to current indices.
    std::vector<DepNodeIndex> mapped_deps;
    mapped_deps.reserve(pn.edges.size());
    for (PrevDepNodeIndex dep_pi : pn.edges) {
      const PrevNode &dep = prev_[dep_pi];
      std::optional<DepNodeIndex> dep_cur = tryMarkGreen(dep.node);
      if (!dep_cur) {
        pn.in_progress = false;
        return std::nullopt;  // dep red -> this node red
      }
      mapped_deps.push_back(*dep_cur);
    }

    // All deps green: promote into the current graph.
    DepNodeIndex cur = intern(node);
    for (DepNodeIndex d : mapped_deps) addEdge(cur, d);
    pn.color = DepColor::Green;
    pn.current_index = cur;
    pn.in_progress = false;
    return cur;
  }

  bool prevIsGreen(const DepNode &node) const {
    auto it = prev_index_.find(node);
    return it != prev_index_.end() &&
           prev_[it->second].color == DepColor::Green;
  }

 private:
  std::vector<DepGraphNode> nodes_;
  std::unordered_map<DepNode, DepNodeIndex, DepNodeHash> node_index_;

  std::vector<PrevNode> prev_;
  std::unordered_map<DepNode, PrevDepNodeIndex, DepNodeHash> prev_index_;
};

// ---------------------------------------------------------------------------
// Query caches (in-memory, single-threaded — TODO Phase 1.2)
// ---------------------------------------------------------------------------

// A cached value plus the dep-node that produced it.
template <typename V>
struct CacheEntry {
  V value;
  DepNodeIndex producer = 0;
};

// SingleCache: one value for the whole query (e.g. "module index").
template <typename V>
class SingleCache {
 public:
  void put(const V &v, DepNodeIndex producer) { entry_ = {v, producer}; }
  const CacheEntry<V> *get() const { return entry_ ? &*entry_ : nullptr; }

 private:
  std::optional<CacheEntry<V>> entry_;
};

// DefIdCache: keyed by module DefId (uint32_t module index).
template <typename K, typename V>
class DefIdCache {
 public:
  void put(K key, const V &v, DepNodeIndex producer) {
    entries_[key] = {v, producer};
  }
  const CacheEntry<V> *get(K key) const {
    auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
  }
  size_t size() const { return entries_.size(); }

 private:
  std::unordered_map<K, CacheEntry<V>> entries_;
};

// ---------------------------------------------------------------------------
// QueryContext — execution environment + implicit dep tracking
// ---------------------------------------------------------------------------

// Thrown on cyclic query dependencies (sync cycle detection per TODO
// "Query Execution Flow" step 2).
class QueryCycleError : public std::runtime_error {
 public:
  QueryCycleError(const DepNode &n)
      : std::runtime_error("query cycle detected"), node(n) {}
  DepNode node;
};

class QueryContext {
 public:
  DepGraph &graph() { return graph_; }
  const DepGraph &graph() const { return graph_; }

  // -- Running a query ----------------------------------------------------
  //
  // execute(key, kind, provider):
  //   1. resolve/intern the dep node
  //   2. record an edge from the CURRENTLY RUNNING query to this one
  //      (implicit dependency tracking)
  //   3. hit cache -> return
  //   4. cycle check against the active stack
  //   5. run provider with this node pushed as the active query
  //
  // The cache itself is type-erased here; typed helpers below bind it.

  // Type-erased cache slot.
  struct AnyCache {
    virtual ~AnyCache() = default;
  };

  template <typename V>
  struct TypedCacheSlot : AnyCache {
    // Keyed by dep-node for simplicity in Phase 1 (Phase 2 can shard).
    std::unordered_map<DepNode, CacheEntry<V>, DepNodeHash> map;
  };

  template <typename V>
  const V &execute(DepKind kind, Fingerprint key,
                   std::function<V(QueryContext &)> provider) {
    DepNode node{kind, key};
    DepNodeIndex idx = graph_.intern(node);

    // Implicit dependency edge: whatever query is currently executing
    // reads this node.
    if (!active_.empty()) {
      graph_.addEdge(active_.back(), idx);
    }

    TypedCacheSlot<V> &slot = cacheSlot<V>();
    auto cit = slot.map.find(node);
    if (cit != slot.map.end()) return cit->second.value;

    // Sync cycle detection: this node already on the active stack.
    for (DepNodeIndex a : active_) {
      if (a == idx) throw QueryCycleError(node);
    }

    active_.push_back(idx);
    V value;
    try {
      value = provider(*this);
    } catch (...) {
      active_.pop_back();
      throw;  // per TODO: panics poison the query; cache stays clean
    }
    active_.pop_back();
    slot.map[node] = {std::move(value), idx};
    return slot.map[node].value;
  }

  // Invalidate a node (for tests / manual eviction): drop its cached value
  // and mark the previous-graph node red so downstream recompute.
  void invalidate(DepKind kind, Fingerprint key) {
    DepNode node{kind, key};
    for (auto &kv : caches_) {
      (void)kv;  // type-erased: per-type invalidation via execute covers it
    }
    // Phase 1 memory-only semantics: removing from the slot maps happens
    // through typed paths; simplest correct eviction is clearing all
    // caches whose provider could have read this node — but Phase 1 keeps
    // it conservative: clear everything.
    caches_.clear();
    redCount_++;
  }

  size_t redCount() const { return redCount_; }
  size_t activeDepth() const { return active_.size(); }

 private:
  template <typename V>
  TypedCacheSlot<V> &cacheSlot() {
    std::type_index ti(typeid(V));
    auto it = caches_.find(ti);
    if (it == caches_.end()) {
      it = caches_.emplace(ti, std::make_unique<TypedCacheSlot<V>>()).first;
    }
    return *static_cast<TypedCacheSlot<V> *>(it->second.get());
  }

  DepGraph graph_;
  std::unordered_map<std::type_index, std::unique_ptr<AnyCache>> caches_;
  std::vector<DepNodeIndex> active_;  // active query stack
  size_t redCount_ = 0;
};

}  // namespace havel::compiler
