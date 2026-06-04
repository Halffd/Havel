#pragma once

struct HavelAPI;

extern "C" void havel_extension_init(HavelAPI *api);

void qt_toolkit_register_functions(void *api);
