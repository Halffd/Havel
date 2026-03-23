// ModuleLoader.cpp - STUBBED (interpreter removed)
// Module loading requires Environment which was removed with interpreter

#include "ModuleLoader.hpp"

namespace havel {
namespace modules {

ModuleLoader::ModuleLoader() {
}

void ModuleLoader::addModulePath(const std::string &path) {
  (void)path;
}

void ModuleLoader::discoverModules() {
}

bool ModuleLoader::loadModule(const std::string &moduleName, Environment &env) {
  (void)moduleName;
  (void)env;
  return false;
}

void ModuleLoader::loadAllModules(Environment &env) {
  (void)env;
}

bool ModuleLoader::unloadModule(const std::string &moduleName) {
  (void)moduleName;
  return false;
}

void loadAllModules(Environment &env, std::shared_ptr<IHostAPI> hostAPI) {
  (void)env;
  (void)hostAPI;
}

void registerStdLibModules(ModuleLoader &loader) {
  (void)loader;
}

void loadStdLibModules(Environment &env, ModuleLoader &loader) {
  (void)env;
  (void)loader;
}

void registerHostModules(ModuleLoader &loader) {
  (void)loader;
}

void loadHostModules(Environment &env, ModuleLoader &loader,
                     std::shared_ptr<IHostAPI> hostAPI) {
  (void)env;
  (void)loader;
  (void)hostAPI;
}

} // namespace modules
} // namespace havel
