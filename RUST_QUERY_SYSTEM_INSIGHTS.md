# Rust Query System Insights for Havel Build System

## 1. Query-Based Demand-Driven Compilation Model

### Core Architecture
- **Queries as pure functions**: Each query takes a key (e.g., `DefId`) and returns a value. Results are cached.
- **On-demand execution**: Queries only run when needed by downstream consumers.
- **Implicit dependency tracking**: Reading a query result automatically records a dependency edge in the dep-graph.
- **Parallel execution**: Multiple queries run concurrently using Rayon thread pool with latch-based synchronization.

### Key Components
```
QueryConfig          - Defines query metadata (name, dep_kind, hash_result, etc.)
QueryContext         - Trait implemented by TyCtxt providing execution environment
QueryState<K, I>     - Sharded hashmap tracking active/pending queries
QueryCache           - Trait for in-memory caches (DefaultCache, DefIdCache, SingleCache, VecCache)
QueryJob             - Represents an executing query with latch for waiters
```

### Query Execution Flow
1. **Check in-memory cache** → hit → return cached value + record dep-graph read
2. **Check active query map** → already running → wait on latch (parallel) or cycle error (sync)
3. **Start new job** → insert into active map → execute provider function
4. **On completion** → store in cache → signal latch → remove from active map
5. **On panic** → poison query → waiters receive fatal error

## 2. Incremental Compilation Dependency Tracking

### Dependency Graph Structure
- **DepNode**: `(DepKind, Fingerprint)` - uniquely identifies a query invocation
- **DepNodeIndex**: Compact integer index for current session nodes
- **SerializedDepNodeIndex**: Index into previous session's graph (for cross-session references)
- **Edges**: Stored as `Vec<DepNodeIndex>` per node (inline capacity for small edge counts)

### Red/Green Marking Algorithm
```
try_mark_green(node):
  if node not in previous graph → return None (new node, must compute)
  if node already Green → return index
  if node already Red → return None
  
  // Unknown state: recursively mark all dependencies green
  for each dep in previous_graph.edges(node):
    if !try_mark_green(dep): return None
  
  // All deps green → promote node to current graph
  promote_node_and_deps_to_current(prev_index)
  colors[prev_index] = Green(new_index)
  return Some(new_index)
```

### Key Insight: Dependency Tracking via TLS
- **TaskDeps** stored in thread-local storage during query execution
- `read_index(dep_node_index)` called automatically when query accesses another query's result
- `with_deps(TaskDepsRef::Allow(&task_deps), || task(...))` wraps query execution
- No explicit dependency declarations needed - tracking is implicit via query calls

## 3. Caching and Invalidation Strategies

### Three-Tier Caching
```
┌─────────────────────────────────────────────────────┐
│              In-Memory Query Cache                  │
│  (DefaultCache / DefIdCache / SingleCache / VecCache)│
│  - Fast lookup by key                               │
│  - Stores (value, DepNodeIndex)                     │
│  - Per-session only                                 │
└─────────────────────┬───────────────────────────────┘
                      │ cache miss
                      ▼
┌─────────────────────────────────────────────────────┐
│            On-Disk Cache (query cache file)         │
│  - Serialized query results                         │
│  - Loaded on-demand when node marked green          │
│  - Verified via fingerprint on load                 │
└─────────────────────┬───────────────────────────────┘
                      │ cache miss / fingerprint mismatch
                      ▼
┌─────────────────────────────────────────────────────┐
│              Re-execute Query Provider              │
│  - Full recomputation                               │
│  - Results written to both caches                   │
└─────────────────────────────────────────────────────┘
```

### Invalidation via Fingerprint Comparison
- **Result hashing**: `hash_result(fn(&mut StableHashingContext, &Value) -> Fingerprint)`
- **Green node**: Previous fingerprint == current fingerprint
- **Red node**: Fingerprint differs → must recompute
- **No-hash queries**: Always treated as red (conservative)

### Cache Invalidation Triggers
1. **Source file changes** → different commandline args hash → full cache invalidation
2. **Query result fingerprint mismatch** → node marked red → downstream dependents become red
3. **Missing work products** (object files) → deleted, forcing recompilation
4. **Compiler version mismatch** → cache discarded

## 4. Salsa-like Query System Architecture

### Similarities to Salsa
| Salsa Concept | Rust Query System |
|--------------|-------------------|
| `Query<K, V>` | `QueryConfig` + provider function |
| `Database` | `QueryContext` (implemented by `TyCtxt`) |
| `Memoization` | In-memory `QueryCache` + on-disk cache |
| `Dependency tracking` | Implicit via `read_index()` in TLS |
| `Invalidation` | Red/green marking + fingerprint comparison |
| `Parallel execution` | Rayon + `QueryLatch` (condvar-based) |

### Key Differences from Salsa
1. **Two-phase graph**: Separate "previous" (immutable) and "current" (building) graphs
2. **Explicit dep-node construction**: `DepNode` built from key, not auto-derived
3. **Anonymous queries**: Dep-node ID = hash of dependency set + session seed
4. **Side effects**: Diagnostics recorded as special dep-nodes, replayed on green
5. **Work products**: Separate tracking for artifacts (object files, etc.)

### Cycle Handling
- **Detection**: DFS over active query wait-for graph
- **Breaking**: Deterministic selection of latch-waited edge to resume
- **Error reporting**: Full cycle stack with spans and descriptions
- **Single-threaded**: Immediate cycle error (no latches)

## 5. Techniques Applicable to Havel

### A. Implicit Dependency Tracking
```rust
// Instead of manual dep tracking, wrap computation:
fn compute_query(key) -> Value {
    let task_deps = TaskDeps::new();
    with_deps(TaskDepsRef::Allow(&task_deps), || {
        provider(key)  // Any query calls inside automatically recorded
    })
}
```
**Apply to Havel**: Wrap bytecode compilation phases in tracked tasks. Module imports, type checking, etc. automatically build dependency graph.

### B. Red/Green Incremental Compilation
```rust
// Check if cached result is valid:
fn try_load_cached(key) -> Option<Value> {
    let dep_node = make_dep_node(key);
    if let Some((prev_idx, curr_idx)) = dep_graph.try_mark_green(dep_node) {
        // Load from on-disk cache, verify fingerprint
        return load_from_disk(key, prev_idx, curr_idx);
    }
    None  // Must recompute
}
```
**Apply to Havel**: 
- Hash AST + dependencies for each module
- On rebuild, check if module's transitive deps unchanged
- Skip recompilation if green, only re-execute red nodes

### C. Specialized Cache Types
```rust
// Havel can use different caches for different query types:
DefIdCache      → Module-level queries (keyed by ModuleId)
SingleCache     → Global queries (e.g., "all types", "all functions")  
VecCache        → Dense integer keys (e.g., function indices in module)
DefaultCache    → General purpose (e.g., type inference results)
```

### D. On-Disk Cache with Fingerprint Verification
```rust
// Save: serialize result + fingerprint
// Load: deserialize → recompute fingerprint → verify match
// Mismatch → discard, recompute
```
**Apply to Havel**: Cache bytecode, type info, symbol tables to disk. Verify on load.

### E. Work Product Tracking
```rust
// Track generated artifacts (object files, bytecode files)
struct WorkProduct {
    module_name: String,
    files: HashMap<String, PathBuf>,  // e.g., "hv" -> bytecode, "dbg" -> debug info
    content_hash: Fingerprint,         // Hash of module's exported symbols
}
```
**Apply to Havel**: Track `.hvb` bytecode files, `.hvd` debug info. Reuse if content hash matches.

### F. Anonymous Queries for Fine-Grained Tracking
```rust
// For queries without stable key (e.g., "all items in module"):
fn anon_query(dep_kind, computation) {
    let deps = track_dependencies(computation);
    let anon_id = hash(deps) ^ session_seed;
    // Multiple computations with same deps → same node → deduplicated
}
```
**Apply to Havel**: Fine-grained queries like "type of expression at span" without explicit keys.

### G. Parallel Query Execution with Latches
```rust
// Havel can use similar pattern for parallel module compilation:
struct QueryLatch {
    complete: AtomicBool,
    waiters: Mutex<Vec<Waiter>>,
    condvar: Condvar,
}
// Waiters block on condvar, notified on completion
```

### H. Command-Line Hash for Cache Invalidation
```rust
// Hash all compiler flags affecting output:
// - Optimization level
// - Target triple
// - Feature flags
// - Include paths
// Store in cache header; mismatch → full rebuild
```

## Recommended Havel Architecture

### Phase 1: Query System Foundation
1. Define `QueryContext` trait for Havel compiler context
2. Implement `QueryCache` trait with `DefIdCache` for modules, `SingleCache` for globals
3. Create `DepGraph` with `DepNode = (DepKind, Fingerprint)`
4. Implement `try_mark_green` / red-green algorithm

### Phase 2: Incremental Compilation
1. Add on-disk cache (serialize bytecode + type info + fingerprints)
2. Implement work product tracking for `.hvb` files
3. Add command-line hash for cache invalidation
4. Implement `try_load_from_disk` with fingerprint verification

### Phase 3: Parallel Execution
1. Add `QueryLatch` with condvar for cross-thread waiting
2. Integrate with thread pool (Rayon or custom)
3. Implement cycle detection/breaking for parallel queries
4. Add query stack tracking for debugging

### Phase 4: Advanced Features
1. Anonymous queries for fine-grained incremental (e.g., per-function)
2. Side-effect tracking for diagnostics
3. Incremental verification mode (`-Z incremental-verify-ich` equivalent)
4. Query profiling / self-profiling integration

## Critical Implementation Details

### Fingerprint Stability
- Use **stable hashing** (not `Hash`) for cross-session fingerprints
- Hash `DefPathHash` equivalents, not `DefId` (which change per session)
- Include session-specific seed for anonymous nodes

### Memory Efficiency
- **Sharded hashmaps** for concurrent cache access
- **Inline edge storage** (small vec) for dep-graph edges
- **IndexVec** for dense integer-indexed data
- **Arc<DepGraphData>** for shared read-only access

### Correctness Guarantees
- **Red-green invariant**: Green node = all deps green + fingerprint matches
- **Cycle soundness**: Cycle detection only on active queries, not cached results
- **Verification**: Random sampling (1/32) + optional full verification of loaded fingerprints
- **Error resilience**: Compilation errors don't corrupt incremental cache

### Performance Optimizations
- **Virtual dep-node indices** for non-incremental mode (profiling only)
- **Eval-always queries**: Skip caching/dep-tracking for always-recompute queries
- **Feedable queries**: Allow feeding values to break cycles (const generics)
- **Depth limiting**: Prevent stack overflow in deep query chains

## Summary

The Rust query system provides a **mature, production-tested architecture** for demand-driven incremental compilation. Key takeaways for Havel:

1. **Implicit dependency tracking** via TLS is simpler and less error-prone than explicit graphs
2. **Red/green marking** with fingerprint comparison is the gold standard for invalidation
3. **Three-tier caching** (memory → disk → recompute) balances speed and correctness
4. **Parallel execution** requires careful latch-based synchronization and cycle breaking
5. **Specialized caches** (DefIdCache, VecCache, etc.) provide significant memory/perf wins
6. **Work product tracking** enables artifact reuse beyond just query results

Havel should start with a simplified version (single-threaded, memory-only cache) and incrementally add disk cache, parallel execution, and advanced features.