#include <algorithm>
#include <cstdlib>
#include <set>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <cstring>

namespace fs = std::filesystem;

static std::string trim(std::string s);

struct PackageManifest {
  std::string name;
  std::string version{"0.1.0"};
  std::string description;
  std::string author;
  std::string license{"MIT"};
  std::vector<std::string> dependencies;
  // Parallel to `dependencies`: the version constraint recorded for each
  // dependency ("*", "^1.2.3", ">=1.0.0, <2.0.0"). Empty when the manifest
  // gave no operator, which means "*".
  std::vector<std::string> dependency_specs;
  std::vector<std::string> files;
};

// ---------------------------------------------------------------------------
// Semantic versions and constraints (cargo/npm-shaped)
//
// Parsed forms: "1.2.3" (exact), "*" (any), "^1.2.3" (compatible: same major,
// or same minor when major is 0), "~1.2.3" (same minor), and comma-joined
// comparator chains ">=1.2.3, <2.0.0". Bare numbers mean exact; missing
// parts are zero, so "1" == "1.0.0".
struct SemVer {
  int major = 0;
  int minor = 0;
  int patch = 0;
};

static bool parse_semver(const std::string &raw, SemVer &out) {
  std::string s;
  // Tolerate a leading 'v' and drop any pre-release/build suffix: a
  // published 1.0.0-beta satisfies nothing by suffix today, but it must not
  // make the numeric version unreadable.
  size_t start = 0;
  while (start < raw.size() && (raw[start] == 'v' || raw[start] == 'V'))
    ++start;
  for (size_t i = start; i < raw.size(); ++i) {
    char c = raw[i];
    if (c == '-' || c == '+')
      break;
    s.push_back(c);
  }
  if (s.empty())
    return false;
  int part = 0, value = 0, count = 0;
  for (char c : s) {
    if (c == '.') {
      if (part > 2)
        return false;
      if (count == 0)
        return false; // "1..2"
      if (part == 0)
        out.major = value;
      else if (part == 1)
        out.minor = value;
      else
        out.patch = value;
      value = 0;
      count = 0;
      ++part;
      if (part > 3)
        return false;
      continue;
    }
    if (c < '0' || c > '9')
      return false;
    value = value * 10 + (c - '0');
    ++count;
  }
  if (count == 0)
    return false;
  if (part == 0)
    out.major = value;
  else if (part == 1)
    out.minor = value;
  else
    out.patch = value;
  return true;
}

static int compare_semver(const SemVer &a, const SemVer &b) {
  if (a.major != b.major)
    return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor)
    return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch)
    return a.patch < b.patch ? -1 : 1;
  return 0;
}

// One comparator term of a constraint.
static bool term_satisfies(const SemVer &v, const std::string &term_in) {
  std::string term = trim(term_in);
  if (term.empty() || term == "*" || term == "x" || term == "X")
    return true;
  SemVer base{};
  if (term[0] == '^' || term[0] == '~') {
    if (!parse_semver(term.substr(1), base))
      return false;
    if (compare_semver(v, base) < 0)
      return false;
    SemVer upper = base;
    if (term[0] == '^') {
      // Caret: while major is 0 the minor is the breaking axis (npm's rule).
      if (base.major > 0) {
        upper = {base.major + 1, 0, 0};
      } else if (base.minor > 0) {
        upper = {0, base.minor + 1, 0};
      } else {
        upper = {0, 0, base.patch + 1};
      }
    } else {
      upper = {base.major, base.minor + 1, 0};
    }
    return compare_semver(v, upper) < 0;
  }
  static const char *ops[] = {">=", "<=", "==", "!=", ">", "<", "="};
  for (const char *op : ops) {
    size_t n = std::strlen(op);
    if (term.size() > n && term.compare(0, n, op) == 0) {
      if (!parse_semver(term.substr(n), base))
        return false;
      int cmp = compare_semver(v, base);
      std::string o(op);
      if (o == ">=")
        return cmp >= 0;
      if (o == "<=")
        return cmp <= 0;
      if (o == "==" || o == "=")
        return cmp == 0;
      if (o == "!=")
        return cmp != 0;
      if (o == ">")
        return cmp > 0;
      if (o == "<")
        return cmp < 0;
    }
  }
  // Bare version: exact.
  if (!parse_semver(term, base))
    return false;
  return compare_semver(v, base) == 0;
}

static bool satisfies(const std::string &version, const std::string &constraint) {
  SemVer v{};
  if (!parse_semver(version, v))
    return false;
  std::string c = trim(constraint);
  if (c.empty() || c == "*")
    return true;
  // Comma-joined terms are AND-ed; "||" (or) is rejected rather than
  // half-supported, so an unparseable spec never silently resolves.
  if (c.find("||") != std::string::npos)
    return false;
  std::istringstream parts(c);
  std::string term;
  while (std::getline(parts, term, ',')) {
    if (!term_satisfies(v, term))
      return false;
  }
  return true;
}

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
        std::string spec = line.substr(eq + 1);
        auto vstart = spec.find_first_not_of(" \t\"");
        auto vend = spec.find_last_not_of(" \t\"");
        spec =
            (vstart == std::string::npos) ? std::string() : spec.substr(vstart, vend - vstart + 1);
        manifest.dependencies.push_back(dep);
        manifest.dependency_specs.push_back(spec);
      } else {
        manifest.dependencies.push_back(line);
        manifest.dependency_specs.push_back("");
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
    for (size_t i = 0; i < m.dependencies.size(); ++i) {
      std::string spec = (i < m.dependency_specs.size()) ? m.dependency_specs[i] : "";
      if (trim(spec).empty() || trim(spec) == "*")
        spec = "*";
      f << m.dependencies[i] << " = \"" << spec << "\"\n";
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
    f << "}\n";
    // No `export` keyword: Havel modules are Python-style, so every top-level
    // function is already the package's API. An `export hello` line here makes
    // the scaffolded package fail to compile with
    // "SemanticError: Unresolved identifier 'export'".
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

// ---------------------------------------------------------------------------
// Registry, lockfile and resolution
//
// Layout (npm/cargo-shaped, filesystem-backed until a network registry
// exists):
//   ~/.havel/registry/<name>/<version>/   published package contents
//   ~/.havel/packages/<name>/             installed package (one version)
//   ~/.havel/havel.lock                   pin: name <tab> version <tab> source
//                                        <tab> spec
//
// The registry is a directory, so installs work with no server and no
// network; the lockfile records which version a name resolved to and where
// it came from, so `update` re-resolves against the same constraint and
// `remove` can see installed versions. HAVEL_REGISTRY_DIR (and HAVEL_TOOLCHAIN_STORE's
// sibling) redirect the store for isolated test runs.

static std::string get_registry_dir() {
  if (const char *env = std::getenv("HAVEL_REGISTRY_DIR")) {
    std::string v = trim(env);
    if (!v.empty())
      return v;
  }
  const char *home = std::getenv("HOME");
  if (!home) {
    std::cerr << "hpkg: HOME not set" << std::endl;
    return "";
  }
  return (fs::path(home) / ".havel" / "registry").string();
}

static std::string lock_file_path() {
  std::string pkgs = get_packages_dir();
  if (pkgs.empty())
    return "";
  return (fs::path(pkgs).parent_path() / "havel.lock").string();
}

struct LockEntry {
  std::string name;
  std::string version;
  std::string source; // registry path or the local directory installed from
  std::string spec;   // constraint this version was resolved against
};

static std::vector<LockEntry> read_lock() {
  std::vector<LockEntry> out;
  std::string path = lock_file_path();
  if (path.empty())
    return out;
  std::ifstream f(path);
  if (!f.is_open())
    return out;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream parts(line);
    LockEntry e;
    if (std::getline(parts, e.name, '\t') && std::getline(parts, e.version, '\t') &&
        std::getline(parts, e.source, '\t') && std::getline(parts, e.spec)) {
      out.push_back(e);
    }
  }
  return out;
}

static bool write_lock(const std::vector<LockEntry> &entries) {
  std::string path = lock_file_path();
  if (path.empty())
    return false;
  std::ofstream f(path, std::ios::trunc);
  if (!f.is_open())
    return false;
  f << "# havel lockfile - name<TAB>version<TAB>source<TAB>spec\n";
  for (const auto &e : entries)
    f << e.name << "\t" << e.version << "\t" << e.source << "\t" << e.spec << "\n";
  return true;
}

// Add or replace a lock entry (one entry per name: one installed version).
static void lock_upsert(std::vector<LockEntry> &entries, const LockEntry &e) {
  for (auto &existing : entries) {
    if (existing.name == e.name) {
      existing = e;
      return;
    }
  }
  entries.push_back(e);
}

static void lock_remove(std::vector<LockEntry> &entries, const std::string &name) {
  for (auto it = entries.begin(); it != entries.end();) {
    it = (it->name == name) ? entries.erase(it) : it + 1;
  }
}

// Every version directory published under <name>, ascending.
static std::vector<std::string> registry_versions(const std::string &name) {
  std::vector<std::string> out;
  std::string reg = get_registry_dir();
  if (reg.empty())
    return out;
  fs::path dir = fs::path(reg) / name;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (!entry.is_directory())
      continue;
    SemVer v{};
    if (parse_semver(entry.path().filename().string(), v))
      out.push_back(entry.path().filename().string());
  }
  std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
    SemVer va{}, vb{};
    parse_semver(a, va);
    parse_semver(b, vb);
    return compare_semver(va, vb) < 0;
  });
  return out;
}

// Highest published version satisfying the constraint. "" when none does;
// `available` lists what exists for the error message.
static std::string resolve_registry_version(const std::string &name, const std::string &spec,
                                            std::string &available) {
  available.clear();
  for (const auto &v : registry_versions(name)) {
    if (!available.empty())
      available += ", ";
    available += v;
  }
  std::vector<std::string> versions = registry_versions(name);
  for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
    if (satisfies(*it, spec))
      return *it;
  }
  return "";
}

static std::string manifest_spec(const PackageManifest &m, const std::string &dep) {
  for (size_t i = 0; i < m.dependencies.size(); ++i) {
    if (m.dependencies[i] == dep) {
      std::string s = (i < m.dependency_specs.size()) ? m.dependency_specs[i] : "";
      return trim(s).empty() ? "*" : s;
    }
  }
  return "*";
}

// Copy a published registry payload into ~/.havel/packages/<name>.
static bool copy_payload(const fs::path &dir, const fs::path &dest) {
  std::error_code ec;
  fs::remove_all(dest, ec);
  fs::create_directories(dest, ec);
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (!entry.is_regular_file())
      continue;
    fs::copy_file(entry.path(), dest / entry.path().filename(),
                  fs::copy_options::overwrite_existing, ec);
  }
  return !ec;
}

// Recursive install from the registry. `visiting` guards cycles; returns
// nonzero on the first unresolved dependency and names it.
static int install_registry_package(const std::string &name, const std::string &spec,
                                    std::string &installed_version,
                                    std::set<std::string> &visiting) {
  if (visiting.count(name)) {
    std::cerr << "hpkg: circular dependency: " << name << " is already being installed"
              << std::endl;
    return 1;
  }
  visiting.insert(name);
  int rc = 0;

  std::string reg = get_registry_dir();
  if (reg.empty()) {
    visiting.erase(name);
    return 1;
  }

  std::string available;
  std::string version = resolve_registry_version(name, spec, available);
  if (version.empty()) {
    std::cerr << "hpkg: no version of '" << name << "' satisfies '" << spec << "'";
    if (!available.empty())
      std::cerr << " (available: " << available << ")";
    std::cerr << std::endl;
    visiting.erase(name);
    return 1;
  }

  fs::path src = fs::path(reg) / name / version;
  fs::path dest = fs::path(get_packages_dir()) / name;
  if (!copy_payload(src, dest)) {
    std::cerr << "hpkg: install of " << name << " failed" << std::endl;
    visiting.erase(name);
    return 1;
  }
  installed_version = version;
  std::cout << "  installed " << name << "@" << version << std::endl;

  // Recurse into this version's own dependencies, so a broken transitive
  // dep fails the whole install. This package's lock entry goes down before
  // the loop, keeping the lock in pre-order (app before its deps).
  {
    std::vector<LockEntry> entries = read_lock();
    lock_upsert(entries, LockEntry{name, version, src.string(), spec});
    PackageManifest m;
    if (parse_havel_toml(dest / "havel.toml", m) && !m.dependencies.empty()) {
      for (size_t i = 0; i < m.dependencies.size(); ++i) {
        std::string dep_spec = (i < m.dependency_specs.size()) ? m.dependency_specs[i] : "";
        dep_spec = trim(dep_spec).empty() ? "*" : dep_spec;
        std::string dep_version;
        int dep_rc = install_registry_package(m.dependencies[i], dep_spec, dep_version, visiting);
        if (dep_rc != 0) {
          rc = dep_rc;
          break;
        }
        lock_upsert(entries, LockEntry{m.dependencies[i], dep_version,
                                       (fs::path(reg) / m.dependencies[i] / dep_version).string(),
                                       dep_spec});
      }
    }
    if (rc == 0 && !write_lock(entries)) {
      std::cerr << "hpkg: cannot write lockfile" << std::endl;
      rc = 1;
    }
  }

  visiting.erase(name);
  return rc;
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

      // Path/directory installs resolve their own dependencies from the
      // registry too (cargo's behavior for path dependencies), and pin the
      // result in the lockfile with the directory as source.
      if (!manifest.dependencies.empty()) {
        for (size_t i = 0; i < manifest.dependencies.size(); ++i) {
          std::string dep_spec =
              (i < manifest.dependency_specs.size()) ? manifest.dependency_specs[i] : "";
          dep_spec = trim(dep_spec).empty() ? "*" : dep_spec;
          std::string dep_version;
          std::set<std::string> visiting;
          if (install_registry_package(manifest.dependencies[i], dep_spec, dep_version, visiting) !=
              0) {
            return 1;
          }
        }
      }
      {
        std::vector<LockEntry> entries = read_lock();
        lock_upsert(entries, LockEntry{manifest.name, manifest.version, arg, "*"});
        if (!write_lock(entries)) {
          std::cerr << "hpkg: cannot write lockfile" << std::endl;
          return 1;
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

    // Bare name (optionally name@spec): resolve from the registry, like npm
    // resolving a package name. Paths above have already taken precedence.
    if (arg.find('/') == std::string::npos && arg.find('\\') == std::string::npos) {
      std::string name = arg, spec = "*";
      auto at = arg.find('@');
      if (at != std::string::npos && at > 0 && at + 1 < arg.size()) {
        name = arg.substr(0, at);
        spec = trim(arg.substr(at + 1));
      }
      std::string available;
      std::string version = resolve_registry_version(name, spec, available);
      if (!version.empty()) {
        std::string installed_version;
        std::set<std::string> visiting;
        return install_registry_package(name, spec, installed_version, visiting);
      }
      // Not published under this constraint: report what exists (an
      // "already installed" name whose version no longer satisfies a fresh
      // install request must not silently report success).
      if (!available.empty()) {
        std::cerr << "hpkg: no version of '" << name << "' satisfies '" << spec
                  << "' (available: " << available << ")" << std::endl;
        return 1;
      }
      fs::path installed = fs::path(pkgs_dir) / name;
      if (fs::exists(installed)) {
        PackageManifest manifest;
        fs::path toml = installed / "havel.toml";
        if (fs::exists(toml) && parse_havel_toml(toml, manifest)) {
          std::cout << name << " v" << manifest.version << " (already installed)" << std::endl;
        } else {
          std::cout << name << " (installed, no manifest)" << std::endl;
        }
        continue;
      }
      std::cerr << "hpkg: package not found in registry: " << name << std::endl;
      return 1;
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
      bool installed = !pkgs_dir.empty() && fs::exists(fs::path(pkgs_dir) / dep);
      // npm/cargo publish without every dependency installed: the
      // consumer's install resolves them. A dependency is publishable when
      // it is installed OR at least one version is published.
      bool published = !registry_versions(dep).empty();
      if (!installed && !published) {
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

  // Publish to the registry: copy the package into
  // <registry>/<name>/<version>/. npm refuses re-publishing the same
  // version, so do the same - an overwrite would silently change what a
  // lockfile pin means.
  std::string reg = get_registry_dir();
  if (reg.empty())
    return 1;
  fs::path dest = fs::path(reg) / manifest.name / manifest.version;
  std::error_code ec;
  if (fs::exists(dest)) {
    std::cerr << "hpkg: " << manifest.name << "@" << manifest.version
              << " is already published; bump the version in havel.toml" << std::endl;
    return 1;
  }
  fs::create_directories(dest, ec);
  if (ec) {
    std::cerr << "hpkg: cannot create " << dest.string() << ": " << ec.message() << std::endl;
    return 1;
  }
  fs::copy_file(toml, dest / "havel.toml", fs::copy_options::overwrite_existing, ec);
  if (ec) {
    std::cerr << "hpkg: copy havel.toml failed: " << ec.message() << std::endl;
    return 1;
  }
  for (const auto &f : hv_files) {
    fs::copy_file(cwd / f, dest / f, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      std::cerr << "hpkg: copy " << f << " failed: " << ec.message() << std::endl;
      return 1;
    }
  }
  std::cout << "\npublished " << manifest.name << "@" << manifest.version << " to " << reg
            << std::endl;
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

  bool force = false;
  std::vector<std::string> names;
  for (const auto &arg : args) {
    if (arg == "--force") {
      force = true;
      continue;
    }
    if (arg[0] == '-') {
      std::cerr << "hpkg remove: unknown option " << arg << std::endl;
      return 1;
    }
    names.push_back(arg);
  }
  if (names.empty()) {
    std::cerr << "hpkg remove: specify a package name" << std::endl;
    return 1;
  }

  for (const auto &name : names) {
    fs::path pkg_path = fs::path(pkgs_dir) / name;
    if (!fs::exists(pkg_path)) {
      std::cerr << "hpkg: package '" << name << "' not installed" << std::endl;
      return 1;
    }

    // npm/cargo refuse to break a dependent: scan every installed package
    // for a dependency on this name before removing it.
    std::vector<std::string> dependents;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(pkgs_dir, ec)) {
      if (ec)
        break;
      if (!entry.is_directory())
        continue;
      std::string other = entry.path().filename().string();
      if (other == name)
        continue;
      PackageManifest m;
      if (!parse_havel_toml(entry.path() / "havel.toml", m))
        continue;
      for (const auto &dep : m.dependencies) {
        if (dep == name)
          dependents.push_back(other);
      }
    }
    if (!dependents.empty() && !force) {
      std::cerr << "hpkg: cannot remove '" << name << "': needed by";
      for (const auto &d : dependents)
        std::cerr << " " << d;
      std::cerr << " (use --force to remove anyway)" << std::endl;
      return 1;
    }

    std::uintmax_t count = fs::remove_all(pkg_path);
    std::cout << "removed " << name << " (" << count << " files)" << std::endl;
    std::vector<LockEntry> entries = read_lock();
    lock_remove(entries, name);
    write_lock(entries);
  }

  return 0;
}

// Version ordering for humans: compare_semver's infertility is the only
// ordering hpkg needs.
static int compare_version_names(const std::string &a, const std::string &b) {
  SemVer va{}, vb{};
  if (!parse_semver(a, va))
    return -1;
  if (!parse_semver(b, vb))
    return 1;
  return compare_semver(va, vb);
}

// One tree line plus its dependency subtree. `seen` prevents endless
// recursion on a cycle; repeats are printed but not descended into.
static void print_tree_node(const std::string &name, int depth, std::set<std::string> seen) {
  std::string pkgs_dir = get_packages_dir();
  fs::path pkg = fs::path(pkgs_dir) / name;
  std::string version;
  PackageManifest m;
  if (parse_havel_toml(pkg / "havel.toml", m) && !m.version.empty())
    version = m.version;
  for (int i = 0; i < depth; ++i)
    std::cout << "  ";
  std::cout << name;
  if (!version.empty())
    std::cout << "@" << version;
  std::cout << std::endl;
  if (m.name.empty() || seen.count(name))
    return;
  seen.insert(name);
  for (const auto &dep : m.dependencies)
    print_tree_node(dep, depth + 1, seen);
}

// Update: re-resolve a locked package against its recorded spec and install
// the best available version. Only registry-sourced entries can update; a
// directory-installed package has no registry version to move to.
static int cmd_update(const std::vector<std::string> &args) {
  bool force = false;
  std::vector<std::string> names;
  for (const auto &arg : args) {
    if (arg == "--force") {
      force = true;
      continue;
    }
    if (arg[0] == '-') {
      std::cerr << "hpkg update: unknown option " << arg << std::endl;
      return 1;
    }
    names.push_back(arg);
  }

  std::vector<LockEntry> entries = read_lock();
  if (entries.empty()) {
    std::cout << "nothing to update (lockfile is empty)" << std::endl;
    return 0;
  }
  int rc = 0;
  for (auto &e : entries) {
    if (!names.empty() && std::find(names.begin(), names.end(), e.name) == names.end())
      continue;
    if (e.source.find("/registry/") == std::string::npos && !force) {
      std::cout << e.name << "@" << e.version << ": not installed from the registry" << std::endl;
      continue;
    }
    std::string spec = trim(e.spec).empty() ? "*" : e.spec;
    std::string available;
    std::string best = resolve_registry_version(e.name, spec, available);
    if (best.empty()) {
      std::cerr << "hpkg: no version of '" << e.name << "' satisfies '" << spec << "'";
      if (!available.empty())
        std::cerr << " (available: " << available << ")";
      std::cerr << std::endl;
      rc = 1;
      continue;
    }
    if (compare_version_names(best, e.version) <= 0) {
      std::cout << e.name << "@" << e.version << ": already up to date" << std::endl;
      continue;
    }
    std::string installed_version;
    std::set<std::string> visiting;
    std::string reg = get_registry_dir();
    if (install_registry_package(e.name, spec, installed_version, visiting) != 0) {
      rc = 1;
      continue;
    }
    std::cout << "updated " << e.name << ": " << e.version << " -> " << installed_version
              << std::endl;
    e.version = installed_version;
    e.source = (fs::path(reg) / e.name / installed_version).string();
  }
  write_lock(entries);
  return rc;
}

// Tree: every installed package and its dependency edges, npm ls-shaped.
static int cmd_tree(const std::vector<std::string> &args) {
  std::string pkgs_dir = get_packages_dir();
  if (pkgs_dir.empty())
    return 1;
  if (args.empty()) {
    // Roots: packages no other installed package depends on (plus every
    // package when nothing forms a cycle).
    std::vector<LockEntry> entries = read_lock();
    std::set<std::string> depended;
    for (const auto &e : entries) {
      PackageManifest m;
      if (parse_havel_toml(fs::path(pkgs_dir) / e.name / "havel.toml", m))
        for (const auto &d : m.dependencies)
          depended.insert(d);
    }
    for (const auto &e : entries) {
      if (!depended.count(e.name))
        print_tree_node(e.name, 0, {});
    }
    return 0;
  }
  for (const auto &name : args) {
    print_tree_node(name, 0, {});
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
    if (rc == -1)
      return -1;
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
  if (run_capture("ldd '" + bin.string() + "'", &out) != 0) {
    std::cerr << "hpkg: warning: ldd failed on " << bin.string()
              << "; toolchain may miss runtime .so files" << std::endl;
    return;
  }
  std::istringstream lines(out);
  std::string line;
  std::error_code ec;
  while (std::getline(lines, line)) {
    auto arrow = line.find("=> not found");
    if (arrow == std::string::npos)
      continue;
    std::string name = trim(line.substr(0, arrow));
    if (name.empty())
      continue;
    // Search the source build dir and its toolkits/ subdir for the library.
    bool copied = false;
    for (const fs::path &dir : {source_dir, source_dir / "toolkits"}) {
      fs::path candidate = dir / name;
      if (fs::exists(candidate)) {
        fs::copy_file(candidate, bin.parent_path() / name, fs::copy_options::overwrite_existing,
                      ec);
        copied = true;
        break;
      }
    }
    if (!copied) {
      // Missing at install time = dead toolchain on first run. Say so now
      // instead of leaving the user to decode a loader error later.
      std::cerr << "hpkg: warning: runtime lib '" << name << "' not found under "
                << source_dir.string() << " - '" << bin.filename().string()
                << "' will fail to start" << std::endl;
    }
  }
}

// Plugins and toolkit backends are dlopen'd at runtime from directories
// found exe-relative (<toolchain>/modules, <toolchain>/toolkits), and a
// plugin's own Havel-owned helper libs (libhavel_*.so, e.g.
// libhavel_ffi_types.so for the ffi plugin) resolve from the plugin's
// RUNPATH one level up. ldd on `havel` cannot see any of this: the
// executable has no Havel-owned DT_NEEDED at all. Copying only the
// binaries therefore produced a toolchain whose scripts died on the first
// native module ("libhavel_ffi_types.so: cannot open shared object
// file"). This copies the whole dlopen payload.
static void copy_runtime_payload(const fs::path &source_dir, const fs::path &tc_dir) {
  std::error_code ec;
  // Native module plugins (havel_mod_*.so), found exe-relative.
  if (fs::is_directory(source_dir / "modules")) {
    fs::create_directories(tc_dir / "modules", ec);
    for (const auto &entry : fs::directory_iterator(source_dir / "modules", ec)) {
      if (ec)
        break;
      if (!entry.is_regular_file())
        continue;
      if (entry.path().extension() == ".so") {
        fs::copy_file(entry.path(), tc_dir / "modules" / entry.path().filename(),
                      fs::copy_options::overwrite_existing, ec);
      }
    }
  }
  // Toolkit backends, found exe-relative.
  if (fs::is_directory(source_dir / "toolkits")) {
    fs::create_directories(tc_dir / "toolkits", ec);
    for (const auto &entry : fs::directory_iterator(source_dir / "toolkits", ec)) {
      if (ec)
        break;
      if (!entry.is_regular_file())
        continue;
      if (entry.path().extension() == ".so") {
        fs::copy_file(entry.path(), tc_dir / "toolkits" / entry.path().filename(),
                      fs::copy_options::overwrite_existing, ec);
      }
    }
  }
  // Havel-owned helper libraries sit in the build root / <prefix>/lib, one
  // level above the plugin dir; the plugin RUNPATH ($ORIGIN/..) finds them
  // there. ldd cannot derive this set (see above), so it is every
  // libhavel*.so the build produced - .a static archives are not runtime.
  for (const auto &entry : fs::directory_iterator(source_dir, ec)) {
    if (ec)
      break;
    if (!entry.is_regular_file())
      continue;
    std::string fname = entry.path().filename().string();
    if (fname.rfind("libhavel", 0) == 0 && entry.path().extension() == ".so") {
      fs::copy_file(entry.path(), tc_dir / fname, fs::copy_options::overwrite_existing, ec);
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
      copy_runtime_libs(dir / "havel", fs::is_directory(src) ? src : src.parent_path());
      copy_runtime_payload(fs::is_directory(src) ? src : src.parent_path(), dir);
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
      // Copy the binaries FIRST: copy_runtime_libs derives the .so set by
      // running ldd on the toolchain's own copy of `havel`. It ran on
      // dir/"havel" before that file existed, ldd failed, and the function
      // returned silently - the install produced a binary with no runtime
      // libs (loader error 127 on first run). Same order as the copied path
      // above (binary copy at :796 precedes its copy_runtime_libs at :815).
      for (const char *name : {"havel", "hvtest"}) {
        fs::path src = wt / "build-debug" / name;
        if (fs::exists(src)) {
          fs::copy_file(src, dir / name, fs::copy_options::overwrite_existing, ec);
        }
      }
      copy_runtime_libs(dir / "havel", wt / "build-debug");
      copy_runtime_payload(wt / "build-debug", dir);
      tc.mode = "built";
      tc.origin = repo.string();
      run_capture("git -C '" + repo.string() + "' worktree remove --force '" + wt.string() + "'",
                  nullptr);
      fs::remove_all(wt, ec);
    }

    write_toolchain_info(dir, tc);
    // The install is not done until the installed binary starts. Every
    // "cannot open shared object file" defect in this area reported success
    // at copy time and failed at first use; running the artifact is the
    // difference between a tool that verifies and one that lies.
    {
      std::string ver;
      if (run_capture("'" + (dir / "havel").string() + "' --version", &ver) != 0 ||
          ver.find("havel") == std::string::npos) {
        std::cerr << "hpkg: installed toolchain binary does not run: " << (dir / "havel").string()
                  << " - removing it instead of reporting success" << std::endl;
        std::error_code rmec;
        fs::remove_all(dir, rmec);
        return 1;
      }
    }
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
    // Surface the env override before the list: a default chosen from the
    // environment is indistinguishable from the default file otherwise.
    if (const char *env = std::getenv("HAVEL_TOOLCHAIN"); env && trim(env)[0] != '\0') {
      std::cout << "note: HAVEL_TOOLCHAIN=" << trim(env) << " overrides ~/.havel/toolchains/default"
                << std::endl;
    }
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
    f.close();
    // Read back what was written: a "success" message that a later `list`
    // or `current` contradicts was the exact "use does nothing" symptom,
    // and a write that silently landed in the wrong place is otherwise
    // invisible until someone runs two commands in a row.
    std::string saved;
    {
      std::ifstream back(root / "default");
      if (!back.is_open())
        saved = "<unreadable>";
      else
        std::getline(back, saved);
    }
    if (trim(saved) != commit) {
      std::cerr << "hpkg: cannot read default toolchain back (got '" << trim(saved) << "', wrote '"
                << commit << "')" << std::endl;
      return 1;
    }
    std::cout << "default toolchain set to " << commit.substr(0, 12) << std::endl;
    // The default file is only consulted when HAVEL_TOOLCHAIN is unset;
    // a stale env value makes every later `list`/`run` ignore what was
    // just written, which reads exactly like "use did not take effect".
    if (const char *env = std::getenv("HAVEL_TOOLCHAIN"); env && trim(env)[0] != '\0') {
      std::cout << "note: HAVEL_TOOLCHAIN=" << trim(env)
                << " is set and overrides the default file; unset it to use "
                << commit.substr(0, 12) << std::endl;
    }
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
    if (repo.empty())
      repo = repo_root_from_cwd();
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
      if (line.empty())
        continue;
      std::istringstream parts(line);
      TagRow row;
      if (!(parts >> row.name >> row.commit >> row.date))
        continue;
      // An annotated tag object name is not the commit it points at; peel it.
      if (row.commit.size() == 40) {
        std::string peeled;
        if (run_capture("git -C '" + repo.string() + "' rev-parse '" + row.commit + "^{commit}'",
                        &peeled) == 0) {
          row.commit = trim(peeled);
        }
      }
      rows.push_back(row);
    }
    // git lists tags lexicographically, so v0.10 would sort before v0.9. Sort
    // by date descending, falling back to name for tags sharing a date.
    std::sort(rows.begin(), rows.end(), [](const TagRow &a, const TagRow &b) {
      if (a.date != b.date)
        return a.date > b.date;
      return a.name < b.name;
    });

    fs::path root = get_toolchains_dir();
    size_t shown = 0;
    for (const auto &row : rows) {
      if (limit > 0 && shown >= (size_t)limit)
        break;
      std::cout << row.name;
      if (row.commit.size() >= 12)
        std::cout << "  " << row.commit.substr(0, 12);
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
      std::cerr << "hpkg toolchain run: specify a ref or a command "
                << "(with no ref, the current toolchain runs the args)" << std::endl;
      return 1;
    }
    fs::path root = get_toolchains_dir();
    // First arg is a toolchain ref only if it resolves to an installed one.
    // Anything else (a script path, -flags, ...) means "run with the
    // current toolchain" - `hpkg toolchain run ./script.hv` is the natural
    // spelling, and treating the path as a ref name produced the absurd
    // "not installed: /home/.../script.hv".
    std::string commit = resolve_installed(root, rest[0]);
    size_t arg_start = 1;
    if (commit.empty()) {
      commit = resolve_installed(root, selected_toolchain());
      arg_start = 0;
      if (commit.empty()) {
        std::cerr << "hpkg toolchain run: '" << rest[0]
                  << "' is not an installed toolchain and no default is set" << std::endl;
        return 1;
      }
    }
    fs::path bin = toolchain_dir(commit) / "havel";
    if (!fs::exists(bin)) {
      std::cerr << "hpkg toolchain run: no havel binary at " << commit.substr(0, 12) << std::endl;
      return 1;
    }
    std::string argv_line = "'" + bin.string() + "'";
    for (size_t i = arg_start; i < rest.size(); ++i)
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
               "  install <path|name>  install a package from a local directory, .hv file,\n"
               "                       registry name, or name@spec (spec: 1.2.3, ^1.2.3,\n"
               "                       ~1.2.3, >=1.2.3, <2.0.0, or comma-joined chains)\n"
               "  list                 list installed packages\n"
               "  info [name]          show package info (current dir or named package)\n"
               "  publish [--local]    publish to ~/.havel/registry/<name>/<version>\n"
               "                       (--local installs into ~/.havel/packages instead)\n"
               "  remove <name>        remove an installed package (--force skips the\n"
               "                       dependents check)\n"
               "  update [name...]      re-resolve locked packages against their specs\n"
               "  tree [name...]        dependency tree of installed packages\n"
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
               "installed packages live in ~/.havel/packages/<name>/\n"
               "registry lives in ~/.havel/registry/<name>/<version>/\n"
               "installed pins live in ~/.havel/havel.lock (name<TAB>version<TAB>source<TAB>spec)\n"
               "HAVEL_REGISTRY_DIR redirects the registry for isolated test runs\n";
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
  else if (cmd == "update")
    return cmd_update(args);
  else if (cmd == "tree")
    return cmd_tree(args);
  else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
    print_usage(argv[0]);
    return 0;
  } else {
    std::cerr << "hpkg: unknown command '" << cmd << "'" << std::endl;
    print_usage(argv[0]);
    return 1;
  }
}
