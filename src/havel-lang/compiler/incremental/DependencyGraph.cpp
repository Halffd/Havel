#include "DependencyGraph.hpp"

#include <algorithm>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace havel::compiler::incremental {

void DependencyGraph::addNode(const NodeId& id) {
    if (!adj_.count(id)) {
        adj_[id] = {};
        rev_[id] = {};
    }
}

bool DependencyGraph::addEdge(const NodeId& from, const NodeId& to) {
    addNode(from);
    addNode(to);
    if (adj_[from].insert(to).second) {
        rev_[to].insert(from);
        ++edge_count_;
        return true;
    }
    return false;
}

bool DependencyGraph::removeNode(const NodeId& id) {
    if (!adj_.count(id)) return false;

    // Remove all incoming edges
    for (const auto& pred : rev_[id]) {
        adj_[pred].erase(id);
        --edge_count_;
    }
    // Remove all outgoing edges
    for (const auto& succ : adj_[id]) {
        rev_[succ].erase(id);
        --edge_count_;
    }
    adj_.erase(id);
    rev_.erase(id);
    return true;
}

bool DependencyGraph::removeEdge(const NodeId& from, const NodeId& to) {
    if (!adj_.count(from)) return false;
    if (adj_[from].erase(to)) {
        rev_[to].erase(from);
        --edge_count_;
        return true;
    }
    return false;
}

std::unordered_set<NodeId> DependencyGraph::dependents(const NodeId& id) const {
    if (rev_.count(id)) return rev_.at(id);
    return {};
}

std::unordered_set<NodeId> DependencyGraph::dependencies(const NodeId& id) const {
    if (adj_.count(id)) return adj_.at(id);
    return {};
}

std::unordered_set<NodeId> DependencyGraph::transitiveDependents(const NodeId& id) const {
    std::unordered_set<NodeId> result;
    std::queue<NodeId> q;
    q.push(id);
    while (!q.empty()) {
        NodeId cur = q.front();
        q.pop();
        for (const auto& dep : rev_.at(cur)) {
            if (result.insert(dep).second) {
                q.push(dep);
            }
        }
    }
    return result;
}

std::unordered_set<NodeId> DependencyGraph::transitiveDependencies(const NodeId& id) const {
    std::unordered_set<NodeId> result;
    std::queue<NodeId> q;
    q.push(id);
    while (!q.empty()) {
        NodeId cur = q.front();
        q.pop();
        for (const auto& dep : adj_.at(cur)) {
            if (result.insert(dep).second) {
                q.push(dep);
            }
        }
    }
    return result;
}

bool DependencyGraph::hasNode(const NodeId& id) const {
    return adj_.count(id) > 0;
}

std::vector<NodeId> DependencyGraph::nodes() const {
    std::vector<NodeId> result;
    result.reserve(adj_.size());
    for (const auto& [id, _] : adj_) result.push_back(id);
    return result;
}

std::vector<NodeId> DependencyGraph::detectCycle() const {
    std::unordered_map<NodeId, int> state;  // 0=unvisited, 1=visiting, 2=done
    std::vector<NodeId> path;

    std::function<bool(const NodeId&)> dfs = [&](const NodeId& u) -> bool {
        state[u] = 1;
        path.push_back(u);
        for (const auto& v : adj_.at(u)) {
            int st = state[v];
            if (st == 1) {
                // Found cycle - path from v to u
                auto it = std::find(path.begin(), path.end(), v);
                if (it != path.end()) {
                    path.erase(path.begin(), it);
                }
                return true;
            }
            if (st == 0 && dfs(v)) return true;
        }
        state[u] = 2;
        path.pop_back();
        return false;
    };

    for (const auto& [id, _] : adj_) {
        if (state[id] == 0) {
            path.clear();
            if (dfs(id)) return path;
        }
    }
    return {};
}

std::optional<std::vector<NodeId>> DependencyGraph::topologicalOrder() const {
    std::unordered_map<NodeId, int> indegree;
    for (const auto& [u, outs] : adj_) {
        indegree.try_emplace(u, 0);
        for (const auto& v : outs) indegree[v]++;
    }

    std::queue<NodeId> q;
    for (const auto& [id, deg] : indegree) {
        if (deg == 0) q.push(id);
    }

    std::vector<NodeId> order;
    while (!q.empty()) {
        NodeId u = q.front();
        q.pop();
        order.push_back(u);
        for (const auto& v : adj_.at(u)) {
            if (--indegree[v] == 0) q.push(v);
        }
    }

    if (order.size() != adj_.size()) return std::nullopt;  // cycle
    return order;
}

std::string DependencyGraph::serialize() const {
    std::ostringstream oss;
    oss << "DEPGRAF v1\n";
    oss << nodes().size() << " " << edge_count_ << "\n";
    for (const auto& id : nodes()) {
        oss << "N " << id << "\n";
    }
    for (const auto& [from, outs] : adj_) {
        for (const auto& to : outs) {
            oss << "E " << from << " " << to << "\n";
        }
    }
    return oss.str();
}

std::optional<DependencyGraph> DependencyGraph::deserialize(const std::string& data) {
    DependencyGraph g;
    std::istringstream iss(data);
    std::string line;
    if (!std::getline(iss, line) || line != "DEPGRAF v1") return std::nullopt;

    size_t node_count, edge_count;
    if (!std::getline(iss, line)) return std::nullopt;
    std::istringstream hdr(line);
    if (!(hdr >> node_count >> edge_count)) return std::nullopt;

    for (size_t i = 0; i < node_count; ++i) {
        if (!std::getline(iss, line)) return std::nullopt;
        if (line.size() < 2 || line[0] != 'N' || line[1] != ' ') return std::nullopt;
        g.addNode(line.substr(2));
    }

    for (size_t i = 0; i < edge_count; ++i) {
        if (!std::getline(iss, line)) return std::nullopt;
        if (line.size() < 2 || line[0] != 'E' || line[1] != ' ') return std::nullopt;
        std::istringstream eiss(line.substr(2));
        std::string from, to;
        if (!(eiss >> from >> to)) return std::nullopt;
        g.addEdge(from, to);
    }

    if (g.edgeCount() != edge_count) return std::nullopt;
    return g;
}

void DependencyGraph::clear() {
    adj_.clear();
    rev_.clear();
    edge_count_ = 0;
}

// DependencyTracker implementation

void DependencyTracker::recordCompilation(const CompilationUnit& unit) {
    compilations_[unit.name] = unit;
}

std::optional<DependencyTracker::CompilationUnit> DependencyTracker::getCompilation(
    const std::string& name) const {
    auto it = compilations_.find(name);
    if (it != compilations_.end()) return it->second;
    return std::nullopt;
}

bool DependencyTracker::needsRecompilation(
    const std::string& name, const std::string& current_fingerprint) const {
    auto it = compilations_.find(name);
    if (it == compilations_.end()) return true;  // never compiled
    return it->second.fingerprint != current_fingerprint;
}

std::vector<std::string> DependencyTracker::getAffectedUnits(
    const std::unordered_set<std::string>& changed_inputs) const {
    std::vector<std::string> affected;
    for (const auto& [name, unit] : compilations_) {
        for (const auto& input : unit.inputs) {
            if (changed_inputs.count(input)) {
                affected.push_back(name);
                break;
            }
        }
    }
    return affected;
}

std::string DependencyTracker::serialize() const {
    std::ostringstream oss;
    oss << "DEPLOCK v1\n";
    oss << compilations_.size() << "\n";
    for (const auto& [name, unit] : compilations_) {
        oss << "U " << name << "\n";
        oss << "  FP " << unit.fingerprint << "\n";
        oss << "  TS " << unit.timestamp_ms << "\n";
        oss << "  IN " << unit.inputs.size();
        for (const auto& in : unit.inputs) oss << " " << in;
        oss << "\n";
        oss << "  OUT " << unit.outputs.size();
        for (const auto& out : unit.outputs) oss << " " << out;
        oss << "\n";
    }
    return oss.str();
}

std::optional<DependencyTracker> DependencyTracker::deserialize(const std::string& data) {
    DependencyTracker t;
    std::istringstream iss(data);
    std::string line;
    if (!std::getline(iss, line) || line != "DEPLOCK v1") return std::nullopt;

    size_t count;
    if (!std::getline(iss, line)) return std::nullopt;
    std::istringstream hdr(line);
    if (!(hdr >> count)) return std::nullopt;

    for (size_t i = 0; i < count; ++i) {
        CompilationUnit unit;
        if (!std::getline(iss, line) || line.size() < 2 || line[0] != 'U' || line[1] != ' ')
            return std::nullopt;
        unit.name = line.substr(2);

        if (!std::getline(iss, line) || line.size() < 4 || line.substr(0, 4) != "  FP ")
            return std::nullopt;
        unit.fingerprint = line.substr(4);

        if (!std::getline(iss, line) || line.size() < 4 || line.substr(0, 4) != "  TS ")
            return std::nullopt;
        unit.timestamp_ms = std::stoull(line.substr(4));

        if (!std::getline(iss, line) || line.size() < 4 || line.substr(0, 4) != "  IN ")
            return std::nullopt;
        std::istringstream inss(line.substr(4));
        size_t in_count;
        if (!(inss >> in_count)) return std::nullopt;
        unit.inputs.resize(in_count);
        for (size_t j = 0; j < in_count; ++j) {
            if (!(inss >> unit.inputs[j])) return std::nullopt;
        }

        if (!std::getline(iss, line) || line.size() < 5 || line.substr(0, 5) != "  OUT ")
            return std::nullopt;
        std::istringstream outss(line.substr(5));
        size_t out_count;
        if (!(outss >> out_count)) return std::nullopt;
        unit.outputs.resize(out_count);
        for (size_t j = 0; j < out_count; ++j) {
            if (!(outss >> unit.outputs[j])) return std::nullopt;
        }

        t.recordCompilation(unit);
    }
    return t;
}

}  // namespace havel::compiler::incremental