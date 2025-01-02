#pragma once

#include <iostream>
#include <string>
#include <vector>

struct LargeNumber
{
    LargeNumber();

    operator unsigned long long() const;
    LargeNumber& operator=(unsigned long long i);
    bool operator!=(const LargeNumber& other) const;

    unsigned int high;
    unsigned int low;
    bool isUnsigned;
};

std::ostream& operator<<(std::ostream& os, const LargeNumber& number);

struct RodataChunk
{
    LargeNumber offset;
    std::vector<unsigned char> data;
};

struct SymbolInfo
{
    unsigned int section;
    LargeNumber address;
    LargeNumber size;
    std::string name;
};

struct RelocationInfo
{
    LargeNumber address;
    LargeNumber target;
};

struct ProgramInfo
{
    std::string error;
    int addressSize;
    unsigned int rodataIndex;
    LargeNumber rodataStart;
    std::vector<RodataChunk> rodataChunks;
    unsigned int relRodataIndex;
    LargeNumber relRodataStart;
    std::vector<RodataChunk> relRodataChunks;
    std::vector<SymbolInfo> symbols;
    std::vector<RelocationInfo> relocations;
};

ProgramInfo process(char *image, std::size_t size);
