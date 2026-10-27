// Unit tests for the config store's save pipeline (src/c/Config.c).
//
// What is under test:
//   - canonical serialization emits exactly one [Section] header per section
//     and writes root keys before any table header (TOML requires this)
//   - a save round-trips: every key survives a reload
//   - a save that changes nothing rewrites neither the file nor a backup slot
//   - the diff report distinguishes added / removed / changed keys
//   - the backup ring rotates, lists, and restores
//   - writes are atomic (no temp file is left behind)
//   - a duplicate-section file is detected, still loads, and is repaired on save
//   - a zero-length file is recovered from the newest backup instead of being
//     read as "no settings"
//
// Each test isolates itself under its own temp HOME so it cannot touch the
// developer's real ~/.config/havel.

#include "Config.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

class ConfigStoreTest : public ::testing::Test {
protected:
  std::string home;
  std::string cfgPath;
  std::string backupPath1;

  void SetUp() override {
    // mkdtemp gives each test a private HOME; the config directory resolver
    // caches, so point it here explicitly rather than relying on setenv
    // ordering inside the already-running process.
    char tmpl[] = "/tmp/havel-config-test-XXXXXX";
    const char *dir = mkdtemp(tmpl);
    ASSERT_NE(dir, nullptr) << "mkdtemp failed";
    home = dir;
    cfgPath = home + "/havel.cfg";
    backupPath1 = home + "/havel.cfg.bak1";
    ::mkdir(home.c_str(), 0755);

    HavelConfig_setConfigPath((home + "/").c_str(), "havel.cfg");
    HavelConfig *cfg = HavelConfig_getInstance();
    HavelConfig_load(cfg, "havel.cfg");
  }

  void TearDown() override {
    HavelConfig_setConfigPath((home + "/").c_str(), "havel.cfg");
    std::string cmd = "rm -rf '" + home + "'";
    if (system(cmd.c_str()) != 0)
      std::fprintf(stderr, "cleanup of %s failed\n", home.c_str());
  }

  static void set(const char *k, const char *v) {
    HavelConfig_setString(HavelConfig_getInstance(), k, v, false);
  }
  static std::string get(const char *k) {
    return HavelConfig_getString(HavelConfig_getInstance(), k, "<missing>");
  }

  static std::string readFile(const std::string &p) {
    FILE *f = fopen(p.c_str(), "rb");
    if (!f)
      return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
      out.append(buf, n);
    fclose(f);
    return out;
  }

  static int countSub(const std::string &hay, const std::string &needle) {
    int n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos;
         at = hay.find(needle, at + needle.size()))
      n++;
    return n;
  }

  static void writeRaw(const std::string &p, const std::string &text) {
    FILE *f = fopen(p.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
  }

  static long fileSize(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 ? static_cast<long>(st.st_size) : -1;
  }
};

// Regression: the writer used to store the whole previous KEY as
// "lastSection" and compare the next section's length against strlen(key), so
// every key got its own header. Re-reading that file redefined tables and lost
// multi-key sections, which surfaced as "OBJECT_SET expects object
// container, got null" far away in a user script.
TEST_F(ConfigStoreTest, OneHeaderPerSectionAndRootKeysFirst) {
  set("IO.Backend", "X11");
  set("IO.Executor", "Scheduler");
  set("Debug.ForceMinimal", "0");
  set("Mouse.DPI", "400");
  set("RootKey", "top");

  HavelConfigSaveReport rep;
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, &rep);
  ASSERT_TRUE(rep.written);
  EXPECT_EQ(rep.added, 5u);

  std::string text = readFile(cfgPath);
  ASSERT_FALSE(text.empty());
  EXPECT_EQ(countSub(text, "[IO]"), 1) << text;
  EXPECT_EQ(countSub(text, "[Debug]"), 1) << text;
  EXPECT_EQ(countSub(text, "[Mouse]"), 1) << text;

  size_t rootAt = text.find("RootKey");
  size_t tableAt = text.find("[Debug]");
  ASSERT_NE(rootAt, std::string::npos);
  ASSERT_NE(tableAt, std::string::npos);
  EXPECT_LT(rootAt, tableAt) << "a bare key after a table header belongs to it";
}

TEST_F(ConfigStoreTest, EveryKeySurvivesAReload) {
  set("IO.Backend", "X11");
  set("IO.Executor", "Scheduler");
  set("Debug.ForceMinimal", "0");
  set("RootKey", "top");
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, NULL);

  HavelConfig_reload(HavelConfig_getInstance());
  EXPECT_EQ(get("IO.Backend"), "X11");
  EXPECT_EQ(get("IO.Executor"), "Scheduler");
  EXPECT_EQ(get("Debug.ForceMinimal"), "0");
  EXPECT_EQ(get("RootKey"), "top");
}

TEST_F(ConfigStoreTest, SaveWithNoChangesDoesNotTouchTheFile) {
  set("IO.Backend", "X11");
  set("IO.Executor", "Scheduler");
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, NULL);

  struct stat before;
  ASSERT_EQ(stat(cfgPath.c_str(), &before), 0);

  HavelConfigSaveReport rep;
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, &rep);
  EXPECT_FALSE(rep.written);
  EXPECT_EQ(rep.added, 0u);
  EXPECT_EQ(rep.removed, 0u);
  EXPECT_EQ(rep.changed, 0u);

  struct stat after;
  ASSERT_EQ(stat(cfgPath.c_str(), &after), 0);
  EXPECT_EQ(before.st_mtime, after.st_mtime);
  EXPECT_LE(fileSize(backupPath1), 0) << "a no-op save must not consume a backup slot";
}

TEST_F(ConfigStoreTest, DiffSeparatesAddedRemovedAndChanged) {
  set("IO.Backend", "X11");
  set("IO.Executor", "Scheduler");
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, NULL);

  set("IO.Backend", "Wayland");
  set("Mouse.DPI", "400");
  ASSERT_TRUE(HavelConfig_remove(HavelConfig_getInstance(), "IO.Executor"));

  HavelConfigSaveReport diff;
  HavelConfig_diffToDisk(HavelConfig_getInstance(), &diff);
  EXPECT_EQ(diff.changed, 1u);
  EXPECT_EQ(diff.added, 1u);
  EXPECT_EQ(diff.removed, 1u);

  HavelConfigSaveReport rep;
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, &rep);
  EXPECT_TRUE(rep.written);
  EXPECT_EQ(rep.added, 1u);
  EXPECT_EQ(rep.changed, 1u);
  EXPECT_EQ(rep.removed, 1u);
  EXPECT_NE(std::string(rep.backup).find(".bak1"), std::string::npos)
      << "a changing save must record which backup it wrote";
}

TEST_F(ConfigStoreTest, BackupRingRotatesListsAndRestores) {
  const char *values[4] = {"one", "two", "three", "four"};
  for (int i = 0; i < 4; i++) {
    set("Gen.Value", values[i]);
    HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, NULL);
  }

  char **baks = nullptr;
  size_t n = HavelConfig_listBackups("havel.cfg", &baks);
  EXPECT_EQ(n, 3u);
  if (n == 3)
    EXPECT_NE(std::string(baks[0]).find(".bak1"), std::string::npos);
  HavelConfig_freeBackups(baks, n);

  ASSERT_TRUE(HavelConfig_restoreBackup(HavelConfig_getInstance(), NULL, 0));
  EXPECT_EQ(get("Gen.Value"), "three")
      << "restore must load the backup it wrote, not the file already there";
}

TEST_F(ConfigStoreTest, AtomicWriteLeavesNoTempFile) {
  set("A.B", "1");
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, NULL);

  std::string cmd = "ls " + home + "/havel.cfg.tmp.* 2>/dev/null";
  FILE *p = popen(cmd.c_str(), "r");
  ASSERT_NE(p, nullptr);
  char out[512] = {0};
  bool leftover = fgets(out, sizeof(out), p) != nullptr;
  pclose(p);
  EXPECT_FALSE(leftover) << "temp file left behind: " << out;
}

TEST_F(ConfigStoreTest, DuplicateSectionsAreDetectedLoadedAndRepaired) {
  set("IO.Backend", "X11");
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, NULL);

  // Exactly what the old writer produced.
  writeRaw(cfgPath, "[IO]\nBackend = X11\n\n[IO]\nExecutor = Scheduler\n");

  size_t dups = 0, bad = 0;
  EXPECT_GE(HavelConfig_validateFile("havel.cfg", &dups, &bad), 1);
  EXPECT_EQ(dups, 1u);

  // Loading must merge rather than lose data.
  HavelConfig_reload(HavelConfig_getInstance());
  EXPECT_EQ(get("IO.Backend"), "X11");
  EXPECT_EQ(get("IO.Executor"), "Scheduler");

  HavelConfigSaveReport rep;
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, &rep);
  EXPECT_TRUE(rep.written);
  EXPECT_EQ(countSub(readFile(cfgPath), "[IO]"), 1);
}

TEST_F(ConfigStoreTest, ZeroLengthFileRecoversFromBackup) {
  set("IO.Backend", "X11");
  set("IO.Executor", "Scheduler");
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, NULL);
  set("IO.Backend", "Wayland");
  HavelConfig_saveWithReport(HavelConfig_getInstance(), NULL, NULL); // .bak1 = X11

  writeRaw(cfgPath, "");
  ASSERT_EQ(fileSize(cfgPath), 0);

  HavelConfig_reload(HavelConfig_getInstance());
  EXPECT_EQ(get("IO.Backend"), "X11") << "a truncated file must not read as 'no settings'";
  EXPECT_GT(fileSize(cfgPath), 0) << "recovery should leave a usable file";
}

} // namespace
