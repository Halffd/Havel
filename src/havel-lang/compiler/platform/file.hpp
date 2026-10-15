#pragma once

/*
 * file.hpp - The filesystem gaps std::filesystem does not fill (PLAT-001
 * section 6).
 *
 * Deliberately NOT a reimplementation of the fs module. src/host/filesystem/
 * FileSystemService.cpp owns file and directory semantics and stays the
 * place that goes for those. What lives here is the small set of
 * primitives std::filesystem genuinely does not offer and that the tree
 * reaches for through POSIX APIs today:
 *
 *   readdir   -> FindFirstFileW / FindNextFileW
 *   mkdtemp   -> no Win32 equivalent; GetTempPathW + CreateDirectoryW
 *   /proc/self/exe -> GetModuleFileNameW
 *
 * Encoding: the tree is UTF-8 end to end (Havel source, std::string paths).
 * Win32 hands back UTF-16. Every function here converts on the boundary so
 * callers never handle wchar_t.
 */

#include "havel_platform.h"

#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#if HAVEL_PLATFORM_WINDOWS
#include "havel-lang/compiler/platform/platform_win.hpp"
#else
#include <dirent.h>
#include <unistd.h>
#endif

namespace havel::platform {

/** Path separator: '/' on POSIX, '\\' on Windows. */
inline constexpr char path_separator =
#if HAVEL_PLATFORM_WINDOWS
    '\\';
#else
    '/';
#endif

#if HAVEL_PLATFORM_WINDOWS

/** UTF-16 -> UTF-8. Returns an empty string if conversion fails. */
inline std::string wide_to_utf8(const std::wstring &w) {
  if (w.empty()) return {};
  const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                      static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        out.data(), n, nullptr, nullptr);
  return out;
}

/** UTF-8 -> UTF-16. Empty on failure. */
inline std::wstring utf8_to_wide(const std::string &s) {
  if (s.empty()) return {};
  const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), n);
  return out;
}

#endif  // HAVEL_PLATFORM_WINDOWS

/**
 * Entry names in a directory, excluding "." and "..", unsorted.
 *
 * Returns an empty vector when the directory cannot be opened or does not
 * exist - matching readdir, which returns nullptr for the same cases and is
 * how every existing caller already handles it.
 */
inline std::vector<std::string> list_directory(const std::string &path) {
  std::vector<std::string> names;
#if HAVEL_PLATFORM_WINDOWS
  const std::wstring wpath = utf8_to_wide(path);
  if (wpath.empty()) return names;

  WIN32_FIND_DATAW data{};
  // The search pattern has to be appended with the native separator; a bare
  // trailing '\' makes FindFirstFileW fail with ERROR_INVALID_NAME.
  std::wstring pattern = wpath;
  if (pattern.back() != L'\\' && pattern.back() != L'/') pattern += L'\\';
  pattern += L'*';

  HANDLE h = ::FindFirstFileW(pattern.c_str(), &data);
  if (h == INVALID_HANDLE_VALUE) return names;

  do {
    const std::wstring wname = data.cFileName;
    if (wname == L"." || wname == L"..") continue;
    names.push_back(wide_to_utf8(wname));
  } while (::FindNextFileW(h, &data));

  ::FindClose(h);
#else
  DIR *d = ::opendir(path.c_str());
  if (!d) return names;
  while (struct dirent *e = ::readdir(d)) {
    const std::string name(e->d_name);
    if (name == "." || name == "..") continue;
    names.push_back(name);
  }
  ::closedir(d);
#endif
  return names;
}

/**
 * Create a uniquely named directory and return its path.
 *
 * POSIX mkdtemp(mktemp-template) has no Win32 counterpart, so the template
 * is interpreted here as "a prefix"; the caller supplies one and gets back
 * a directory that did not previously exist. Empty on failure.
 *
 * The template may end in "XXXXXX", in which case those six characters are
 * the random suffix, exactly as on POSIX. Otherwise a numeric suffix is
 * appended.
 */
inline std::string make_temp_directory(const std::string &tmpl) {
#if HAVEL_PLATFORM_WINDOWS
  if (tmpl.empty()) return {};

  // Resolve the base directory from the template's leading path, defaulting
  // to the process temp dir.
  const size_t sep = tmpl.find_last_of("/\\");
  const std::string dir_part = (sep == std::string::npos) ? std::string() : tmpl.substr(0, sep);
  const std::string name_part = (sep == std::string::npos) ? tmpl : tmpl.substr(sep + 1);

  std::string base = dir_part;
  if (base.empty()) {
    wchar_t buf[MAX_PATH + 1];
    const DWORD n = ::GetTempPathW(MAX_PATH, buf);
    if (n == 0 || n > MAX_PATH) return {};
    base = wide_to_utf8(std::wstring(buf, n));
    // GetTempPathW ends with a separator; trim it so the join is uniform.
    if (!base.empty() && (base.back() == '\\' || base.back() == '/')) base.pop_back();
  }
  if (name_part.empty()) return {};

  const bool x6 = name_part.size() >= 6 &&
                  name_part.compare(name_part.size() - 6, 6, "XXXXXX") == 0;

  for (int attempt = 0; attempt < 256; ++attempt) {
    std::string candidate = base;
    candidate += path_separator;
    candidate += name_part;

    if (x6) {
      // Replace the trailing XXXXXX in place. GetTempFileNameW is avoided
      // because it creates a file, not a directory. rand() is not used: it
      // is deterministically seeded unless srand() was called, so two
      // processes started together would pick the same six characters.
      static thread_local std::mt19937 rng{std::random_device{}()};
      static const char kHex[] = "abcdefghijklmnopqrstuvwxyz0123456789";
      std::uniform_int_distribution<int> pick(0, (sizeof(kHex) - 1) - 1);
      for (int i = 0; i < 6; ++i) {
        candidate[base.size() + 1 + name_part.size() - 6 + i] = kHex[pick(rng)];
      }
    } else {
      candidate += std::to_string(attempt);
    }

    const std::wstring wcandidate = utf8_to_wide(candidate);
    if (wcandidate.empty()) return {};

    if (::CreateDirectoryW(wcandidate.c_str(), nullptr)) return candidate;
    if (::GetLastError() != ERROR_ALREADY_EXISTS) return {};
  }
  return {};
#else
  if (tmpl.empty()) return {};
  std::string buf = tmpl;
  // mkdtemp mutates its argument in place and returns it.
  const char *made = ::mkdtemp(buf.data());
  return made ? std::string(made) : std::string();
#endif
}

/**
 * Absolute path of the running executable, or empty if unavailable.
 *
 * The Windows answer can come back with "\\?\Device\\..." style prefixes on
 * some configurations; that is left intact because it is the only form
 * guaranteed to be usable for GetProcAddress-style binary identity checks.
 */
inline std::string executable_path() {
#if HAVEL_PLATFORM_WINDOWS
  std::vector<wchar_t> buf(MAX_PATH);
  for (;;) {
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(),
                                         static_cast<DWORD>(buf.size()));
    if (n == 0) return {};
    if (n < buf.size() - 1) {
      return wide_to_utf8(std::wstring(buf.data(), n));
    }
    if (buf.size() >= 32768) return {};  // MAX_PATH extended limit
    buf.resize(buf.size() * 2);
  }
#else
  std::vector<char> buf(1024);
  for (;;) {
    const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n < 0) {
      // Not Linux: fall back to argv[0] resolution, which every POSIX host
      // supports for the current working directory case.
      return {};
    }
    if (static_cast<size_t>(n) < buf.size() - 1) {
      return std::string(buf.data(), static_cast<size_t>(n));
    }
    if (buf.size() >= 65536) return {};
    buf.resize(buf.size() * 2);
  }
#endif
}

/** Directory containing the running executable, or empty. */
inline std::string executable_directory() {
  const std::string exe = executable_path();
  if (exe.empty()) return {};
  const size_t sep = exe.find_last_of("/\\");
  if (sep == std::string::npos) return {};
  return exe.substr(0, sep);
}

}  // namespace havel::platform
