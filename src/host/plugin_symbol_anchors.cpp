// Plugin symbol anchors.
//
// Runtime-loaded plugins (modules/*.so, toolkits/*.so) resolve havel::
// symbols from the host executable's dynamic symbol table (--export-dynamic
// + ENABLE_EXPORTS). Static archives only pull a member when an
// ALREADY-INCLUDED object references it — a class whose only consumers are
// plugins is never pulled, so its symbols do not exist in the process image
// no matter what visibility says (observed: AudioService carried
// HAVEL_EXPORT since 2026-09, was compiled with default visibility, and was
// still undefined at dlopen because nothing in the executable referenced
// it). Each anchor below takes the address of one plugin-facing symbol,
// which forces the defining archive member to link and, via HAVEL_EXPORT on
// the class, exports the whole class through the dynamic symbol table.
//
// When adding a host class whose ONLY consumer is a plugin: give the class
// HAVEL_EXPORT in its header and add an anchor here. The audit script
// (scripts/ci/plugin-symbol-audit.sh) fails CI when a plugin needs a symbol
// the host never exports.

#include "host/audio/AudioService.hpp"
#include "core/automation/PixelAutomation.hpp"

namespace havel::plugin_anchors {

// Never called: exists only so the references below survive LTO and keep
// their archive members linked. Member-pointer arrays carry relocations to
// the function symbols, which is what forces the pull.
[[gnu::used]] void (host::AudioService::*const kAudioServiceAnchors[])() = {
    &host::AudioService::toggleMute,
};
[[gnu::used]] bool (host::AudioService::*const kAudioServiceAnchorsBool[])() = {
    &host::AudioService::playTestSound,
};

// PixelAutomation.cpp.o: the pixel module plugin resolves Color::fromHex.
[[gnu::used]] Color (*const kColorAnchors[])(const std::string &) = {
    &Color::fromHex,
};

} // namespace havel::plugin_anchors
