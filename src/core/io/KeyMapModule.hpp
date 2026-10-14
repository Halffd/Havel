#pragma once
// Loads the key table from the kymap Havel module into KeyMap.
//
// Kept in its own translation unit because KeyMap.hpp includes <X11/X.h>,
// whose `None` macro collides with identifiers of the same name in the VM
// headers. This loader needs the VM but none of the X11 surface.

namespace havel {

namespace compiler { class VM; }

// Pull modules/app/kymap.hv through `vm` and install its rows into KeyMap.
// Runs on the VM thread during startup, before any IO lookup can occur.
void loadKeyMapFromHavelModule(compiler::VM &vm);

} // namespace havel