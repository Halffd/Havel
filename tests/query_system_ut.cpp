// Query-system unit tests (TODO.md Phase 1: Query System Foundation).
//
// Exercises QueryContext (execute/caching/cycle detection/implicit dep
// edges), the SingleCache/DefIdCache flavors, and DepGraph red/green
// marking (try_mark_green promotion rules). One test = one concern.

#include "QuerySystem.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace havel::compiler {
namespace {

// A counter-based provider: records invocation counts so cache hits are
// observable.
struct CallCounter {
  int calls = 0;
};

TEST(QueryContextTest, ExecutesProviderAndCaches) {
  QueryContext ctx;
  CallCounter cc;

  const std::string &a = ctx.execute<std::string>(
      DepKind::SourceText, 1, [&](QueryContext &) {
        cc.calls++;
        return std::string("src-a");
      });
  EXPECT_EQ(a, "src-a");
  EXPECT_EQ(cc.calls, 1);

  // Second execution is a cache hit: provider must not run again.
  const std::string &b = ctx.execute<std::string>(
      DepKind::SourceText, 1, [&](QueryContext &) {
        cc.calls++;
        return std::string("src-a");
      });
  EXPECT_EQ(b, "src-a");
  EXPECT_EQ(cc.calls, 1);
}

TEST(QueryContextTest, DifferentKeysAndKindsCacheIndependently) {
  QueryContext ctx;
  int source_calls = 0;
  int ast_calls = 0;

  auto s1 = ctx.execute<std::string>(
      DepKind::SourceText, 1,
      [&](QueryContext &) { source_calls++; return std::string("s1"); });
  auto s2 = ctx.execute<std::string>(
      DepKind::SourceText, 2,
      [&](QueryContext &) { source_calls++; return std::string("s2"); });
  auto a1 = ctx.execute<int>(
      DepKind::Ast, 1, [&](QueryContext &) { ast_calls++; return 42; });

  EXPECT_EQ(s1, "s1");
  EXPECT_EQ(s2, "s2");
  EXPECT_EQ(a1, 42);
  EXPECT_EQ(source_calls, 2);
  EXPECT_EQ(ast_calls, 1);

  // Repeat all: zero new provider runs.
  ctx.execute<std::string>(DepKind::SourceText, 1, [&](QueryContext &) {
    source_calls++; return std::string("s1");
  });
  ctx.execute<std::string>(DepKind::SourceText, 2, [&](QueryContext &) {
    source_calls++; return std::string("s2");
  });
  ctx.execute<int>(DepKind::Ast, 1,
                   [&](QueryContext &) { ast_calls++; return 42; });
  EXPECT_EQ(source_calls, 2);
  EXPECT_EQ(ast_calls, 1);
}

TEST(QueryContextTest, RecordsImplicitDependencyEdges) {
  QueryContext ctx;
  // ast(1) reads source(1): the provider calls execute for SourceText.
  ctx.execute<int>(DepKind::Ast, 1, [&](QueryContext &c) {
    const std::string &src = c.execute<std::string>(
        DepKind::SourceText, 1,
        [](QueryContext &) { return std::string("module text"); });
    return static_cast<int>(src.size());
  });

  DepGraph &g = ctx.graph();
  ASSERT_EQ(g.size(), 2u);  // SourceText(1) + Ast(1)
  // Find the Ast node and check it has exactly one edge to SourceText.
  bool found_edge = false;
  for (uint32_t i = 0; i < g.size(); ++i) {
    if (g.node(i).kind == DepKind::Ast) {
      ASSERT_EQ(g.edges(i).size(), 1u);
      EXPECT_EQ(g.node(g.edges(i)[0]).kind, DepKind::SourceText);
      found_edge = true;
    }
  }
  EXPECT_TRUE(found_edge);
}

TEST(QueryContextTest, TransitiveDependenciesChain) {
  QueryContext ctx;
  // bytecode(1) -> ast(1) -> source(1)
  int source_calls = 0;
  ctx.execute<int>(DepKind::BytecodeChunk, 1, [&](QueryContext &c) {
    int node_count = c.execute<int>(DepKind::Ast, 1, [&](QueryContext &c2) {
      const std::string &src = c2.execute<std::string>(
          DepKind::SourceText, 1, [&](QueryContext &) {
            source_calls++;
            return std::string("abc");
          });
      return static_cast<int>(src.size());
    });
    return node_count * 2;
  });
  EXPECT_EQ(source_calls, 1);

  DepGraph &g = ctx.graph();
  EXPECT_EQ(g.size(), 3u);

  // The chain: BytecodeChunk -> Ast -> SourceText.
  std::vector<DepKind> chain;
  for (uint32_t i = 0; i < g.size(); ++i) {
    if (g.node(i).kind == DepKind::BytecodeChunk) {
      ASSERT_EQ(g.edges(i).size(), 1u);
      uint32_t ast_idx = g.edges(i)[0];
      ASSERT_EQ(g.node(ast_idx).kind, DepKind::Ast);
      ASSERT_EQ(g.edges(ast_idx).size(), 1u);
      EXPECT_EQ(g.node(g.edges(ast_idx)[0]).kind, DepKind::SourceText);
      chain.push_back(DepKind::BytecodeChunk);
    }
  }
  EXPECT_EQ(chain.size(), 1u);
}

TEST(QueryContextTest, DetectsDirectCycle) {
  QueryContext ctx;
  // A query whose provider reads itself: must throw QueryCycleError,
  // not recurse forever.
  EXPECT_THROW(
      {
        ctx.execute<int>(DepKind::Ast, 7, [&](QueryContext &c) {
          return c.execute<int>(DepKind::Ast, 7,
                                [](QueryContext &) { return 1; });
        });
      },
      QueryCycleError);
  // Cycle abort leaves the stack unwound: subsequent queries still work.
  int v = ctx.execute<int>(DepKind::Ast, 8,
                           [](QueryContext &) { return 5; });
  EXPECT_EQ(v, 5);
  EXPECT_EQ(ctx.activeDepth(), 0u);
}

TEST(QueryContextTest, ProviderPanicLeavesCacheClean) {
  QueryContext ctx;
  // First call throws: nothing cached.
  EXPECT_THROW(
      {
        ctx.execute<int>(DepKind::Ast, 3, [](QueryContext &) -> int {
          throw std::runtime_error("provider blew up");
        });
      },
      std::runtime_error);
  // Second call runs the provider again (cache was not poisoned) and
  // succeeds.
  int calls = 0;
  int v = ctx.execute<int>(DepKind::Ast, 3, [&](QueryContext &) {
    calls++;
    return 11;
  });
  EXPECT_EQ(v, 11);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(ctx.activeDepth(), 0u);
}

// ---------------------------------------------------------------------------
// Cache flavors
// ---------------------------------------------------------------------------

TEST(CacheFlavorsTest, SingleCacheStoresOneEntry) {
  SingleCache<std::string> cache;
  EXPECT_EQ(cache.get(), nullptr);
  cache.put("the-module-index", 7);
  const auto *e = cache.get();
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->value, "the-module-index");
  EXPECT_EQ(e->producer, 7u);
  // Overwrite semantics: one slot.
  cache.put("updated", 9);
  e = cache.get();
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->value, "updated");
  EXPECT_EQ(e->producer, 9u);
}

TEST(CacheFlavorsTest, DefIdCacheKeysByModuleId) {
  DefIdCache<uint32_t, std::string> cache;
  EXPECT_EQ(cache.get(1), nullptr);
  cache.put(1, "chunk-one", 10);
  cache.put(2, "chunk-two", 11);
  const auto *a = cache.get(1);
  const auto *b = cache.get(2);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(a->value, "chunk-one");
  EXPECT_EQ(b->value, "chunk-two");
  EXPECT_EQ(cache.size(), 2u);
  EXPECT_EQ(cache.get(3), nullptr);
}

// ---------------------------------------------------------------------------
// DepGraph red/green marking
// ---------------------------------------------------------------------------

TEST(DepGraphTest, TryMarkGreenUnknownNodeReturnsNull) {
  DepGraph g;
  EXPECT_EQ(g.tryMarkGreen(DepNode{DepKind::SourceText, 99}), std::nullopt);
}

TEST(DepGraphTest, TryMarkGreenPromotesUnchangedChain) {
  // Previous session: bytecode(1) -> ast(1) -> source(1).
  DepGraph g;
  std::vector<DepGraph::PrevNode> prev(3);
  prev[0] = {{DepKind::SourceText, 1}, {}, DepColor::Red, 0, false};
  prev[1] = {{DepKind::Ast, 1}, {0}, DepColor::Red, 0, false};
  prev[2] = {{DepKind::BytecodeChunk, 1}, {1}, DepColor::Red, 0, false};
  g.setPreviousGraph(std::move(prev));

  // Mark the root green: deps promote transitively.
  auto root = g.tryMarkGreen(DepNode{DepKind::BytecodeChunk, 1});
  ASSERT_TRUE(root.has_value());
  EXPECT_TRUE(g.prevIsGreen(DepNode{DepKind::SourceText, 1}));
  EXPECT_TRUE(g.prevIsGreen(DepNode{DepKind::Ast, 1}));
  EXPECT_TRUE(g.prevIsGreen(DepNode{DepKind::BytecodeChunk, 1}));

  // Current graph got all three nodes with the same edge structure.
  EXPECT_EQ(g.size(), 3u);
  EXPECT_EQ(g.node(*root).kind, DepKind::BytecodeChunk);
  EXPECT_EQ(g.edges(*root).size(), 1u);
  EXPECT_EQ(g.node(g.edges(*root)[0]).kind, DepKind::Ast);

  // Re-marking returns the same index (idempotent).
  auto again = g.tryMarkGreen(DepNode{DepKind::BytecodeChunk, 1});
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(*again, *root);
}

TEST(DepGraphTest, MissingDependencyMeansRed) {
  // Previous: ast(5) -> source(6), but source(6) is absent from the prev
  // graph as loaded (e.g. the module was deleted). ast must stay red.
  DepGraph g;
  std::vector<DepGraph::PrevNode> prev(1);
  prev[0] = {{DepKind::Ast, 5}, {0}, DepColor::Red, 0, false};
  g.setPreviousGraph(std::move(prev));

  EXPECT_EQ(g.tryMarkGreen(DepNode{DepKind::Ast, 5}), std::nullopt);
  EXPECT_FALSE(g.prevIsGreen(DepNode{DepKind::Ast, 5}));
}

TEST(DepGraphTest, PrevGraphCycleTreatedAsRed) {
  // A corrupt previous graph with a 2-cycle: a -> b -> a. try_mark_green
  // must terminate (in_progress guard) and report red, not hang.
  DepGraph g;
  std::vector<DepGraph::PrevNode> prev(2);
  prev[0] = {{DepKind::Ast, 1}, {1}, DepColor::Red, 0, false};
  prev[1] = {{DepKind::SourceText, 1}, {0}, DepColor::Red, 0, false};
  g.setPreviousGraph(std::move(prev));

  EXPECT_EQ(g.tryMarkGreen(DepNode{DepKind::Ast, 1}), std::nullopt);
  EXPECT_EQ(g.tryMarkGreen(DepNode{DepKind::SourceText, 1}), std::nullopt);
}

TEST(DepGraphTest, InternDeduplicatesNodes) {
  DepGraph g;
  auto a = g.intern(DepNode{DepKind::Ast, 5});
  auto b = g.intern(DepNode{DepKind::Ast, 5});
  auto c = g.intern(DepNode{DepKind::Ast, 6});
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_EQ(g.size(), 2u);
}

// ---------------------------------------------------------------------------
// Prev-graph serialization (Phase 2.1/2.4 groundwork)
// ---------------------------------------------------------------------------

TEST(DepGraphSerializeTest, RoundTripPreservesNodesAndEdges) {
  // Build a prev graph: bytecode(1) -> ast(1) -> source(1) -> (leaf).
  DepGraph original;
  std::vector<DepGraph::PrevNode> prev(3);
  prev[0] = {{DepKind::SourceText, 11}, {}, DepColor::Green, 0, false};
  prev[1] = {{DepKind::Ast, 22}, {0}, DepColor::Green, 0, false};
  prev[2] = {{DepKind::BytecodeChunk, 33}, {1}, DepColor::Green, 0, false};
  original.setPreviousGraph(std::move(prev));

  std::vector<uint8_t> bytes = original.serializePrevGraph();
  ASSERT_FALSE(bytes.empty());

  DepGraph reloaded;
  ASSERT_TRUE(reloaded.loadPrevGraph(bytes));

  // try_mark_green walks the reloaded chain and promotes everything.
  auto root = reloaded.tryMarkGreen(DepNode{DepKind::BytecodeChunk, 33});
  ASSERT_TRUE(root.has_value());
  EXPECT_TRUE(reloaded.prevIsGreen(DepNode{DepKind::SourceText, 11}));
  EXPECT_TRUE(reloaded.prevIsGreen(DepNode{DepKind::Ast, 22}));
  EXPECT_EQ(reloaded.tryMarkGreen(DepNode{DepKind::SourceText, 11}),
            reloaded.tryMarkGreen(DepNode{DepKind::SourceText, 11}));
}

TEST(DepGraphSerializeTest, RejectsBadMagic) {
  DepGraph g;
  std::vector<uint8_t> garbage = {0xDE, 0xAD, 0xBE, 0xEF, 1, 0, 0, 0};
  EXPECT_FALSE(g.loadPrevGraph(garbage));
  // Failed load must not have mutated the graph: still no prev nodes.
  EXPECT_EQ(g.tryMarkGreen(DepNode{DepKind::Ast, 1}), std::nullopt);
}

TEST(DepGraphSerializeTest, RejectsTruncatedData) {
  DepGraph original;
  std::vector<DepGraph::PrevNode> prev(2);
  prev[0] = {{DepKind::SourceText, 1}, {}, DepColor::Green, 0, false};
  prev[1] = {{DepKind::Ast, 2}, {0}, DepColor::Green, 0, false};
  original.setPreviousGraph(std::move(prev));
  std::vector<uint8_t> bytes = original.serializePrevGraph();
  ASSERT_GE(bytes.size(), 4u);

  // Truncate at several points: every prefix must fail cleanly.
  for (size_t cut = 1; cut < bytes.size(); ++cut) {
    DepGraph g;
    std::vector<uint8_t> trunc(bytes.begin(), bytes.begin() + cut);
    EXPECT_FALSE(g.loadPrevGraph(trunc)) << "cut=" << cut;
  }
  // Full bytes still load (sanity).
  DepGraph g2;
  EXPECT_TRUE(g2.loadPrevGraph(bytes));
}

TEST(DepGraphSerializeTest, RejectsOutOfRangeEdges) {
  // Hand-craft a buffer whose single node claims an edge to node 5 (which
  // does not exist in a 1-node graph).
  std::vector<uint8_t> bytes;
  auto put32 = [&](uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes.push_back((v >> (8 * i)) & 0xFF);
  };
  auto put64 = [&](uint64_t v) {
    for (int i = 0; i < 8; ++i) bytes.push_back((v >> (8 * i)) & 0xFF);
  };
  put32(DepGraph::kMagic);
  put32(DepGraph::kVersion);
  put64(1);           // one node
  put32(0);           // kind SourceText
  put32(0);           // reserved
  put64(7);           // key
  put64(1);           // one edge
  put32(5);           // edge to nonexistent node 5
  DepGraph g;
  EXPECT_FALSE(g.loadPrevGraph(bytes));
}

TEST(DepGraphSerializeTest, EmptyGraphRoundTrip) {
  DepGraph g;
  std::vector<uint8_t> bytes = g.serializePrevGraph();
  DepGraph g2;
  EXPECT_TRUE(g2.loadPrevGraph(bytes));
  EXPECT_EQ(g2.tryMarkGreen(DepNode{DepKind::Ast, 1}), std::nullopt);
}

}  // namespace
}  // namespace havel::compiler
