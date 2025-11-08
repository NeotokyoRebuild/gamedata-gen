#pragma once

#include "parser.hpp"

#include <filesystem>
#include <list>

int writeGamedataFile(
    const std::list<ClassInfo>& classes,
    const std::vector<MemberOffset>& memberOffsets,
    const std::vector<std::filesystem::path>& inputFilePaths,
    const std::vector<std::filesystem::path>& outputDirectoryPaths);
