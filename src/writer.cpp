#include "writer.hpp"
#include "formatter.hpp"

#include <cstring>
#include <fstream>
#include <map>
#include <string>

struct FunctionOffsets
{
    int linuxIndex;
    int windowsIndex;
};

using ClassNamespace = std::map<std::string, FunctionOffsets>;
using ClassVTables = std::map<std::string, ClassNamespace>;
using Offsets = std::map<std::string, ClassVTables>;

Offsets prepareOffsets(std::list<ClassInfo>& classes)
{
    std::map<std::string, ClassVTables> offsets;

    for (const auto& class_ : classes)
    {
        ClassVTables vtables;
        ClassNamespace namespace_;

        auto vtable = formatVTable(class_);

        for (const auto& function : vtable)
        {
            if (!function.linuxIndex.has_value())
            {
                // std::cerr << std::format("Error: function {} has no linuxIndex value", function.name) << std::endl;
                continue;
            }

            if (!function.windowsIndex.has_value())
            {
                // std::cerr << std::format("Error: function {} has no windowsIndex value", function.name) << std::endl;
                continue;
            }

            vtables[function.nameSpace].emplace(function.shortName, FunctionOffsets{function.linuxIndex.value(), function.windowsIndex.value()});
        }

        offsets.emplace(class_.name, vtables);
    }

    return offsets;
}

std::optional<int> getOffset(Offsets offsets, const std::string& symbol)
{
    // CBaseEntity::CBaseEntity::EndTouch.linux

    auto functionNameStartPos = symbol.rfind("::");
    if (functionNameStartPos == std::string::npos)
    {
        std::cerr << std::format("Error: incorrect format of symbol {} (missing \'::\' separator)", symbol) << std::endl;
        return std::nullopt;
    }

    auto systemNameStartPos = symbol.rfind('.');
    if (systemNameStartPos == std::string::npos)
    {
        std::cerr << std::format("Error: incorrect format of symbol {} (missing \'.\' separator)", symbol) << std::endl;
        return std::nullopt;
    }

    auto namespaceStartPos = symbol.find("::");
    if (namespaceStartPos == std::string::npos)
    {
        std::cerr << std::format("Error: incorrect format of symbol {} (missing \'::\' separator)", symbol) << std::endl;
        return std::nullopt;
    }

    auto className = symbol.substr(0, namespaceStartPos);
    auto namespaceName = symbol.substr(namespaceStartPos + 2, functionNameStartPos - namespaceStartPos - 2);
    auto functionName = symbol.substr(functionNameStartPos + 2, systemNameStartPos - functionNameStartPos - 2);
    auto systemName = symbol.substr(systemNameStartPos + 1);

    auto classVTablesIterator = offsets.find(className);
    if (classVTablesIterator == offsets.end())
    {
        std::cerr << std::format("Error: failed to find class vtable by its name \'{}\')", className) << std::endl;
        return std::nullopt;
    }
    const auto& classVTables = classVTablesIterator->second;

    auto classNamespaceIterator = classVTables.find(namespaceName);
    if (classNamespaceIterator == classVTables.end())
    {
        std::cerr << std::format("Error: failed to find class namespace by its name \'{}\')", namespaceName) << std::endl;
        return std::nullopt;
    }
    const auto& classNamespace = classNamespaceIterator->second;

    auto functionIterator = classNamespace.find(functionName);
    if (functionIterator == classNamespace.end())
    {
        std::cerr << std::format("Error: failed to find function by its name \'{}\')", functionName) << std::endl;
        return std::nullopt;
    }
    const auto& function = functionIterator->second;

    auto isLinux = systemName == "linux";

    return isLinux ? function.linuxIndex : function.windowsIndex;
}

// TODO return type and values
int writeGamedataFile(std::list<ClassInfo>& classes, const std::vector<std::string>& inputFiles)
{
    auto offsets = prepareOffsets(classes);

    for (const auto& inputFile : inputFiles)
    {
        if (inputFile.empty())
        {
            std::cerr << "Error: input file name is empty" << std::endl;
            return EINVAL;
        }

        constexpr auto inputFileExtension = ".in";
        auto inputFileNameStartPos = inputFile.find(".in");
        if (inputFileNameStartPos == std::string::npos)
        {
            std::cerr << std::format("Error: input file name {} doesn't contain correct file extension {}", inputFile, inputFileExtension) << std::endl;
            return EINVAL;
        }

        std::ifstream inputStream(inputFile);
        if (!inputStream)
        {
            std::cerr << std::format("Error: input file {} open failed - {}", inputFile, std::strerror(errno)) << std::endl;
            return errno;
        }

        std::string outputFile = inputFile.substr(0, inputFileNameStartPos);

        std::ofstream outputStream(outputFile);
        if (!outputStream)
        {
            std::cerr << std::format("Error: output file {} open failed - {}", outputFile, std::strerror(errno)) << std::endl;
            return errno;
        }

        std::string line;
        auto lineNumber = 0u;
        while(getline(inputStream, line))
        {
            lineNumber++;

            auto startPos = line.find('#');
            if (startPos != std::string::npos)
            {
                auto endPos = line.rfind('#');
                if (endPos == startPos)
                {
                    std::cerr << std::format("Error: input file {} contains only one \'#\' at line {}", inputFile, lineNumber) << std::endl;
                    return EINVAL;
                }

                auto symbol = line.substr(startPos + 1, endPos - startPos - 1);
                if (symbol.empty())
                {
                    std::cerr << std::format("Error: symbol from input file {} at line {} is empty somehow", inputFile, lineNumber) << std::endl;
                    return EINVAL;
                }

                auto offset = getOffset(offsets, symbol);
                if (!offset.has_value())
                {
                    std::cerr << std::format("Error: failed to get offset of symbol {} from input file {} at line {}", symbol, inputFile, lineNumber) << std::endl;
                    return EINVAL;
                }

                line.replace(startPos, endPos - startPos + 1, std::to_string(offset.value()));
            }

            outputStream << line << std::endl; // write to os
        }
    }

    return 0;
}
