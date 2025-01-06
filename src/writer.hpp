#pragma once

#include "parser.hpp"

#include <filesystem>
#include <list>

int writeGamedataFile(std::list<ClassInfo>& classes, const std::vector<std::filesystem::path> &inputFiles, const std::filesystem::path &outputPath);
