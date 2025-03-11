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
                // std::cerr << fmt::format("Warning: function {} has no linuxIndex value", function.name) << std::endl;
                continue;
            }

            if (!function.windowsIndex.has_value())
            {
                // std::cerr << fmt::format("Warning: function {} has no windowsIndex value", function.name) << std::endl;
                continue;
            }

            vtables[function.nameSpace].emplace(function.name, FunctionOffsets{function.linuxIndex.value(), function.windowsIndex.value()});
        }

        offsets.emplace(class_.name, vtables);
    }

    return offsets;
}

std::optional<int> getOffset(Offsets offsets, const std::string& symbol)
{
    auto functionNameStartPos = symbol.rfind("::");
    if (functionNameStartPos == std::string::npos)
    {
        std::cerr << fmt::format("Error: incorrect format of symbol {} (missing \'::\' separator)", symbol) << std::endl;
        return std::nullopt;
    }

    auto systemNameStartPos = symbol.rfind('.');
    if (systemNameStartPos == std::string::npos)
    {
        std::cerr << fmt::format("Error: incorrect format of symbol {} (missing \'.\' separator)", symbol) << std::endl;
        return std::nullopt;
    }

    auto namespaceStartPos = symbol.find("::");
    if (namespaceStartPos == std::string::npos)
    {
        std::cerr << fmt::format("Error: incorrect format of symbol {} (missing \'::\' separator)", symbol) << std::endl;
        return std::nullopt;
    }

    auto className = symbol.substr(0, namespaceStartPos);
    auto namespaceName = symbol.substr(namespaceStartPos + 2, functionNameStartPos - namespaceStartPos - 2);
    auto functionName = symbol.substr(functionNameStartPos + 2, systemNameStartPos - functionNameStartPos - 2);
    auto systemName = symbol.substr(systemNameStartPos + 1);

    auto classVTablesIterator = offsets.find(className);
    if (classVTablesIterator == offsets.end())
    {
        std::cerr << fmt::format("Error: failed to find class vtable by its name \'{}\')", className) << std::endl;
        return std::nullopt;
    }
    const auto& classVTables = classVTablesIterator->second;

    auto classNamespaceIterator = classVTables.find(namespaceName);
    if (classNamespaceIterator == classVTables.end())
    {
        std::cerr << fmt::format("Error: failed to find class namespace by its name \'{}\')", namespaceName) << std::endl;
        return std::nullopt;
    }
    const auto& classNamespace = classNamespaceIterator->second;

    auto functionIterator = classNamespace.find(functionName);
    if (functionIterator == classNamespace.end())
    {
        std::cerr << fmt::format("Error: failed to find function by its name \'{}\'", functionName) << std::endl;
        return std::nullopt;
    }
    const auto& function = functionIterator->second;

    auto isLinux = systemName == "linux";

    return isLinux ? function.linuxIndex : function.windowsIndex;
}

int writeGamedataFile(std::list<ClassInfo>& classes, const std::vector<std::filesystem::path> &inputFilePaths, const std::vector<std::filesystem::path>& outputDirectoryPaths)
{
    auto offsets = prepareOffsets(classes);

    auto outputDirectoryPathIterator = outputDirectoryPaths.begin();

    for (const auto& inputFilePath : inputFilePaths)
    {
        if (inputFilePath.empty())
        {
            std::cerr << "Error: input file name is empty" << std::endl;
            return EXIT_FAILURE;
        }

        constexpr auto inputFileExtensionString = ".in";
        auto inputFileExtension = inputFilePath.extension();

        if (inputFileExtension != inputFileExtensionString)
        {
            std::cerr << fmt::format("Error: input file {} doesn't contain correct file extension {}", inputFilePath.string(), inputFileExtension.string()) << std::endl;
            return EXIT_FAILURE;
        }

        std::ifstream inputStream(inputFilePath);
        if (!inputStream)
        {
            std::cerr << fmt::format("Error: input file {} open failed - {}", inputFilePath.string(), std::strerror(errno)) << std::endl;
            return EXIT_FAILURE;
        }

        auto outputFileName = inputFilePath.filename().stem();
        auto outputFileDir = *outputDirectoryPathIterator;

        std::filesystem::create_directory(outputFileDir);

        if (!std::filesystem::exists(outputFileDir))
        {
            std::cerr << fmt::format("Error: failed to create {} directory", outputFileDir.string(), std::strerror(errno)) << std::endl;
            return EXIT_FAILURE;
        }

        auto outputFile = outputFileDir / outputFileName;

        std::ofstream outputStream(outputFile);
        if (!outputStream)
        {
            std::cerr << fmt::format("Error: output file {} open failed - {}", outputFile.string(), std::strerror(errno)) << std::endl;
            return EXIT_FAILURE;
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
                    std::cerr << fmt::format("Error: input file {} contains only one \'#\' at line {}", inputFilePath.string(), lineNumber) << std::endl;
                    return EINVAL;
                }

                auto symbol = line.substr(startPos + 1, endPos - startPos - 1);
                if (symbol.empty())
                {
                    std::cerr << fmt::format("Error: symbol from input file {} at line {} is empty somehow", inputFilePath.string(), lineNumber) << std::endl;
                    return EINVAL;
                }

                auto offset = getOffset(offsets, symbol);
                if (!offset.has_value())
                {
                    std::cerr << fmt::format("Error: failed to get offset of symbol {} from input file {} at line {}", symbol, inputFilePath.string(), lineNumber) << std::endl;
                    return EINVAL;
                }

                line.replace(startPos, endPos - startPos + 1, std::to_string(offset.value()));
            }

            outputStream << line << std::endl; // write to os
        }

        if (auto next = std::next(outputDirectoryPathIterator); next != outputDirectoryPaths.end())
        {
            outputDirectoryPathIterator = next;
        }
    }

    return EXIT_SUCCESS;
}
