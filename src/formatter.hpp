#include "parser.hpp"

#include <optional>
#include <string>
#include <vector>

bool shouldSkipWindowsFunction(const ClassInfo &classInfo, std::size_t vtableIndex, std::size_t functionIndex, const FunctionInfo &functionInfo);

// TODO rename
struct Out2
{
    LargeNumber id;
    std::string symbol; // CNEO_Player::CBaseEntity::EndTouch(CBaseEntity*)
    std::string name; // EndTouch(CBaseEntity*)
    std::string shortName; // EndTouch
    std::string nameSpace; // CBaseEntity
    bool isMulti;
    std::optional<int> linuxIndex;
    std::optional<int> windowsIndex;
};

std::vector<Out2> formatVTable(const ClassInfo &classInfo);