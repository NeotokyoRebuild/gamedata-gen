#include "formatter.hpp"

#include <algorithm>

bool shouldSkipWindowsFunction(const ClassInfo &classInfo, std::size_t vtableIndex, std::size_t functionIndex, const FunctionInfo &functionInfo)
{
    if (functionInfo.name.starts_with('~'))
    {
        return functionIndex > 0 && functionInfo.name == classInfo.vtables.at(vtableIndex).functions.at(functionIndex - 1)->name;
    }

    for (std::size_t n = 0; n < classInfo.vtables.size(); n++)
    {
        if (n > vtableIndex)
        {
            auto functions = classInfo.vtables.at(n).functions;
            auto it = std::find_if(functions.begin(), functions.end(), [&functionInfo](const FunctionInfo *d)
            {
                return d->isThunk && d->name == functionInfo.name;
            });

            if (it != functions.end())
            {
                return true;
            }
        }
    }

    return false;
}

std::vector<Out2> formatVTable(const ClassInfo &classInfo)
{
    std::vector<Out2> vtable;

    std::size_t vtableIndex = 0;
    auto vtableInfo = classInfo.vtables.at(vtableIndex);

    int windowsIndex = 0;
    for (int linuxIndex = 0; linuxIndex < static_cast<int>(vtableInfo.functions.size()); ++linuxIndex)
    {
        auto functionInfo = vtableInfo.functions.at(linuxIndex);

        Out2 function;
        function.id = functionInfo->id;
        function.symbol = functionInfo->demangledSymbol;
        function.name = functionInfo->name;
        function.shortName = functionInfo->shortName;
        function.nameSpace = functionInfo->nameSpace;
        function.isMulti = functionInfo->isMulti;

        auto displayWindowsIndex = windowsIndex;
        if (shouldSkipWindowsFunction(classInfo, vtableIndex, linuxIndex, *functionInfo))
        {
            function.windowsIndex = std::nullopt;
        }
        else
        {
            if (!functionInfo->symbol.name.empty() && !functionInfo->isMulti)
            {
                int previousOverloads = 0;
                int remainingOverloads = 0;

                while ((linuxIndex - (1 + previousOverloads)) >= 0)
                {
                    auto previousFunctionIndex = linuxIndex - (1 + previousOverloads);
                    auto previousFunctionInfo = vtableInfo.functions.at(previousFunctionIndex);

                    if (functionInfo->symbol.name.empty() || shouldSkipWindowsFunction(classInfo, vtableIndex, previousFunctionIndex, *previousFunctionInfo))
                    {
                        break;
                    }

                    if (functionInfo->shortName != previousFunctionInfo->shortName)
                    {
                        break;
                    }

                    previousOverloads++;
                }

                while ((linuxIndex + 1 + remainingOverloads) < static_cast<int>(vtableInfo.functions.size()))
                {
                    auto nextFunctionIndex = linuxIndex + 1 + remainingOverloads;
                    auto nextFunctionInfo = vtableInfo.functions.at(nextFunctionIndex);

                    if (functionInfo->symbol.name.empty() || shouldSkipWindowsFunction(classInfo, vtableIndex, nextFunctionIndex, *nextFunctionInfo))
                    {
                        break;
                    }

                    if (functionInfo->shortName != nextFunctionInfo->shortName)
                    {
                        break;
                    }

                    remainingOverloads++;
                }

                displayWindowsIndex -= previousOverloads;
                displayWindowsIndex += remainingOverloads;
            }

            windowsIndex++;

            function.windowsIndex = displayWindowsIndex;
        }

        function.linuxIndex = linuxIndex;
        vtable.push_back(function);
    }

    return vtable;
}
