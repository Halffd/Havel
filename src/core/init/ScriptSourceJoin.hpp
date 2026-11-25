// Joining script files into one compile unit.
//
// The bytecode cache keys an entry by the compile unit's PATH hash, and the
// serve path (loadCachedScriptChunk) validates an entry against the exact
// text the request would compile. For a single file that text must be the
// file's content VERBATIM: any byte the join adds - a trailing newline -
// changes the text, so the entry the compile wrote describes an identity no
// reader asks about. The entry was rejected on the next load
// ("source-size-differs"), rewritten, and the cache never served: every
// launch recompiled.
//
// The newline is a SEPARATOR between files, so it belongs BETWEEN them and
// not after the last one.
//
// This header holds the rule in one place so the launcher and the test
// cannot drift apart.

#ifndef HAVEL_CORE_INIT_SCRIPTSOURCEJOIN_HPP
#define HAVEL_CORE_INIT_SCRIPTSOURCEJOIN_HPP

#include <string>
#include <vector>

namespace havel::init {

// Join already-read file contents into one compile unit.
//
// - A single file is returned VERBATIM.
// - Several files are separated by exactly one newline between each pair,
//   and the last file is not followed by one.
//
// `contents` must be in the same order as the files they came from; an
// empty entry is joined as an empty file (the caller decides whether that
// is an error).
inline std::string joinScriptContents(
    const std::vector<std::string> &contents) {
  std::string joined;
  for (size_t i = 0; i < contents.size(); ++i) {
    if (i > 0)
      joined += "\n";
    joined += contents[i];
  }
  return joined;
}

} // namespace havel::init

#endif // HAVEL_CORE_INIT_SCRIPTSOURCEJOIN_HPP
