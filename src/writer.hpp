#pragma once

#include "parser.hpp"

#include <filesystem>
#include <list>

int writeGamedataFile(std::list<ClassInfo>& classes, const std::vector<std::filesystem::path> &inputFilePaths, const std::vector<std::filesystem::path> &outputDirectoryPaths);
