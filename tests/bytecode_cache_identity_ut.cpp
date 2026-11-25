// Regression tests for the bytecode cache's embedded source identity.
//
// Two defects, both found on 2026-11-12 while measuring the shared-cache
// lock convoy:
//
//  1. The writer stamped the source identity it computed from the file on
//     disk, while the reader validates the entry against the file on disk
//     TOO - but the reader's file is a different read. When a source is
//     edited between the compile read and the identity read, the entry
//     records an identity that matches neither the compiled text nor the
//     file, and deserializeChunk then rejects it forever. The entry can
//     never be served, so the cache silently stops working.
//
//  2. Every failure of deserializeChunk/loadChunk was reported as
//     "entry-unreadable", including a source-identity mismatch - which
//     points at an IO problem that is not there (the file parses fine).
//
// The tests below exercise the real serializer: they write an entry, edit
// the source, and assert what the reader reports.

#include "RuntimeSupport.hpp"
#include "Pipeline.hpp"
#include "ModuleLoader.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

// A scratch directory that removes itself.
class ScratchDir {
public:
  ScratchDir() {
    path_ = fs::temp_directory_path() /
            ("havel_cache_identity_" + std::to_string(::getpid()) + "_" +
             std::to_string(counter_++));
    fs::create_directories(path_);
  }
  ~ScratchDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  const fs::path &path() const { return path_; }

private:
  fs::path path_;
  static inline int counter_ = 0;
};

void WriteFile(const fs::path &p, const std::string &text) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << text;
}

// A chunk with one function, enough to serialize and deserialize.
havel::compiler::BytecodeChunk MakeChunk() {
  havel::compiler::BytecodeChunk chunk;
  chunk.addString("hello");
  return chunk;
}

} // namespace

// The reader must distinguish an identity mismatch from an unreadable
// file. Before the fix, loadChunk's failure reason did not exist and the
// serve path reported every failure as "entry-unreadable", so a source
// edit was misreported as an IO problem.
TEST(BytecodeCacheIdentity, LoadReportsSourceMismatchNotUnreadable) {
  ScratchDir scratch;
  const fs::path source = scratch.path() / "unit.hv";
  const fs::path entry = scratch.path() / "unit.hvc";

  WriteFile(source, "print(\"hello\")\n");
  const std::string originalText = "print(\"hello\")\n";

  havel::compiler::ValueSerializer serializer;
  auto data = serializer.serializeChunk(MakeChunk(), source.string(),
                                        std::string(), false, false, originalText);
  {
    std::ofstream out(entry, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
  }

  // The entry matches the source as written: it loads.
  std::string reason;
  auto loaded = serializer.loadChunk(entry.string(), 65536, &reason);
  ASSERT_TRUE(loaded.has_value()) << "a matching entry must load (reason=" << reason << ")";
  EXPECT_TRUE(reason.empty());

  // Now edit the source. The entry describes the OLD text, so the reader
  // must refuse it - and must say WHY.
  WriteFile(source, "print(\"world\")\n");
  reason.clear();
  loaded = serializer.loadChunk(entry.string(), 65536, &reason);
  EXPECT_FALSE(loaded.has_value()) << "an entry describing other text must not load";
  EXPECT_EQ(reason, "source-hash-differs")
      << "a source change must be reported as a source change, not as an IO failure";
  EXPECT_NE(reason, "unreadable");
}

// A missing file and a malformed file are distinct from a source
// mismatch, and each gets its own reason.
TEST(BytecodeCacheIdentity, LoadReportsMissingAndMalformedSeparately) {
  ScratchDir scratch;
  havel::compiler::ValueSerializer serializer;

  std::string reason;
  auto loaded = serializer.loadChunk((scratch.path() / "absent.hvc").string(), 65536, &reason);
  EXPECT_FALSE(loaded.has_value());
  EXPECT_EQ(reason, "missing");

  const fs::path garbage = scratch.path() / "garbage.hvc";
  WriteFile(garbage, "not an hvc file at all");
  reason.clear();
  loaded = serializer.loadChunk(garbage.string(), 65536, &reason);
  EXPECT_FALSE(loaded.has_value());
  EXPECT_EQ(reason, "malformed");
}

// The writer must stamp the identity of the text it was given, not a
// re-read of the file. Before the fix, serializeChunk re-read the file
// when no compiledText was passed, so an entry could describe text that
// was never compiled - and the reader would then reject it forever.
//
// This test pins the writer's behaviour: the embedded identity must match
// the compiledText even when the file on disk says otherwise.
TEST(BytecodeCacheIdentity, WriterStampsCompiledTextNotRereadFile) {
  ScratchDir scratch;
  const fs::path source = scratch.path() / "unit.hv";
  const fs::path entry = scratch.path() / "unit.hvc";

  // The file on disk says one thing...
  WriteFile(source, "print(\"on-disk\")\n");
  // ...but the compiler was handed this text.
  const std::string compiledText = "print(\"compiled\")\n";

  havel::compiler::ValueSerializer serializer;
  auto data = serializer.serializeChunk(MakeChunk(), source.string(),
                                        std::string(), false, false, compiledText);
  {
    std::ofstream out(entry, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
  }

  // The embedded identity must describe the COMPILED text.
  auto info = havel::compiler::ValueSerializer::peekSourceInfoFile(entry.string());
  ASSERT_TRUE(info.hasInfo);
  EXPECT_EQ(info.size, compiledText.size())
      << "the entry must record the size of the text that was compiled";
}

// The end-to-end defect: the launcher joined scripts and appended a
// newline to EACH file's content, so the text it handed the compiler was
// one byte longer than the file on disk. The cache entry is keyed by the
// file's PATH hash and the serve path validates an entry against the text
// it is about to compile - so the entry the compile wrote described text
// no reader ever asked about. It was rejected on the next load
// ("source-size-differs") and rewritten, and the cache never served: every
// run recompiled.
//
// The launcher now compiles a single script file verbatim, so the text it
// compiles IS the file's content and the entry describes it exactly.
//
// This test drives the real pipeline twice with the same text and
// asserts the second compile is served from the cache.
TEST(BytecodeCacheIdentity, SecondCompileOfSameTextIsServedFromCache) {
  ScratchDir scratch;
  const fs::path source = scratch.path() / "unit.hv";

  // Point the cache at the scratch directory: the shared cache is used by
  // every checkout on the box, and an entry for this path could otherwise
  // make the first compile hit and hide the defect.
  const fs::path cacheDir = scratch.path() / "cache";
  fs::create_directories(cacheDir);
  ::setenv("XDG_CACHE_HOME", cacheDir.c_str(), 1);

  // What the launcher compiles for a single file: the file's content,
  // verbatim. No host functions: this test binary does not register them,
  // and the point is the cache, not the runtime.
  const std::string compiledText = "val x = 10 + 5\nreturn x\n";
  WriteFile(source, compiledText);

  havel::compiler::PipelineOptions options;
  options.compile_unit_name = source.string();

  bool fromCache = false;
  {
    auto chunk = havel::compiler::compileToBytecodeChunk(compiledText, "__main__",
                                                         options, &fromCache);
    ASSERT_TRUE(chunk != nullptr);
    EXPECT_FALSE(fromCache)
        << "the first compile must miss the cache - there is no entry yet";
  }

  // The second compile of the SAME text must be served from the entry the
  // first compile wrote.
  fromCache = true;
  {
    auto chunk = havel::compiler::compileToBytecodeChunk(compiledText, "__main__",
                                                         options, &fromCache);
    ASSERT_TRUE(chunk != nullptr);
    EXPECT_TRUE(fromCache)
        << "the second compile must be served from the cache; the entry "
           "written by the first compile did not validate";
  }
}

// The loader validates the same cache artifact repeatedly while resolving
// modules. The SHA-256 result is memoized per observed file version, so an
// unchanged artifact is hashed once in the process. The fingerprint (device,
// inode, size, mtime, ctime) is the invalidation boundary: a changed file
// version is hashed again and its new content is used.
TEST(ModuleLoaderFileHashMemo, UnchangedFileIsHashedOnceAndChangedFileIsHashedAgain) {
  ScratchDir scratch;
  const fs::path file = scratch.path() / "unit.hvc";
  WriteFile(file, "first content\n");

  const std::string firstHash = havel::ModuleLoader::sha256FileHex(file.string());
  ASSERT_FALSE(firstHash.empty());
  EXPECT_EQ(havel::ModuleLoader::sha256FileHashCount(file.string()), 1U);

  const std::string repeatedHash = havel::ModuleLoader::sha256FileHex(file.string());
  EXPECT_EQ(repeatedHash, firstHash);
  EXPECT_EQ(havel::ModuleLoader::sha256FileHashCount(file.string()), 1U)
      << "an unchanged file must not be hashed again in the same process";

  // Replace the file with different content. The new inode and timestamps
  // make this a new file version, so validation must hash it again.
  const fs::path replacement = scratch.path() / "replacement.hvc";
  WriteFile(replacement, "second content\n");
  fs::rename(replacement, file);

  const std::string changedHash = havel::ModuleLoader::sha256FileHex(file.string());
  EXPECT_NE(changedHash, firstHash);
  EXPECT_EQ(havel::ModuleLoader::sha256FileHashCount(file.string()), 2U)
      << "a replaced file must be validated again";
}

// A corrupt artifact is a validation failure, not a reason to skip the
// hash: the memo keeps the failed version's hash so the same corrupt bytes
// are not hashed again during this process.
TEST(ModuleLoaderFileHashMemo, CorruptFileIsNotHashedRepeatedly) {
  ScratchDir scratch;
  const fs::path file = scratch.path() / "corrupt.hvc";
  WriteFile(file, "not a valid hvc payload\n");

  const std::string firstHash = havel::ModuleLoader::sha256FileHex(file.string());
  ASSERT_FALSE(firstHash.empty());
  EXPECT_EQ(havel::ModuleLoader::sha256FileHashCount(file.string()), 1U);

  const std::string repeatedHash = havel::ModuleLoader::sha256FileHex(file.string());
  EXPECT_EQ(repeatedHash, firstHash);
  EXPECT_EQ(havel::ModuleLoader::sha256FileHashCount(file.string()), 1U)
      << "a corrupt but unchanged file must not be hashed again";
}
