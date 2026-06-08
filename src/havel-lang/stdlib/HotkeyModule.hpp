#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"
#include <string>
#include <vector>

namespace havel::stdlib {

class HotkeyModule {
public:
    static void install(havel::compiler::VMApi &api);

    static havel::compiler::Value
    createHotkeyContext(havel::compiler::VM *vm, const std::string &hotkeyId,
                        const std::string &alias, const std::string &key,
                        const std::string &condition, const std::string &info,
                        havel::compiler::CallbackId callback);

    static havel::compiler::Value
    rebuildHotkeyContext(havel::compiler::VM &vm, const std::string &hotkeyId);

    static void recordTrigger(const std::string &hotkeyId);
    static size_t contextCount();

    static std::string findByAlias(const std::string &alias);
    static std::vector<std::string> findByKey(const std::string &key);
    static std::vector<std::string> getAllIds();
    static bool removeById(const std::string &hotkeyId);
    static void resetTriggerCount(const std::string &hotkeyId);
    static int64_t getAge(const std::string &hotkeyId);
    static int64_t getElapsed(const std::string &hotkeyId);

    static std::string findConditionalById(int condId);
    static size_t conditionalCount();

    static void setGoroutineId(const std::string &hotkeyId, uint32_t gid);

    static std::string resolveUniqueId(const std::string &preferredId);
};

void registerHotkeyModule(const havel::compiler::VMApi &api);

} // namespace havel::stdlib
