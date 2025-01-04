#pragma once

#include "parser.hpp"

#include <list>

int writeGamedataFile(std::list<ClassInfo>& classes, const std::vector<std::string>& inputFiles);
