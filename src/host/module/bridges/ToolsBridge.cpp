// ToolsBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
namespace {
::havel::host::TextChunkerService g_textChunker;
} // namespace

void ToolsBridge::install(PipelineOptions &options) {
  options.host_functions["textchunker.setText"] = [ctx =
                                                       ctx_](const auto &args) {
    return handleTextChunkerSetText(args, ctx);
  };
  options.host_functions["textchunker.getText"] = [ctx =
                                                       ctx_](const auto &args) {
    return handleTextChunkerGetText(args, ctx);
  };
  options.host_functions["textchunker.setChunkSize"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerSetChunkSize(args, ctx);
      };
  options.host_functions["textchunker.getTotalChunks"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGetTotalChunks(args, ctx);
      };
  options.host_functions["textchunker.getCurrentChunk"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGetCurrentChunk(args, ctx);
      };
  options.host_functions["textchunker.setCurrentChunk"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerSetCurrentChunk(args, ctx);
      };
  options.host_functions["textchunker.getChunk"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGetChunk(args, ctx);
      };
  options.host_functions["textchunker.getNextChunk"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGetNextChunk(args, ctx);
      };
  options.host_functions["textchunker.getPreviousChunk"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGetPreviousChunk(args, ctx);
      };
  options.host_functions["textchunker.goToFirst"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGoToFirst(args, ctx);
      };
  options.host_functions["textchunker.goToLast"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGoToLast(args, ctx);
      };
  options.host_functions["textchunker.clear"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerClear(args, ctx);
  };
}


Value
ToolsBridge::handleTextChunkerSetText(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setText() requires text");
  }
  const std::string *text = nullptr;
  if (!text) {
    throw std::runtime_error("textchunker.setText() requires a string");
  }
  g_textChunker.setText(*text);
  return Value::makeBool(true);
}


Value
ToolsBridge::handleTextChunkerGetText(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // TODO: string pool integration - for now return null
  (void)g_textChunker.getText();
  return Value::makeNull();
}


Value ToolsBridge::handleTextChunkerSetChunkSize(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setChunkSize() requires a size");
  }
  int64_t size = 20000;
  if (args[0].isInt()) {
    size = args[0].asInt();
  }
  g_textChunker.setChunkSize(static_cast<size_t>(size));
  return Value::makeBool(true);
}


Value ToolsBridge::handleTextChunkerGetTotalChunks(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeInt(static_cast<int64_t>(g_textChunker.getTotalChunks()));
}


Value ToolsBridge::handleTextChunkerGetCurrentChunk(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeInt(static_cast<int64_t>(g_textChunker.getCurrentChunk()));
}


Value ToolsBridge::handleTextChunkerSetCurrentChunk(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error(
        "textchunker.setCurrentChunk() requires a chunk index");
  }
  int64_t index = 0;
  if (args[0].isInt()) {
    index = args[0].asInt();
  }
  g_textChunker.setCurrentChunk(static_cast<int>(index));
  return Value::makeBool(true);
}


Value
ToolsBridge::handleTextChunkerGetChunk(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.getChunk() requires a chunk index");
  }
  int64_t index = 0;
  if (args[0].isInt()) {
    index = args[0].asInt();
  }
  // TODO: string pool integration - for now return null
  (void)g_textChunker.getChunk(static_cast<int>(index));
  return Value::makeNull();
}


Value ToolsBridge::handleTextChunkerGetNextChunk(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // TODO: string pool integration - for now return null
  (void)g_textChunker.getNextChunk();
  return Value::makeNull();
}


Value ToolsBridge::handleTextChunkerGetPreviousChunk(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // TODO: string pool integration - for now return null
  (void)g_textChunker.getPreviousChunk();
  return Value::makeNull();
}


Value
ToolsBridge::handleTextChunkerGoToFirst(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.goToFirst();
  return Value::makeBool(true);
}


Value
ToolsBridge::handleTextChunkerGoToLast(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.goToLast();
  return Value::makeBool(true);
}


Value
ToolsBridge::handleTextChunkerClear(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.clear();
  return Value::makeBool(true);
}

// ============================================================================
// MediaBridge Implementation
// ============================================================================

} // namespace havel::compiler
