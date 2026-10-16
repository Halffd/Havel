#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <cstring>

namespace fs = std::filesystem;

struct PackageManifest {
  std::string name;
  std::string version{"0.1.0"};
  std::string description;
  std::string author;
  std::string license{"MIT"};
  std::vector<std::string> dependencies;
  std::vector<std::string> files;
};

static std::string get_packages_dir() {
  const char *home = std::getenv("HOME");
  if (!home) {
    std::cerr << "hpkg: HOME not set" << std::endl;
    return "";
  }
  return (fs::path(home) / ".havel" / "packages").string();
}

// Parses only the flat key=value [package] and [dependencies] sections we generate.
static bool parse_havel_toml(const fs::path &path, PackageManifest &manifest) {
  std::ifstream f(path);
  if (!f.is_open())
    return false;

  std::string line;
  bool in_deps = false;
  while (std::getline(f, line)) {
    auto start = line.find_first_not_of(" \t");
    if (start == std::string::npos)
      continue;
    line = line.substr(start);
    auto end = line.find_last_not_of(" \t\r\n");
    if (end != std::string::npos)
      line = line.substr(0, end + 1);

    if (line.empty() || line[0] == '#')
      continue;

    if (line == "[package]") {
      in_deps = false;
      continue;
    }
    if (line == "[dependencies]") {
      in_deps = true;
      continue;
    }

    if (in_deps) {
      auto eq = line.find('=');
      if (eq != std::string::npos) {
        std::string dep = line.substr(0, eq);
        auto dep_end = dep.find_last_not_of(" \t");
        if (dep_end != std::string::npos)
          dep = dep.substr(0, dep_end + 1);
        manifest.dependencies.push_back(dep);
      } else {
        manifest.dependencies.push_back(line);
      }
      continue;
    }

    auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;

    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);

    auto ke = key.find_last_not_of(" \t");
    if (ke != std::string::npos)
      key = key.substr(0, ke + 1);
    auto vs = val.find_first_not_of(" \t");
    if (vs != std::string::npos)
      val = val.substr(vs);
    auto ve = val.find_last_not_of(" \t");
    if (ve != std::string::npos)
      val = val.substr(0, ve + 1);

    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
      val = val.substr(1, val.size() - 2);
    }

    if (key == "name")
      manifest.name = val;
    else if (key == "version")
      manifest.version = val;
    else if (key == "description")
      manifest.description = val;
    else if (key == "author")
      manifest.author = val;
    else if (key == "license")
      manifest.license = val;
  }
  return true;
}

static bool write_havel_toml(const fs::path &path, const PackageManifest &m) {
  std::ofstream f(path);
  if (!f.is_open())
    return false;

  f << "[package]\n";
  f << "name = \"" << m.name << "\"\n";
  f << "version = \"" << m.version << "\"\n";
  if (!m.description.empty())
    f << "description = \"" << m.description << "\"\n";
  if (!m.author.empty())
    f << "author = \"" << m.author << "\"\n";
  f << "license = \"" << m.license << "\"\n";

  if (!m.dependencies.empty()) {
    f << "\n[dependencies]\n";
    for (const auto &dep : m.dependencies) {
      f << dep << " = \"*\"\n";
    }
  }
  return true;
}

// ============================================================================
// Commands
// ============================================================================

static int cmd_init(const std::vector<std::string> &args) {
  std::string pkg_name;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--name" && i + 1 < args.size())
      pkg_name = args[++i];
    else if (pkg_name.empty() && args[i][0] != '-')
      pkg_name = args[i];
  }

  if (pkg_name.empty()) {
    pkg_name = fs::current_path().filename().string();
    std::transform(pkg_name.begin(), pkg_name.end(), pkg_name.begin(),
                   [](unsigned char c) { return c == ' ' ? '-' : std::tolower(c); });
  }

  for (char c : pkg_name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
      std::cerr << "hpkg: invalid package name '" << pkg_name
                << "' (only alphanumeric, hyphen, underscore allowed)" << std::endl;
      return 1;
    }
  }

  fs::path cwd = fs::current_path();
  fs::path toml_path = cwd / "havel.toml";

  if (fs::exists(toml_path)) {
    std::cerr << "hpkg: havel.toml already exists in this directory" << std::endl;
    return 1;
  }

  fs::path entry_point = cwd / (pkg_name + ".hv");
  if (!fs::exists(entry_point)) {
    std::ofstream f(entry_point);
    f << "// " << pkg_name << " - a Havel package\n\n";
    f << "fn hello() {\n";
    f << "  print(\"hello from " << pkg_name << "\")\n";
    f << "}\n\n";
    f << "export hello\n";
    std::cout << "created " << entry_point.string() << std::endl;
  }

  PackageManifest manifest;
  manifest.name = pkg_name;
  if (!write_havel_toml(toml_path, manifest)) {
    std::cerr << "hpkg: failed to write havel.toml" << std::endl;
    return 1;
  }
  std::cout << "created havel.toml" << std::endl;

  fs::path gitignore = cwd / ".gitignore";
  if (!fs::exists(gitignore)) {
    std::ofstream f(gitignore);
    f << "*.hvc\n";
    std::cout << "created .gitignore" << std::endl;
  }

  std::cout << "\npackage '" << pkg_name << "' initialized" << std::endl;
  return 0;
}

static int cmd_install(const std::vector<std::string> &args) {
  if (args.empty()) {
    std::cerr << "hpkg install: specify a package path or name" << std::endl;
    return 1;
  }

  std::string pkgs_dir = get_packages_dir();
  if (pkgs_dir.empty())
    return 1;

  fs::create_directories(pkgs_dir);

  for (const auto &arg : args) {
    if (arg[0] == '-') {
      std::cerr << "hpkg install: unknown option " << arg << std::endl;
      return 1;
    }

    fs::path source(arg);

    if (fs::is_directory(source)) {
      fs::path toml = source / "havel.toml";
      if (!fs::exists(toml)) {
        std::cerr << "hpkg: no havel.toml in " << source.string() << std::endl;
        return 1;
      }

      PackageManifest manifest;
      if (!parse_havel_toml(toml, manifest) || manifest.name.empty()) {
        std::cerr << "hpkg: invalid havel.toml in " << source.string() << std::endl;
        return 1;
      }

      fs::path dest = fs::path(pkgs_dir) / manifest.name;
      if (fs::exists(dest)) {
        fs::remove_all(dest);
      }
      fs::create_directories(dest);

      std::error_code ec;

      fs::copy_file(toml, dest / "havel.toml", fs::copy_options::overwrite_existing, ec);
      if (ec) {
        std::cerr << "hpkg: copy havel.toml failed: " << ec.message() << std::endl;
        return 1;
      }

      fs::path src_dir = source;
      if (fs::is_directory(source / manifest.name)) {
        src_dir = source / manifest.name;
      }

      for (const auto &entry : fs::directory_iterator(src_dir)) {
        if (!entry.is_regular_file())
          continue;
        if (entry.path().extension() == ".hv") {
          fs::copy_file(entry.path(), dest / entry.path().filename(),
                        fs::copy_options::overwrite_existing, ec);
          if (ec) {
            std::cerr << "hpkg: copy failed: " << ec.message() << std::endl;
            return 1;
          }
        }
      }

      std::cout << "installed " << manifest.name << " v" << manifest.version << std::endl;
      continue;
    }

    if (fs::exists(source) && source.extension() == ".hv") {
      std::string name = source.stem().string();
      fs::path dest_dir = fs::path(pkgs_dir) / name;
      fs::create_directories(dest_dir);
      fs::path dest_file = dest_dir / (name + ".hv");

      std::error_code ec;
      fs::copy_file(source, dest_file, fs::copy_options::overwrite_existing, ec);

      if (ec) {
        std::cerr << "hpkg: copy failed: " << ec.message() << std::endl;
        return 1;
      }

      std::cout << "installed " << name << " (single file)" << std::endl;
      continue;
    }

    fs::path installed = fs::path(pkgs_dir) / arg;
    if (fs::exists(installed)) {
      PackageManifest manifest;
      fs::path toml = installed / "havel.toml";
      if (fs::exists(toml) && parse_havel_toml(toml, manifest)) {
        std::cout << arg << " v" << manifest.version << " (already installed)" << std::endl;
      } else {
        std::cout << arg << " (installed, no manifest)" << std::endl;
      }
      continue;
    }

    std::cerr << "hpkg: package not found: " << arg << std::endl;
    return 1;
  }

  return 0;
}

static int cmd_list(const std::vector<std::string> &args) {
  std::string pkgs_dir = get_packages_dir();
  if (pkgs_dir.empty())
    return 1;

  if (!fs::exists(pkgs_dir)) {
    std::cout << "no packages installed" << std::endl;
    return 0;
  }

  bool found = false;
  for (const auto &entry : fs::directory_iterator(pkgs_dir)) {
    if (!entry.is_directory())
      continue;

    std::string name = entry.path().filename().string();
    fs::path toml = entry.path() / "havel.toml";

    if (fs::exists(toml)) {
      PackageManifest manifest;
      if (parse_havel_toml(toml, manifest)) {
        std::cout << manifest.name << " v" << manifest.version;
        if (!manifest.description.empty())
          std::cout << " - " << manifest.description;
        std::cout << std::endl;
      } else {
        std::cout << name << " (invalid manifest)" << std::endl;
      }
    } else {
      fs::path hv_file = entry.path() / (name + ".hv");
      if (fs::exists(hv_file)) {
        std::cout << name << " (no manifest)" << std::endl;
      } else {
        std::cout << name << " (broken)" << std::endl;
      }
    }
    found = true;
  }

  if (!found) {
    std::cout << "no packages installed" << std::endl;
  }
  return 0;
}

static int cmd_publish(const std::vector<std::string> &args) {
  fs::path cwd = fs::current_path();
  fs::path toml = cwd / "havel.toml";

  if (!fs::exists(toml)) {
    std::cerr << "hpkg: no havel.toml in current directory" << std::endl;
    std::cerr << "  run 'hpkg init' first" << std::endl;
    return 1;
  }

  PackageManifest manifest;
  if (!parse_havel_toml(toml, manifest) || manifest.name.empty()) {
    std::cerr << "hpkg: invalid havel.toml" << std::endl;
    return 1;
  }

  fs::path entry = cwd / (manifest.name + ".hv");
  if (!fs::exists(entry)) {
    std::cerr << "hpkg: missing entry point " << (manifest.name + ".hv") << std::endl;
    return 1;
  }

  std::vector<std::string> hv_files;
  for (const auto &e : fs::directory_iterator(cwd)) {
    if (e.is_regular_file() && e.path().extension() == ".hv") {
      hv_files.push_back(e.path().filename().string());
    }
  }

  if (manifest.version.empty()) {
    std::cerr << "hpkg: version is required in havel.toml" << std::endl;
    return 1;
  }

  if (!manifest.dependencies.empty()) {
    std::string pkgs_dir = get_packages_dir();
    for (const auto &dep : manifest.dependencies) {
      fs::path dep_path = fs::path(pkgs_dir) / dep;
      if (!fs::exists(dep_path)) {
        std::cerr << "hpkg: missing dependency '" << dep << "'" << std::endl;
        std::cerr << "  run 'hpkg install " << dep << "' first" << std::endl;
        return 1;
      }
    }
  }

  std::cout << "package: " << manifest.name << " v" << manifest.version << std::endl;
  std::cout << "files:" << std::endl;
  for (const auto &f : hv_files) {
    std::cout << "  " << f << std::endl;
  }
  if (!manifest.dependencies.empty()) {
    std::cout << "dependencies:" << std::endl;
    for (const auto &d : manifest.dependencies) {
      std::cout << "  " << d << std::endl;
    }
  }
  std::cout << "\npackage is valid and ready for distribution" << std::endl;

  bool local_install = false;
  for (const auto &a : args) {
    if (a == "--local")
      local_install = true;
  }

  if (local_install) {
    std::vector<std::string> install_args = {cwd.string()};
    return cmd_install(install_args);
  }

  return 0;
}

static int cmd_remove(const std::vector<std::string> &args) {
  if (args.empty()) {
    std::cerr << "hpkg remove: specify a package name" << std::endl;
    return 1;
  }

  std::string pkgs_dir = get_packages_dir();
  if (pkgs_dir.empty())
    return 1;

  for (const auto &name : args) {
    if (name[0] == '-') {
      std::cerr << "hpkg remove: unknown option " << name << std::endl;
      return 1;
    }

    fs::path pkg_path = fs::path(pkgs_dir) / name;
    if (!fs::exists(pkg_path)) {
      std::cerr << "hpkg: package '" << name << "' not installed" << std::endl;
      return 1;
    }

    std::uintmax_t count = fs::remove_all(pkg_path);
    std::cout << "removed " << name << " (" << count << " files)" << std::endl;
  }

  return 0;
}

static int cmd_info(const std::vector<std::string> &args) {
  if (args.empty()) {
    fs::path toml = fs::current_path() / "havel.toml";
    if (!fs::exists(toml)) {
      std::cerr << "hpkg: no havel.toml in current directory" << std::endl;
      return 1;
    }
    PackageManifest manifest;
    parse_havel_toml(toml, manifest);
    std::cout << "name:        " << manifest.name << std::endl;
    std::cout << "version:     " << manifest.version << std::endl;
    if (!manifest.description.empty())
      std::cout << "description: " << manifest.description << std::endl;
    if (!manifest.author.empty())
      std::cout << "author:      " << manifest.author << std::endl;
    std::cout << "license:     " << manifest.license << std::endl;
    if (!manifest.dependencies.empty()) {
      std::cout << "dependencies:" << std::endl;
      for (const auto &d : manifest.dependencies)
        std::cout << "  - " << d << std::endl;
    }
    return 0;
  }

  std::string pkgs_dir = get_packages_dir();
  if (pkgs_dir.empty())
    return 1;

  for (const auto &name : args) {
    fs::path pkg_path = fs::path(pkgs_dir) / name;
    fs::path toml = pkg_path / "havel.toml";
    fs::path hv_file = pkg_path / (name + ".hv");

    std::cout << name << ":" << std::endl;
    std::cout << "  location: " << pkg_path.string() << std::endl;

    if (fs::exists(toml)) {
      PackageManifest manifest;
      parse_havel_toml(toml, manifest);
      std::cout << "  version:  " << manifest.version << std::endl;
      if (!manifest.description.empty())
        std::cout << "  desc:     " << manifest.description << std::endl;
    } else {
      std::cout << "  (no manifest)" << std::endl;
    }

    if (fs::exists(hv_file)) {
      auto fsize = fs::file_size(hv_file);
      std::cout << "  entry:    " << hv_file.string() << " (" << fsize << " bytes)" << std::endl;
    }
  }
  return 0;
}

// ============================================================================
// Toolchains: per-commit / per-tag havel binaries for regression testing
// ============================================================================
//
// Storage layout:
//   ~/.havel/toolchains/<commit>/havel      the binary for that commit
//   ~/.havel/toolchains/<commit>/hvtest     the test runner (when built)
//   ~/.havel/toolchains/<commit>/.info      ref, date, build mode, origin
//   ~/.havel/toolchains/default             text file: the selected commit
//
// Selection precedence (nvm-style): the HAVEL_TOOLCHAIN environment variable
// wins over the default file, and both are consulted by `run`/`regress` only.
// Nothing outside hpkg reads them, so selecting a toolchain can never change
// what a bare `havel` invocation does.

struct Toolchain {
  std::string commit;
  std::string ref;
  std::string date;
  std::string mode;
  std::string origin;
  bool has_havel = false;
  bool has_hvtest = false;
};

static fs::path get_toolchains_dir() {
  // HAVEL_TOOLCHAIN_STORE relocates the whole store. It exists so tests (and
  // parallel users on a shared box) never touch the real ~/.havel/toolchains.
  if (const char *store = std::getenv("HAVEL_TOOLCHAIN_STORE")) {
    if (*store)
      return fs::path(store);
  }
  const char *home = std::getenv("HOME");
  if (!home)
    return {};
  return fs::path(home) / ".havel" / "toolchains";
}

// Runs a command with output captured. Returns the exit status, or -1 when the
// command could not be started at all.
static int run_capture(const std::string &cmd, std::string *out) {
  // With no sink the command's output belongs to the user: run it unbuffered
  // through the shell so `toolchain run` shows the script's output and a build
  // streams its log, instead of both being swallowed.
  if (!out) {
    int rc = std::system(cmd.c_str());
    if (rc == -1) return -1;
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
  }
  std::string full = cmd + " 2>&1";
  FILE *pipe = popen(full.c_str(), "r");
  if (!pipe)
    return -1;
  char buf[4096];
  while (fgets(buf, sizeof(buf), pipe)) {
    if (out)
      out->append(buf);
  }
  int rc = pclose(pipe);
  if (rc == -1)
    return -1;
  return WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
}

static std::string trim(std::string s) {
  auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos)
    return "";
  auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

static fs::path repo_root_from_cwd() {
  std::string out;
  if (run_capture("git rev-parse --show-toplevel", &out) == 0) {
    std::string top = trim(out);
    if (!top.empty())
      return fs::path(top);
  }
  return {};
}

// Reads the .info file next to an installed binary.
static bool read_toolchain_info(const fs::path &dir, Toolchain &tc) {
  std::ifstream f(dir / ".info");
  if (!f.is_open())
    return false;
  std::string line;
  while (std::getline(f, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    if (key == "commit")
      tc.commit = val;
    else if (key == "ref")
      tc.ref = val;
    else if (key == "date")
      tc.date = val;
    else if (key == "mode")
      tc.mode = val;
    else if (key == "origin")
      tc.origin = val;
  }
  tc.has_havel = fs::exists(dir / "havel");
  tc.has_hvtest = fs::exists(dir / "hvtest");
  return true;
}

static void write_toolchain_info(const fs::path &dir, const Toolchain &tc) {
  std::ofstream f(dir / ".info");
  if (!f.is_open())
    return;
  f << "commit=" << tc.commit << "\n";
  f << "ref=" << tc.ref << "\n";
  f << "date=" << tc.date << "\n";
  f << "mode=" << tc.mode << "\n";
  f << "origin=" << tc.origin << "\n";
}

// Resolves a tag/branch/sha to a full commit via git. Returns "" on failure.
static std::string resolve_ref(const fs::path &repo, const std::string &ref) {
  if (repo.empty())
    return "";
  std::string out;
  std::string q = "git -C '" + repo.string() + "' rev-parse --verify '" + ref + "^{commit}'";
  if (run_capture(q, &out) != 0)
    return "";
  std::string sha = trim(out);
  if (sha.size() != 40)
    return "";
  return sha;
}

static std::string commit_date(const fs::path &repo, const std::string &commit) {
  std::string out;
  std::string q = "git -C '" + repo.string() + "' show -s --format=%cs " + commit;
  if (run_capture(q, &out) != 0)
    return "";
  return trim(out);
}

static fs::path toolchain_dir(const std::string &commit) {
  return get_toolchains_dir() / commit;
}

// A copied binary keeps RUNPATH=$ORIGIN, so any shared library it needs has to
// travel with it or the toolchain is dead on arrival (127 from the loader).
// The set is derived from the binary itself via ldd rather than a hardcoded
// list, so a new runtime .so is picked up without editing this file.
static void copy_runtime_libs(const fs::path &bin, const fs::path &source_dir) {
  std::string out;
  if (run_capture("ldd '" + bin.string() + "'", &out) != 0) return;
  std::istringstream lines(out);
  std::string line;
  std::error_code ec;
  while (std::getline(lines, line)) {
    auto arrow = line.find("=> not found");
    if (arrow == std::string::npos) continue;
    std::string name = trim(line.substr(0, arrow));
    if (name.empty()) continue;
    // Search the source build dir and its toolkits/ subdir for the library.
    for (const fs::path &dir : {source_dir, source_dir / "toolkits"}) {
      fs::path candidate = dir / name;
      if (fs::exists(candidate)) {
        fs::copy_file(candidate, bin.parent_path() / name,
                      fs::copy_options::overwrite_existing, ec);
        break;
      }
    }
  }
}

// Finds an installed toolchain by commit, or by any ref recorded in an .info.
static std::string resolve_installed(const fs::path &root, const std::string &want) {
  if (want.empty())
    return "";
  if (want.size() == 40 && fs::is_directory(root / want))
    return want;
  // A short sha prefix or a recorded ref both resolve by scanning.
  if (!fs::is_directory(root))
    return "";
  for (const auto &entry : fs::directory_iterator(root)) {
    if (!entry.is_directory())
      continue;
    std::string name = entry.path().filename().string();
    if (name == want)
      return name;
    if (want.size() >= 4 && name.rfind(want, 0) == 0)
      return name;
    Toolchain tc;
    if (read_toolchain_info(entry.path(), tc) && tc.ref == want)
      return name;
  }
  return "";
}

static std::string selected_toolchain() {
  if (const char *env = std::getenv("HAVEL_TOOLCHAIN")) {
    std::string v = trim(env);
    if (!v.empty())
      return v;
  }
  fs::path root = get_toolchains_dir();
  std::ifstream f(root / "default");
  if (!f.is_open())
    return "";
  std::string line;
  std::getline(f, line);
  return trim(line);
}

static int cmd_toolchain(const std::vector<std::string> &args) {
  if (args.empty()) {
    std::cerr << "hpkg toolchain: specify a subcommand "
                              "(install, list, use, current, remove, run, which, tags, regress)"
              << std::endl;
    return 1;
  }

  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  // ---------------------------------------------------------------- install
  if (sub == "install") {
    std::string ref;
    std::string from;
    fs::path repo;
    int jobs = 0;
    bool no_build = false;
    for (size_t i = 0; i < rest.size(); ++i) {
      const std::string &a = rest[i];
      if (a == "--from" && i + 1 < rest.size())
        from = rest[++i];
      else if (a == "--repo" && i + 1 < rest.size())
        repo = rest[++i];
      else if (a == "--jobs" && i + 1 < rest.size())
        jobs = std::stoi(rest[++i]);
      else if (a == "--no-build")
        no_build = true;
      else if (a[0] == '-') {
        std::cerr << "hpkg toolchain install: unknown option " << a << std::endl;
        return 1;
      } else if (ref.empty())
        ref = a;
    }
    if (ref.empty()) {
      std::cerr << "hpkg toolchain install: specify a ref" << std::endl;
      return 1;
    }
    if (no_build && from.empty()) {
      std::cerr << "hpkg toolchain install: --no-build requires --from" << std::endl;
      return 1;
    }
    if (repo.empty())
      repo = repo_root_from_cwd();
    if (repo.empty()) {
      std::cerr << "hpkg toolchain install: not inside a git repository; "
                   "pass --repo <path>"
                << std::endl;
      return 1;
    }

    std::string commit = resolve_ref(repo, ref);
    if (commit.empty()) {
      std::cerr << "hpkg toolchain install: ref not found in " << repo.string() << ": " << ref
                << std::endl;
      return 1;
    }

    fs::path root = get_toolchains_dir();
    fs::path dir = toolchain_dir(commit);
    if (fs::exists(dir / "havel")) {
      std::cout << "toolchain " << commit.substr(0, 12) << " already installed" << std::endl;
      return 0;
    }
    fs::create_directories(dir);

    Toolchain tc;
    tc.commit = commit;
    tc.ref = ref;
    tc.date = commit_date(repo, commit);

    if (!from.empty()) {
      // Register a prebuilt binary without building. The source may be a
      // build directory or a directory holding both binaries.
      std::error_code ec;
      fs::path src(from);
      if (fs::is_directory(src)) {
        fs::copy_file(src / "havel", dir / "havel", fs::copy_options::overwrite_existing, ec);
        if (ec) {
          std::cerr << "hpkg: copy havel failed: " << ec.message() << std::endl;
          fs::remove_all(dir);
          return 1;
        }
        if (fs::exists(src / "hvtest")) {
          fs::copy_file(src / "hvtest", dir / "hvtest", fs::copy_options::overwrite_existing, ec);
        }
      } else {
        fs::copy_file(src, dir / "havel", fs::copy_options::overwrite_existing, ec);
        if (ec) {
          std::cerr << "hpkg: copy failed: " << ec.message() << std::endl;
          fs::remove_all(dir);
          return 1;
        }
      }
      // Runtime .so files must land next to the binary ($ORIGIN) or the
      // toolchain cannot start.
      copy_runtime_libs(dir / "havel",
                        fs::is_directory(src) ? src : src.parent_path());
      tc.mode = "copied";
      tc.origin = from;
    } else {
      // Build at that commit in a throwaway worktree. The worktree is
      // removed afterwards so a failed install leaves no repo state.
      fs::path wt = root / ("build-" + commit.substr(0, 12));
      std::error_code ec;
      fs::remove_all(wt, ec);
      fs::create_directories(wt, ec);

      std::string q =
          "git -C '" + repo.string() + "' worktree add --detach '" + wt.string() + "' " + commit;
      if (run_capture(q, nullptr) != 0) {
        std::cerr << "hpkg: git worktree add failed for " << commit << std::endl;
        fs::remove_all(wt, ec);
        fs::remove_all(dir, ec);
        return 1;
      }

      std::string jobs_arg = jobs > 0 ? (" -j" + std::to_string(jobs)) : "";
      std::string build = "cd '" + wt.string() + "' && ./build.sh 6 build" + jobs_arg;
      std::cout << "building " << commit.substr(0, 12) << " (this takes a while)" << std::endl;
      if (run_capture(build, nullptr) != 0) {
        std::cerr << "hpkg: build failed at " << commit.substr(0, 12) << std::endl;
        run_capture("git -C '" + repo.string() + "' worktree remove --force '" + wt.string() + "'",
                    nullptr);
        fs::remove_all(wt, ec);
        fs::remove_all(dir, ec);
        return 1;
      }

      std::string produced;
      run_capture("cd '" + wt.string() + "/build-debug' && ls -d havel hvtest 2>/dev/null",
                  &produced);
      if (produced.find("havel") == std::string::npos) {
        std::cerr << "hpkg: build produced no havel binary at " << commit.substr(0, 12)
                  << std::endl;
        run_capture("git -C '" + repo.string() + "' worktree remove --force '" + wt.string() + "'",
                    nullptr);
        fs::remove_all(wt, ec);
        fs::remove_all(dir, ec);
        return 1;
      }
      copy_runtime_libs(dir / "havel", wt / "build-debug");
      for (const char *name : {"havel", "hvtest"}) {
        fs::path src = wt / "build-debug" / name;
        if (fs::exists(src)) {
          fs::copy_file(src, dir / name, fs::copy_options::overwrite_existing, ec);
        }
      }
      tc.mode = "built";
      tc.origin = repo.string();
      run_capture("git -C '" + repo.string() + "' worktree remove --force '" + wt.string() + "'",
                  nullptr);
      fs::remove_all(wt, ec);
    }

    write_toolchain_info(dir, tc);
    std::cout << "installed toolchain " << commit.substr(0, 12) << " (" << ref << ")" << std::endl;
    if (tc.date.empty()) {
      std::cout << "date: unknown" << std::endl;
    } else {
      std::cout << "date: " << tc.date << std::endl;
    }
    return 0;
  }

  // ------------------------------------------------------------------- list
  if (sub == "list") {
    fs::path root = get_toolchains_dir();
    if (!fs::is_directory(root)) {
      std::cout << "no toolchains installed" << std::endl;
      return 0;
    }
    std::string def = selected_toolchain();
    bool found = false;
    std::vector<Toolchain> tcs;
    for (const auto &entry : fs::directory_iterator(root)) {
      if (!entry.is_directory())
        continue;
      Toolchain tc;
      if (!read_toolchain_info(entry.path(), tc))
        continue;
      tcs.push_back(tc);
    }
    std::sort(tcs.begin(), tcs.end(),
              [](const Toolchain &a, const Toolchain &b) { return a.date > b.date; });
    for (const auto &tc : tcs) {
      bool is_default = tc.commit == def;
      std::cout << (is_default ? "* " : "  ") << tc.commit.substr(0, 12);
      std::cout << "  " << (tc.ref.empty() ? "(unknown ref)" : tc.ref);
      std::cout << "  " << (tc.date.empty() ? "unknown-date" : tc.date);
      std::cout << "  " << (tc.has_havel ? "havel" : "-");
      if (tc.has_hvtest)
        std::cout << "+hvtest";
      std::cout << std::endl;
      if (is_default) {
        std::cout << "  (default)" << std::endl;
      }
      found = true;
    }
    if (!found)
      std::cout << "no toolchains installed" << std::endl;
    return 0;
  }

  // ---------------------------------------------------------------- current
  if (sub == "current") {
    std::string sel = selected_toolchain();
    if (sel.empty()) {
      std::cout << "no toolchain selected (install one, then 'hpkg toolchain use <ref>')"
                << std::endl;
      return 1;
    }
    fs::path root = get_toolchains_dir();
    std::string commit = resolve_installed(root, sel);
    if (commit.empty()) {
      std::cerr << "hpkg toolchain: selected toolchain not installed: " << sel << std::endl;
      return 1;
    }
    Toolchain tc;
    read_toolchain_info(toolchain_dir(commit), tc);
    std::cout << commit;
    if (!tc.ref.empty())
      std::cout << " (" << tc.ref << ")";
    std::cout << std::endl;
    return 0;
  }

  // -------------------------------------------------------------------- use
  if (sub == "use") {
    if (rest.empty()) {
      std::cerr << "hpkg toolchain use: specify a ref" << std::endl;
      return 1;
    }
    fs::path root = get_toolchains_dir();
    std::string commit = resolve_installed(root, rest[0]);
    if (commit.empty()) {
      std::cerr << "hpkg toolchain use: not installed: " << rest[0] << std::endl;
      return 1;
    }
    fs::create_directories(root);
    std::ofstream f(root / "default");
    if (!f.is_open()) {
      std::cerr << "hpkg: cannot write default toolchain" << std::endl;
      return 1;
    }
    f << commit << "\n";
    std::cout << "default toolchain set to " << commit.substr(0, 12) << std::endl;
    return 0;
  }

  // ----------------------------------------------------------------- remove
  if (sub == "remove") {
    if (rest.empty()) {
      std::cerr << "hpkg toolchain remove: specify a ref" << std::endl;
      return 1;
    }
    fs::path root = get_toolchains_dir();
    std::string commit = resolve_installed(root, rest[0]);
    if (commit.empty()) {
      std::cerr << "hpkg toolchain remove: not installed: " << rest[0] << std::endl;
      return 1;
    }
    if (selected_toolchain() == commit) {
      std::cerr << "hpkg toolchain remove: " << commit.substr(0, 12)
                << " is the default; switch with 'hpkg toolchain use' first" << std::endl;
      return 1;
    }
    std::error_code ec;
    fs::remove_all(toolchain_dir(commit), ec);
    std::cout << "removed toolchain " << commit.substr(0, 12) << std::endl;
    return 0;
  }

  // ------------------------------------------------------------------- tags
  // Every tag in the repository with the commit it points at, most recent
  // first, annotated with whether that commit is already installed. This is
  // how you discover what versions exist before installing one: `install`
  // resolves a tag through git rev-parse <ref>^{commit}, so any name listed
  // here is directly installable.
  if (sub == "tags") {
    fs::path repo;
    int limit = 0;
    bool all = false;
    for (size_t i = 0; i < rest.size(); ++i) {
      if (rest[i] == "--repo" && i + 1 < rest.size()) {
        repo = rest[++i];
      } else if (rest[i] == "--limit" && i + 1 < rest.size()) {
        limit = std::stoi(rest[++i]);
      } else if (rest[i] == "--all") {
        all = true;
      } else if (rest[i][0] == '-') {
        std::cerr << "hpkg toolchain tags: unknown option " << rest[i] << std::endl;
        return 1;
      }
    }
    if (repo.empty()) repo = repo_root_from_cwd();
    if (repo.empty()) {
      std::cerr << "hpkg toolchain tags: not inside a git repository; pass --repo <path>"
                << std::endl;
      return 1;
    }

    std::string out;
    std::string q = "git -C '" + repo.string() +
                    "' for-each-ref --format='%(refname:short) %(objectname) %(creatordate:short)' "
                    "refs/tags";
    if (run_capture(q, &out) != 0) {
      std::cerr << "hpkg toolchain tags: cannot read tags from " << repo.string() << std::endl;
      return 1;
    }

    struct TagRow {
      std::string name;
      std::string commit;
      std::string date;
    };
    std::vector<TagRow> rows;
    std::istringstream lines(out);
    std::string line;
    while (std::getline(lines, line)) {
      line = trim(line);
      if (line.empty()) continue;
      std::istringstream parts(line);
      TagRow row;
      if (!(parts >> row.name >> row.commit >> row.date)) continue;
      // An annotated tag object name is not the commit it points at; peel it.
      if (row.commit.size() == 40) {
        std::string peeled;
        if (run_capture("git -C '" + repo.string() + "' rev-parse '" + row.commit +
                            "^{commit}'",
                        &peeled) == 0) {
          row.commit = trim(peeled);
        }
      }
      rows.push_back(row);
    }
    // git lists tags lexicographically, so v0.10 would sort before v0.9. Sort
    // by date descending, falling back to name for tags sharing a date.
    std::sort(rows.begin(), rows.end(), [](const TagRow &a, const TagRow &b) {
      if (a.date != b.date) return a.date > b.date;
      return a.name < b.name;
    });

    fs::path root = get_toolchains_dir();
    size_t shown = 0;
    for (const auto &row : rows) {
      if (limit > 0 && shown >= (size_t)limit) break;
      std::cout << row.name;
      if (row.commit.size() >= 12) std::cout << "  " << row.commit.substr(0, 12);
      std::cout << "  " << (row.date.empty() ? "unknown-date" : row.date);
      if (fs::is_directory(root / row.commit)) {
        std::cout << "  [installed]";
      } else if (!all) {
        std::cout << "  (install with: hpkg toolchain install " << row.name << ")";
      }
      std::cout << std::endl;
      shown++;
    }
    if (shown == 0) {
      std::cout << "no tags in " << repo.string() << std::endl;
    }
    return 0;
  }

  // ------------------------------------------------------------------ which
  if (sub == "which") {
    if (rest.empty()) {
      std::cerr << "hpkg toolchain which: specify a ref" << std::endl;
      return 1;
    }
    fs::path root = get_toolchains_dir();
    std::string commit = resolve_installed(root, rest[0]);
    if (commit.empty()) {
      std::cerr << "hpkg toolchain which: not installed: " << rest[0] << std::endl;
      return 1;
    }
    fs::path bin = toolchain_dir(commit) / "havel";
    if (!fs::exists(bin)) {
      std::cerr << "hpkg toolchain which: no havel binary at " << commit.substr(0, 12) << std::endl;
      return 1;
    }
    std::cout << bin.string() << std::endl;
    return 0;
  }

  // -------------------------------------------------------------------- run
  if (sub == "run") {
    if (rest.empty()) {
      std::cerr << "hpkg toolchain run: specify a ref" << std::endl;
      return 1;
    }
    fs::path root = get_toolchains_dir();
    std::string commit = resolve_installed(root, rest[0]);
    if (commit.empty()) {
      std::cerr << "hpkg toolchain run: not installed: " << rest[0] << std::endl;
      return 1;
    }
    fs::path bin = toolchain_dir(commit) / "havel";
    if (!fs::exists(bin)) {
      std::cerr << "hpkg toolchain run: no havel binary at " << commit.substr(0, 12) << std::endl;
      return 1;
    }
    std::string argv_line = "'" + bin.string() + "'";
    for (size_t i = 1; i < rest.size(); ++i)
      argv_line += " '" + rest[i] + "'";
    int rc = run_capture(argv_line, nullptr);
    return rc < 0 ? 1 : rc;
  }

  // ----------------------------------------------------------------- regress
  // Run the same command under several toolchains and report pass/fail per
  // commit. The point is to find the commit that broke something.
  if (sub == "regress") {
    std::vector<std::string> refs;
    std::vector<std::string> cmd;
    bool in_cmd = false;
    for (const auto &a : rest) {
      if (a == "--") {
        in_cmd = true;
        continue;
      }
      if (in_cmd)
        cmd.push_back(a);
      else
        refs.push_back(a);
    }
    if (refs.empty() || cmd.empty()) {
      std::cerr << "hpkg toolchain regress: usage: regress <ref...> -- <command...>" << std::endl;
      return 1;
    }
    fs::path root = get_toolchains_dir();
    std::string pass_line, fail_line;
    for (const auto &ref : refs) {
      std::string commit = resolve_installed(root, ref);
      if (commit.empty()) {
        std::cerr << "hpkg toolchain regress: not installed: " << ref << std::endl;
        fail_line += ref + " ";
        continue;
      }
      fs::path bin = toolchain_dir(commit) / "havel";
      if (!fs::exists(bin)) {
        fail_line += ref + " ";
        continue;
      }
      std::string argv_line = "'" + bin.string() + "'";
      for (const auto &c : cmd)
        argv_line += " '" + c + "'";
      std::cout << "=== " << commit.substr(0, 12) << " (" << ref << ")" << std::endl;
      int rc = run_capture(argv_line, nullptr);
      if (rc == 0) {
        pass_line += ref + " ";
      } else {
        fail_line += ref + " ";
      }
    }
    std::cout << "\npassed: " << (pass_line.empty() ? "(none)" : pass_line) << std::endl;
    std::cout << "failed: " << (fail_line.empty() ? "(none)" : fail_line) << std::endl;
    return fail_line.empty() ? 0 : 1;
  }

  std::cerr << "hpkg toolchain: unknown subcommand '" << sub << "'" << std::endl;
  return 1;
}

// ============================================================================
// Main
// ============================================================================

static void print_usage(const char *prog) {
  std::cerr << "usage: hpkg <command> [args...]\n"
               "\n"
               "commands:\n"
               "  init [name]          create a new package in current directory\n"
               "  install <path|name>  install a package from a local directory or .hv file\n"
               "  list                 list installed packages\n"
               "  info [name]          show package info (current dir or named package)\n"
               "  publish [--local]    validate package (and optionally install locally)\n"
               "  remove <name>        remove an installed package\n"
        "\n"
        "toolchains (per-commit binaries for regression testing):\n"
        "  toolchain install <ref> [--from <path>] [--repo <path>] [--jobs N]\n"
        "                       register a havel binary for a commit/tag; builds from\n"
        "                       a git worktree unless --from points at a prebuilt one\n"
        "  toolchain list       list installed toolchains (\* = selected)\n"
        "  toolchain use <ref>  set the default toolchain\n"
        "  toolchain current    print the selected toolchain\n"
        "  toolchain remove <ref>  uninstall a toolchain\n"
        "  toolchain which <ref>   print the binary path for a toolchain\n"
        "  toolchain tags [--repo <path>] [--limit N]\n"
        "                       list the repo's tags with the commit each points at\n"
        "  toolchain run <ref> <args...>  run that toolchain's havel\n"
        "  toolchain regress <ref...> -- <cmd...>  run <cmd> under each toolchain\n"
        "\n"
        "  toolchains live in ~/.havel/toolchains/<commit>/\n"
        "  selection: HAVEL_TOOLCHAIN env var overrides ~/.havel/toolchains/default\n"
               "\n"
               "package layout:\n"
               " my-pkg/\n"
               "   havel.toml          package manifest\n"
               "   my-pkg.hv           entry point\n"
               "   *.hv                additional sources\n"
               "\n"
               "installed packages live in ~/.havel/packages/<name>/\n";
}

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  std::string cmd = argv[1];
  std::vector<std::string> args;
  for (int i = 2; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  if (cmd == "init")
    return cmd_init(args);
  else if (cmd == "install")
    return cmd_install(args);
  else if (cmd == "toolchain")
    return cmd_toolchain(args);
  else if (cmd == "list")
    return cmd_list(args);
  else if (cmd == "info")
    return cmd_info(args);
  else if (cmd == "publish")
    return cmd_publish(args);
  else if (cmd == "remove")
    return cmd_remove(args);
  else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
    print_usage(argv[0]);
    return 0;
  } else {
    std::cerr << "hpkg: unknown command '" << cmd << "'" << std::endl;
    print_usage(argv[0]);
    return 1;
  }
}
