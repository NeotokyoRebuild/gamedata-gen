#include <list>
#define __LIBELF_INTERNAL__ 1

#include "gelf.h"
#include "libelf.h"

#define R_386_32 1

#include <cxxabi.h>

#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct LargeNumber
{
    LargeNumber()
    {
        high = 0;
        low = 0;
        isUnsigned = false;
    }

    operator unsigned long long() const
    {
        return ((unsigned long long)high << 32) | low;
    }

    LargeNumber operator=(unsigned long long i)
    {
        high = i >> 32;
        low = i & 0xFFFFFFFF;
        isUnsigned = true;
        return *this;
    }

    // bool operator<(const LargeNumber& other) const
    // {
    //     return std::tie(high, low, isUnsigned) < std::tie(other.high, other.low, other.isUnsigned);
    // }

    bool operator!=(const LargeNumber& other) const
    {
        //return std::tie(high, low, isUnsigned) < std::tie(other.high, other.low, other.isUnsigned);
        return high != other.high && low != other.low && isUnsigned != other.isUnsigned;
    }

    unsigned int high;
    unsigned int low;
    bool isUnsigned;
};

std::ostream& operator<<(std::ostream& os, const LargeNumber& number)
{
    os << std::format("{:#018x}", static_cast<unsigned long long>(number));
    //os << std::format("{:#016}", static_cast<unsigned long long>(number));
    return os;
}

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

ProgramInfo process(const std::string& image)
{
    ProgramInfo programInfo = {};

    if (elf_version(EV_CURRENT) == EV_NONE)
    {
        programInfo.error = "Failed to init libelf.";
        return programInfo;
    }

    Elf *elf = elf_memory(const_cast<char *>(image.data()), image.size());
    if (!elf)
    {
        programInfo.error = "elf_begin failed. (" + std::string(elf_errmsg(-1)) + ")";
        return programInfo;
    }

    Elf_Kind elfKind = elf_kind(elf);
    if (elfKind != ELF_K_ELF)
    {
        programInfo.error = "Input is not an ELF object. (" + std::to_string(elfKind) + ")";
        return programInfo;
    }

    GElf_Ehdr elfHeader;
    if (gelf_getehdr(elf, &elfHeader) != &elfHeader)
    {
        programInfo.error = "Failed to get ELF header. (" + std::string(elf_errmsg(-1)) + ")";
        return programInfo;
    }

    switch (elfHeader.e_machine)
    {
    case EM_386:
        programInfo.addressSize = 4;
        break;
    case EM_X86_64:
        programInfo.addressSize = 8;
        break;
    default:
        programInfo.error = "Unsupported architecture. (" + std::to_string(elfHeader.e_machine) + ")";
        return programInfo;
    }

    size_t numberOfSections;
    if (elf_getshdrnum(elf, &numberOfSections) != 0)
    {
        programInfo.error = "Failed to get number of ELF sections. (" + std::string(elf_errmsg(-1)) + ")";
        return programInfo;
    }

    size_t sectionNameStringTableIndex;
    if (elf_getshdrstrndx(elf, &sectionNameStringTableIndex) != 0)
    {
        programInfo.error = "Failed to get ELF section names. (" + std::string(elf_errmsg(-1)) + ")";
        return programInfo;
    }

    Elf_Scn *relocationTableScn = nullptr;

    Elf_Scn *dynamicSymbolTableScn = nullptr;

    Elf_Scn *symbolTableScn = nullptr;

    size_t stringTableIndex = SHN_UNDEF;
    Elf_Scn *stringTableScn = nullptr;

    size_t rodataIndex = SHN_UNDEF;
    Elf64_Addr rodataOffset;
    Elf_Scn *rodataScn = nullptr;

    size_t relRodataIndex = SHN_UNDEF;
    Elf64_Addr relRodataOffset;
    Elf_Scn *relRodataScn = nullptr;

    for (size_t elfSectionIndex = 0; elfSectionIndex < numberOfSections; ++elfSectionIndex)
    {
        Elf_Scn *elfScn = elf_getscn(elf, elfSectionIndex);
        if (!elfScn)
        {
            programInfo.error = "Failed to get section " + std::to_string(elfSectionIndex) + ". (" + std::string(elf_errmsg(-1)) + ")";
            continue;
        }

        GElf_Shdr elfSectionHeader;
        if (gelf_getshdr(elfScn, &elfSectionHeader) != &elfSectionHeader)
        {
            programInfo.error = "Failed to get header for section " + std::to_string(elfSectionIndex) + ". (" + std::string(elf_errmsg(-1)) + ")";
            continue;
        }

        const char *name = elf_strptr(elf, sectionNameStringTableIndex, elfSectionHeader.sh_name);
        if (!name)
        {
            programInfo.error = "Failed to get name of section " + std::to_string(elfSectionIndex) + ". (" + std::string(elf_errmsg(-1)) + ")";
            continue;
        }

        if (elfSectionHeader.sh_type == SHT_REL && strcmp(name, ".rel.dyn") == 0)
        {
            relocationTableScn = elfScn;
        }
        else if (elfSectionHeader.sh_type == SHT_DYNSYM && strcmp(name, ".dynsym") == 0)
        {
            dynamicSymbolTableScn = elfScn;
        }
        else if (elfSectionHeader.sh_type == SHT_SYMTAB && strcmp(name, ".symtab") == 0)
        {
            symbolTableScn = elfScn;
        }
        else if (elfSectionHeader.sh_type == SHT_STRTAB && strcmp(name, ".strtab") == 0)
        {
            stringTableIndex = elfSectionIndex;
            stringTableScn = elfScn;
        }
        else if (elfSectionHeader.sh_type == SHT_PROGBITS && strcmp(name, ".rodata") == 0)
        {
            rodataIndex = elfSectionIndex;
            rodataOffset = elfSectionHeader.sh_addr;
            rodataScn = elfScn;
        }
        else if (elfSectionHeader.sh_type == SHT_PROGBITS && strcmp(name, ".data.rel.ro") == 0)
        {
            relRodataIndex = elfSectionIndex;
            relRodataOffset = elfSectionHeader.sh_addr;
            relRodataScn = elfScn;
        }

        if (relocationTableScn && dynamicSymbolTableScn && symbolTableScn && stringTableScn && rodataScn && relRodataScn)
        {
            break;
        }
    }

    if (!symbolTableScn || !stringTableScn || !rodataScn)
    {
        programInfo.error = "Failed to find all required ELF sections.";
        return programInfo;
    }

    programInfo.rodataStart = rodataOffset;
    programInfo.rodataIndex = rodataIndex;

    if (relocationTableScn && dynamicSymbolTableScn)
    {
        Elf_Data *relocationData = nullptr;
        while ((relocationData = elf_getdata(relocationTableScn, relocationData)) != nullptr)
        {
            size_t relocationIndex = 0;
            GElf_Rel relocation;
            while (gelf_getrel(relocationData, relocationIndex++, &relocation) == &relocation)
            {
                size_t type = GELF_R_TYPE(relocation.r_info);
                if (type != R_386_32)
                {
                    continue;
                }

                Elf_Data *symbolData = nullptr;
                while ((symbolData = elf_getdata(dynamicSymbolTableScn, symbolData)) != nullptr)
                {
                    GElf_Sym symbol;
                    size_t symbolIndex = GELF_R_SYM(relocation.r_info);
                    if (gelf_getsym(symbolData, symbolIndex, &symbol) != &symbol)
                    {
                        continue;
                    }

                    RelocationInfo relocationInfo;
                    relocationInfo.address = relocation.r_offset;
                    relocationInfo.target = symbol.st_value;
                    programInfo.relocations.push_back(std::move(relocationInfo));

                    break;
                }
            }
        }
    }

    Elf_Data *rodata = nullptr;
    while ((rodata = elf_getdata(rodataScn, rodata)) != nullptr)
    {
        RodataChunk rodataChunk;
        rodataChunk.offset = rodata->d_off;
        auto buffer = static_cast<char *>(rodata->d_buf);
        rodataChunk.data = std::vector<unsigned char>(buffer, buffer + rodata->d_size);
        programInfo.rodataChunks.push_back(std::move(rodataChunk));
    }

    if (relRodataScn)
    {
        programInfo.relRodataStart = relRodataOffset;
        programInfo.relRodataIndex = relRodataIndex;

        Elf_Data *relRodata = nullptr;
        while ((relRodata = elf_getdata(relRodataScn, relRodata)) != nullptr)
        {
            RodataChunk relRodataChunk;
            relRodataChunk.offset = relRodata->d_off;
            auto buffer = static_cast<char *>(relRodata->d_buf);
            relRodataChunk.data = std::vector<unsigned char>(buffer, buffer + relRodata->d_size);
            programInfo.relRodataChunks.push_back(std::move(relRodataChunk));
        }
    }

    Elf_Data *symbolData = nullptr;
    while ((symbolData = elf_getdata(symbolTableScn, symbolData)) != nullptr)
    {
        size_t symbolIndex = 0;
        GElf_Sym symbol;
        while (gelf_getsym(symbolData, symbolIndex++, &symbol) == &symbol)
        {
            const char *name = elf_strptr(elf, stringTableIndex, symbol.st_name);
            if (!name)
            {
                std::cerr << "Failed to symbol name for " + std::to_string(symbolIndex) + ". (" + std::string(elf_errmsg(-1)) + ")" << std::endl;
                continue;
            }

            SymbolInfo symbolInfo;
            symbolInfo.section = symbol.st_shndx;
            symbolInfo.address = symbol.st_value;
            symbolInfo.size = symbol.st_size;
            symbolInfo.name = name;
            programInfo.symbols.push_back(std::move(symbolInfo));
        }
    }

    elf_end(elf);
    return programInfo;
}

auto demangleSymbol(const char *abiName)
{
    int status;
    char *ret = abi::__cxa_demangle(abiName, 0 /* output buffer */, 0 /* length */, &status);

    auto deallocator = [](char *mem)
    {
        if (mem)
        {
            free(static_cast<void*>(mem));
        }
    };

    if (status)
    {
        // 0: The demangling operation succeeded.
        // -1: A memory allocation failure occurred.
        // -2: mangled_name is not a valid name under the C++ ABI mangling rules.
        // -3: One of the arguments is invalid.
        std::unique_ptr<char, decltype(deallocator)> retval(nullptr, deallocator);
        return retval;
    }

    std::unique_ptr<char, decltype(deallocator)> retval(ret, deallocator);
    return retval;
}

std::vector<unsigned char> getDataForSymbol(const ProgramInfo &programInfo, const SymbolInfo &symbol)
{
    LargeNumber dataStart;
    std::vector<RodataChunk> dataChunks;

    if (symbol.section == 0)
    {
        return {};
    }
    else if (symbol.section == programInfo.rodataIndex)
    {
        dataStart = programInfo.rodataStart;
        dataChunks = programInfo.rodataChunks;
    }
    else if (symbol.section == programInfo.relRodataIndex)
    {
        dataStart = programInfo.relRodataStart;
        dataChunks = programInfo.relRodataChunks;
    }
    else
    {
        return {};
    }

    if (dataStart.high != 0 || symbol.address.high != 0 || symbol.size.high != 0)
    {
        throw ">= 32-bit rodata is not supported";
    }

    for (const auto& chunk : dataChunks)
    {
        if (chunk.offset.high != 0)
        {
            throw ">= 32-bit rodata is not supported";
        }

        auto start = symbol.address.low - (dataStart.low + chunk.offset.low);
        auto end = start + symbol.size.low;

        if (start < 0 || end > chunk.data.size())
        {
            continue;
        }

        return {chunk.data.begin() + start, chunk.data.begin() + end};
    }

    return {};
}

struct ClassInfo;

struct FunctionInfo
{
    LargeNumber id;
    std::string name;
    SymbolInfo symbol;
    std::string shortName;
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

Out parse(ProgramInfo &programInfo)
{
    if (!programInfo.error.empty())
    {
        std::cerr << programInfo.error << std::endl;
        return {};
    }

    Out out{};

    std::vector<SymbolInfo> listOfVirtualClasses;
    std::map<LargeNumber, std::vector<SymbolInfo>> addressToSymbolMap;
    for (const auto& symbol : programInfo.symbols)
    {
        if (static_cast<unsigned long long>(symbol.address) == 0 || symbol.size == 0 || symbol.name.empty())
        {
            continue;
        }

        if (symbol.name.starts_with("_ZTV"))
        {
            listOfVirtualClasses.push_back(symbol);
        }

        addressToSymbolMap[symbol.address].push_back(symbol);
    }

    std::map<LargeNumber, LargeNumber> relocationMap;
    for (const auto& relocation : programInfo.relocations)
    {
        relocationMap[relocation.address] = relocation.target;
    }

    std::map<LargeNumber, FunctionInfo*> addressToFunctionMap;

    for (const auto& symbol : listOfVirtualClasses)
    {
        auto symbolDemangledName = std::string(demangleSymbol(symbol.name.c_str()).get() + 11);

        auto symbolData = getDataForSymbol(programInfo, symbol);
        if (symbolData.empty())
        {
            if (symbol.section != 0)
            {
                std::cout << "rVTable for " << symbolDemangledName << " is outside data" << std::endl;
            }

            continue;
        }

        auto& classInfo = out.classes.emplace_back();
        classInfo.id = symbol.address;
        classInfo.name = symbolDemangledName;
        classInfo.hasMissingFunctions = false;

        using DataViewType = std::uint32_t;
        const int BYTES_PER_ELEMENT = sizeof(DataViewType); // 4 from Uint32Array.BYTES_PER_ELEMENT
        auto symbolDataView = std::span(reinterpret_cast<DataViewType*>(symbolData.data()), symbolData.size() / BYTES_PER_ELEMENT);

        VTable *classVTable;

        for (DataViewType functionIndex = 0; functionIndex < symbolDataView.size(); ++functionIndex)
        {
            //std::cout << "[" << functionIndex << "] ";
            LargeNumber functionAddress;
            functionAddress.high = 0;
            functionAddress.low = symbolDataView[functionIndex];
            functionAddress.isUnsigned = true;

            if (programInfo.addressSize > BYTES_PER_ELEMENT)
            {
                functionAddress.high = symbolDataView[++functionIndex];
            }

            if (programInfo.addressSize == BYTES_PER_ELEMENT)
            {
                LargeNumber localAddress;
                localAddress.high = 0;
                localAddress.low = symbol.address.low + (functionIndex * BYTES_PER_ELEMENT);
                localAddress.isUnsigned = true;

                auto targetAddress = relocationMap[localAddress];
                if (targetAddress)
                {
                    functionAddress = targetAddress;
                }
            }
            else
            {
                std::cout << "Relocations not supported for 64-bit bins" << std::endl;
            }

            auto functionSymbolsIterator = addressToSymbolMap.find(functionAddress);

            auto addPureVirtualFunction = [&out, &classVTable, &classInfo]()
            {
                auto& pureVirtualFunction = out.functions.emplace_back();
                classInfo.hasMissingFunctions = true;
                pureVirtualFunction.name = "(pure virtual function)";

                classVTable->functions.push_back(&pureVirtualFunction);
            };

            // This could be the end of the vtable, or it could just be a pure/deleted func.
            if (functionSymbolsIterator == addressToSymbolMap.end())
            {
                if (classInfo.vtables.size() == 0 || static_cast<unsigned long long>(functionAddress) != 0)
                {
                    auto& newClassVTable = classInfo.vtables.emplace_back();
                    classVTable = &newClassVTable;
                    classVTable->offset = ~(functionAddress.low - 1);

                    // Skip the RTTI pointer and thisptr adjuster,
                    // We'll need to do more work here for virtual bases.
                    auto skip = programInfo.addressSize / BYTES_PER_ELEMENT;
                    functionIndex += skip;
                }
                else
                {
                    addPureVirtualFunction();
                }

                continue;
            }

            auto functionSymbols = functionSymbolsIterator->second;
            auto functionSymbol = functionSymbols.back();

            auto functionSymbolName = functionSymbol.name;
            if (functionSymbolName == "__cxa_deleted_virtual" || functionSymbolName == "__cxa_pure_virtual")
            {
                addPureVirtualFunction();
                continue;
            }

            FunctionInfo *functionInfoPtr;

            auto functionInfoIterator = addressToFunctionMap.find(functionAddress);
            if (functionInfoIterator != addressToFunctionMap.end())
            {
                auto& functionInfo = functionInfoIterator->second;
                functionInfoPtr = functionInfo;
            }
            else
            {
                auto functionDemangledName = demangleSymbol(functionSymbolName.c_str());
                auto functionName = std::string(functionDemangledName.get());

                auto functionShortName = functionName;

                auto startOfArgs = functionShortName.find('(');
                if (startOfArgs != std::string::npos)
                {
                    functionShortName = functionShortName.substr(0, startOfArgs);
                }

                auto startOfName = functionShortName.find("::");
                if (startOfName != std::string::npos)
                {
                    functionShortName = functionShortName.substr(startOfName + 2);
                }

                FunctionInfo functionInfo;

                functionInfo.id = functionAddress;
                functionInfo.name = functionName;
                functionInfo.symbol = functionSymbol;
                functionInfo.shortName = functionShortName;
                functionInfo.isThunk = false;
                functionInfo.isMulti = functionSymbols.size() > 1;
                functionInfo.classes.push_back(&classInfo);

                if (functionSymbol.name.starts_with("_ZTh"))
                {
                    functionInfo.isThunk = true;
                    functionInfo.name = functionName.substr(21); // remove "non-virtual thunk to" substring
                }

                out.functions.push_back(functionInfo);
                functionInfoPtr = &out.functions.back();

                addressToFunctionMap[functionAddress] = functionInfoPtr;
            }

            classVTable->functions.push_back(functionInfoPtr);
        }
    }

    return out;
};

bool shouldSkipWindowsFunction(const ClassInfo &classInfo, std::size_t vtableIndex, std::size_t functionIndex, const FunctionInfo &functionInfo)
{
    if (functionInfo.name.contains("::~"))
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

struct Out2
{
    LargeNumber id;
    std::string name;
    bool isMulti;
    std::optional<int> linuxIndex;
    std::optional<int> windowsIndex;
};

std::vector<Out2> formatVTable(const ClassInfo &classInfo)
{
    std::vector<Out2> vtable;

    std::size_t vtableIndex = 0;
    auto vtableInfo = classInfo.vtables.at(vtableIndex);

    int windowsIndex = 0;
    for (int linuxIndex = 0; linuxIndex < vtableInfo.functions.size(); ++linuxIndex)
    {
        auto functionInfo = vtableInfo.functions.at(linuxIndex);

        Out2 function;
        function.id = functionInfo->id;
        function.name = functionInfo->name;
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

                while ((linuxIndex + 1 + remainingOverloads) < vtableInfo.functions.size())
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

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <file-name>\n", argv[0]);
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    std::string image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    ProgramInfo programInfo = process(image);

    if (!programInfo.error.empty())
    {
        fprintf(stderr, "Failed to process input file '%s': %s.\n", argv[1], programInfo.error.c_str());
        return 1;
    }

#if 0
    fprintf(stdout, "address size: %d\n", programInfo.addressSize);
    fprintf(stdout, "rodata start: %08llx\n", (unsigned long long)programInfo.rodataStart);
    fprintf(stdout, "rodata chunks: %zu\n", programInfo.rodataChunks.size());
    for (const auto &chunk : programInfo.rodataChunks)
    {
        fprintf(stdout, "  offset: %08llx\n", (unsigned long long)chunk.offset);
        fprintf(stdout, "    size: %zu\n", chunk.data.size());
    }

    fprintf(stdout, "symbols: %zu\n", programInfo.symbols.size());
    for (const auto &symbol : programInfo.symbols)
    {
        if (static_cast<unsigned long long>(symbol.address) == 0 || symbol.size == 0 || symbol.name.empty())
        {
            continue;
        }
        fprintf(stdout, "  offset: %08llx\n", (unsigned long long)symbol.address);
        fprintf(stdout, "    size: %llu\n", (unsigned long long)symbol.size);
        fprintf(stdout, "    name: %s\n", demangleSymbol(symbol.name.c_str()).get());
    }
#endif

    auto out = parse(programInfo);

#if 0
    for (const auto& outClass : out.classes)
    {
        std::cout << outClass.id << " " << outClass.name << std::endl;

        for (const auto& vtable : outClass.vtables)
        {
            std::cout << "  vtable.offset=" << vtable.offset << std::endl;

            for (const auto& function : vtable.functions)
            {
                std::string shortName = function->shortName.empty() ? "" : "(" + function->shortName + ")";
                std::cout << "    function.id=" << function->id << " " << function->name << " " << shortName << std::endl;
            }
        }
    }
#endif

#if 1
    for (const auto& outClass : out.classes)
    {
        auto functions = formatVTable(outClass);

        std::cout << "L W " << outClass.name << std::endl;
        for (const auto& function : functions)
        {
            std::string linuxIndex = " ";
            if (function.linuxIndex.has_value())
            {
                linuxIndex = std::to_string(function.linuxIndex.value());
            }

            std::string windowsIndex = " ";
            if (function.windowsIndex.has_value())
            {
                windowsIndex = std::to_string(function.windowsIndex.value());
            }

            std::cout << linuxIndex << " " << windowsIndex << " " << function.name << (function.isMulti ? " [Multi]" : "") << std::endl;
        }
    }
#endif

    return 0;
}
