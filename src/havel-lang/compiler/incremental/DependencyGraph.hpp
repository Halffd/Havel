#pragma once

// ===== Incremental Compilation: Dependency Graph =====
//
// Tracks module/function dependencies for incremental recompilation.
// Used to determine which modules/functions need recompilation when a source
// file changes (Red/Green model per TODO.md #32).
//
// Graph edges represent "module A depends on module B" (import relationship).
// When B changes, A must be recompiled (RED). When unchanged, GREEN (reuse).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havel::compiler::incremental {

/// Node identifier in the dependency graph.
using NodeId = std::string;  // module path or function name

/// Dependency graph for incremental compilation.
/// Supports:
///   - Adding/removing nodes and edges
///   - Querying transitive dependents (what needs rebuild when X changes)
///   - Cycle detection
///   - Serialization for persistence
class DependencyGraph {
public:
    DependencyGraph() = default;

    /// Add a node (module or function).
    void addNode(const NodeId& id);

    /// Add a directed edge: from -> to (from depends on to).
    /// Returns true if edge was added, false if already exists.
    bool addEdge(const NodeId& from, const NodeId& to);

    /// Remove a node and all its edges.
    bool removeNode(const NodeId& id);

    /// Remove a specific edge.
    bool removeEdge(const NodeId& from, const NodeId& to);

    /// Get direct dependents of a node (nodes that directly depend on id).
    std::unordered_set<NodeId> dependents(const NodeId& id) const;

    /// Get direct dependencies of a node (nodes that id directly depends on).
    std::unordered_set<NodeId> dependencies(const NodeId& id) const;

    /// Get ALL transitive dependents of id (what must be rebuilt if id changes).
    std::unordered_set<NodeId> transitiveDependents(const NodeId& id) const;

    /// Get ALL transitive dependencies of id (what id depends on).
    std::unordered_set<NodeId> transitiveDependencies(const NodeId& id) const;

    /// Check if a node exists.
    bool hasNode(const NodeId& id) const;

    /// Get all nodes.
    std::vector<NodeId> nodes() const;

    /// Detect cycles in the graph. Returns empty if acyclic, else one cycle path.
    std::vector<NodeId> detectCycle() const;

    /// Topological sort (requires acyclic graph).
    std::optional<std::vector<NodeId>> topologicalOrder() const;

    /// Serialize graph for cache persistence.
    std::string serialize() const;

    /// Deserialize from serialized form.
    static std::optional<DependencyGraph> deserialize(const std::string& data);

    /// Clear the graph.
    void clear();

    /// Number of nodes.
    size_t size() const { return adj_.size(); }

    /// Number of edges.
    size_t edgeCount() const { return edge_count_; }

private:
    std::unordered_map<NodeId, std::unordered_set<NodeId>> adj_;   // outgoing edges
    std::unordered_map<NodeId, std::unordered_set<NodeId>> rev_;   // incoming edges
    size_t edge_count_ = 0;
};

/// Dependency tracker for a compilation session.
/// Tracks which modules/functions were compiled and their inputs.
class DependencyTracker {
public:
    struct CompilationUnit {
        std::string name;           // module/function name
        std::string fingerprint;    // input fingerprint (source + deps + config)
        std::vector<std::string> inputs;  // direct input files/paths
        std::vector<std::string> outputs; // output artifacts (.hvb, .hvc, etc.)
        uint64_t timestamp_ms = 0;  // compilation time
    };

    DependencyTracker() = default;

    /// Record a successful compilation.
    void recordCompilation(const CompilationUnit& unit);

    /// Get recorded compilation info for a unit.
    std::optional<CompilationUnit> getCompilation(const std::string& name) const;

    /// Check if a unit's inputs have changed (needs recompilation).
    /// Returns true if recompilation needed.
    bool needsRecompilation(const std::string& name,
                            const std::string& current_fingerprint) const;

    /// Get all units that need recompilation given changed input files.
    std::vector<std::string> getAffectedUnits(
        const std::unordered_set<std::string>& changed_inputs) const;

    /// Serialize tracker state.
    std::string serialize() const;

    /// Deserialize tracker state.
    static std::optional<DependencyTracker> deserialize(const std::string& data);

private:
    std::unordered_map<std::string, CompilationUnit> compilations_;
};

}  // namespace havel::compiler::incremental