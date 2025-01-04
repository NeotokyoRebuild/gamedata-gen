#pragma once

#include "reader.hpp"

#include <functional>
#include <list>
#include <memory>
#include <span>
#include <string>
#include <vector>

using DemangledSymbolDeallocator = std::function<void(char *mem)>;
std::unique_ptr<char, DemangledSymbolDeallocator> demangleSymbol(const char *abiName);

std::span<const unsigned char> getDataForSymbol(const ProgramInfo &programInfo, const SymbolInfo &symbol);

struct ClassInfo;

struct FunctionInfo
{
    LargeNumber id;
    std::string name;
    std::string shortName;
    std::string nameSpace;
    SymbolInfo symbol;
    bool isThunk;
    bool isMulti;
    std::vector<ClassInfo*> classes;
};

struct VTable
{
    LargeNumber offset;
    std::vector<FunctionInfo*> functions;
};

struct ClassInfo
{
    LargeNumber id;
    std::string name;
    std::vector<VTable> vtables;
    bool hasMissingFunctions;
};

struct Out
{
    std::list<ClassInfo> classes;
    std::list<FunctionInfo> functions;
};

Out parse(ProgramInfo &programInfo);
