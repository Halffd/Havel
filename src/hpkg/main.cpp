#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
    const char* home = std::getenv("HOME");
    if (!home) {
        std::cerr << "hpkg: HOME not set" << std::endl;
        return "";
    }
    return (fs::path(home) / ".havel" / "packages").string();
}

// Parses only the flat key=value [package] and [dependencies] sections we generate.
static bool parse_havel_toml(const fs::path& path, PackageManifest& manifest) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    bool in_deps = false;
    while (std::getline(f, line)) {
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        auto end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) line = line.substr(0, end + 1);

        if (line.empty() || line[0] == '#') continue;

        if (line == "[package]") { in_deps = false; continue; }
        if (line == "[dependencies]") { in_deps = true; continue; }

        if (in_deps) {
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                std::string dep = line.substr(0, eq);
                auto dep_end = dep.find_last_not_of(" \t");
                if (dep_end != std::string::npos) dep = dep.substr(0, dep_end + 1);
                manifest.dependencies.push_back(dep);
            } else {
                manifest.dependencies.push_back(line);
            }
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        auto ke = key.find_last_not_of(" \t");
        if (ke != std::string::npos) key = key.substr(0, ke + 1);
        auto vs = val.find_first_not_of(" \t");
        if (vs != std::string::npos) val = val.substr(vs);
        auto ve = val.find_last_not_of(" \t");
        if (ve != std::string::npos) val = val.substr(0, ve + 1);

        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }

        if (key == "name") manifest.name = val;
        else if (key == "version") manifest.version = val;
        else if (key == "description") manifest.description = val;
        else if (key == "author") manifest.author = val;
        else if (key == "license") manifest.license = val;
    }
    return true;
}

static bool write_havel_toml(const fs::path& path, const PackageManifest& m) {
    std::ofstream f(path);
    if (!f.is_open()) return false;

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
        for (const auto& dep : m.dependencies) {
            f << dep << " = \"*\"\n";
        }
    }
    return true;
}

// ============================================================================
// Commands
// ============================================================================

static int cmd_init(const std::vector<std::string>& args) {
    std::string pkg_name;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--name" && i + 1 < args.size()) pkg_name = args[++i];
        else if (pkg_name.empty() && args[i][0] != '-') pkg_name = args[i];
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
        f << "__cache__/\n";
        f << "*.hbc\n";
        std::cout << "created .gitignore" << std::endl;
    }

    std::cout << "\npackage '" << pkg_name << "' initialized" << std::endl;
    return 0;
}

static int cmd_install(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "hpkg install: specify a package path or name" << std::endl;
        return 1;
    }

    std::string pkgs_dir = get_packages_dir();
    if (pkgs_dir.empty()) return 1;

    fs::create_directories(pkgs_dir);

    for (const auto& arg : args) {
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

        fs::copy_file(toml, dest / "havel.toml",
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
          std::cerr << "hpkg: copy havel.toml failed: " << ec.message() << std::endl;
          return 1;
        }

        fs::path src_dir = source;
        if (fs::is_directory(source / manifest.name)) {
          src_dir = source / manifest.name;
        }

        for (const auto& entry : fs::directory_iterator(src_dir)) {
          if (!entry.is_regular_file()) continue;
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
            fs::copy_file(source, dest_file,
                fs::copy_options::overwrite_existing, ec);

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

static int cmd_list(const std::vector<std::string>& args) {
    std::string pkgs_dir = get_packages_dir();
    if (pkgs_dir.empty()) return 1;

    if (!fs::exists(pkgs_dir)) {
        std::cout << "no packages installed" << std::endl;
        return 0;
    }

    bool found = false;
    for (const auto& entry : fs::directory_iterator(pkgs_dir)) {
        if (!entry.is_directory()) continue;

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

static int cmd_publish(const std::vector<std::string>& args) {
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
  for (const auto& e : fs::directory_iterator(cwd)) {
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
        for (const auto& dep : manifest.dependencies) {
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
    for (const auto& f : hv_files) {
        std::cout << "  " << f << std::endl;
    }
    if (!manifest.dependencies.empty()) {
        std::cout << "dependencies:" << std::endl;
        for (const auto& d : manifest.dependencies) {
            std::cout << "  " << d << std::endl;
        }
    }
    std::cout << "\npackage is valid and ready for distribution" << std::endl;

    bool local_install = false;
    for (const auto& a : args) {
        if (a == "--local") local_install = true;
    }

    if (local_install) {
        std::vector<std::string> install_args = {cwd.string()};
        return cmd_install(install_args);
    }

    return 0;
}

static int cmd_remove(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "hpkg remove: specify a package name" << std::endl;
        return 1;
    }

    std::string pkgs_dir = get_packages_dir();
    if (pkgs_dir.empty()) return 1;

    for (const auto& name : args) {
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

static int cmd_info(const std::vector<std::string>& args) {
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
            for (const auto& d : manifest.dependencies)
                std::cout << "  - " << d << std::endl;
        }
        return 0;
    }

    std::string pkgs_dir = get_packages_dir();
    if (pkgs_dir.empty()) return 1;

    for (const auto& name : args) {
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
            std::cout << "  entry:    " << hv_file.string()
                      << " (" << fsize << " bytes)" << std::endl;
        }
    }
    return 0;
}

// ============================================================================
// Main
// ============================================================================

static void print_usage(const char* prog) {
    std::cerr <<
        "usage: hpkg <command> [args...]\n"
        "\n"
        "commands:\n"
        "  init [name]          create a new package in current directory\n"
        "  install <path|name>  install a package from a local directory or .hv file\n"
        "  list                 list installed packages\n"
        "  info [name]          show package info (current dir or named package)\n"
        "  publish [--local]    validate package (and optionally install locally)\n"
        "  remove <name>        remove an installed package\n"
        "\n"
        "package layout:\n"
  " my-pkg/\n"
  "   havel.toml          package manifest\n"
  "   my-pkg.hv           entry point\n"
  "   *.hv                additional sources\n"
        "\n"
        "installed packages live in ~/.havel/packages/<name>/\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string cmd = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (cmd == "init")              return cmd_init(args);
    else if (cmd == "install")      return cmd_install(args);
    else if (cmd == "list")         return cmd_list(args);
    else if (cmd == "info")         return cmd_info(args);
    else if (cmd == "publish")      return cmd_publish(args);
    else if (cmd == "remove")       return cmd_remove(args);
    else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_usage(argv[0]);
        return 0;
    }
    else {
        std::cerr << "hpkg: unknown command '" << cmd << "'" << std::endl;
        print_usage(argv[0]);
        return 1;
    }
}
