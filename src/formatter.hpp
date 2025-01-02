#include "parser.hpp"

#include <optional>
#include <string>
#include <vector>

bool shouldSkipWindowsFunction(const ClassInfo &classInfo, std::size_t vtableIndex, std::size_t functionIndex, const FunctionInfo &functionInfo);

struct Out2
{
    LargeNumber id;
    std::string name;
    bool isMulti;
    std::optional<int> linuxIndex;
    std::optional<int> windowsIndex;
};

std::vector<Out2> formatVTable(const ClassInfo &classInfo);