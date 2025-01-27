#include "parser.hpp"

#include <cxxabi.h>

#include <map>
#include <memory>

std::unique_ptr<char, DemangledSymbolDeallocator> demangleSymbol(const char *abiName)
{
    int status;
    char *ret = abi::__cxa_demangle(abiName, 0, 0, &status);

    DemangledSymbolDeallocator deallocator = [](char *mem)
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

    return {ret, deallocator};
}

std::span<const unsigned char> getDataForSymbol(const ProgramInfo &programInfo, const SymbolInfo &symbol)
{
    LargeNumber dataStart;
    const std::vector<RodataChunk> *dataChunks;

    if (symbol.section == 0)
    {
        return {};
    }
    else if (symbol.section == programInfo.rodataIndex)
    {
        dataStart = programInfo.rodataStart;
        dataChunks = &programInfo.rodataChunks;
    }
    else if (symbol.section == programInfo.relRodataIndex)
    {
        dataStart = programInfo.relRodataStart;
        dataChunks = &programInfo.relRodataChunks;
    }
    else
    {
        return {};
    }

    if (dataStart.high != 0 || symbol.address.high != 0 || symbol.size.high != 0)
    {
        throw std::runtime_error(">= 32-bit rodata is not supported");
    }

    for (const auto& chunk : *dataChunks)
    {
        if (chunk.offset.high != 0)
        {
            throw std::runtime_error(">= 32-bit rodata is not supported");
        }

        int start = symbol.address.low - (dataStart.low + chunk.offset.low);
        int end = start + symbol.size.low;

        if (start < 0 || end > static_cast<int>(chunk.data.size()))
        {
            continue;
        }

        return {chunk.data.begin() + start, chunk.data.begin() + end};
    }

    return {};
}

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
        auto symbolDataView = std::span(reinterpret_cast<const DataViewType*>(symbolData.data()), symbolData.size() / BYTES_PER_ELEMENT);

        VTable *classVTable{};

        for (DataViewType functionIndex = 0; functionIndex < symbolDataView.size(); ++functionIndex)
        {
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
                const auto& functionInfo = functionInfoIterator->second;
                functionInfoPtr = functionInfo;
            }
            else
            {
                auto demangledSymbolPtr = demangleSymbol(functionSymbolName.c_str());
                auto demangledSymbol = std::string(demangledSymbolPtr.get());
                std::string name = demangledSymbol;
                std::string shortName = demangledSymbol;
                std::string nameSpace = demangledSymbol;

                auto startOfName = demangledSymbol.rfind("::");
                if (startOfName != std::string::npos)
                {
                    name = name.substr(startOfName + 2);
                }

                auto startOfArgs = demangledSymbol.rfind('(');
                if (startOfArgs != std::string::npos)
                {
                    shortName = shortName.substr(startOfName + 2, startOfArgs - startOfName - 2);
                }


                nameSpace = nameSpace.substr(0, startOfName);

                FunctionInfo functionInfo;

                functionInfo.id = functionAddress;
                functionInfo.symbol = functionSymbol;
                functionInfo.demangledSymbol = demangledSymbol;
                functionInfo.name = name;
                functionInfo.shortName = shortName;
                functionInfo.nameSpace = nameSpace;
                functionInfo.isThunk = false;
                functionInfo.isMulti = functionSymbols.size() > 1;
                functionInfo.classes.push_back(&classInfo);

                if (functionSymbol.name.starts_with("_ZTh"))
                {
                    functionInfo.isThunk = true;
                    functionInfo.name = demangledSymbol.substr(21); // remove "non-virtual thunk to" substring
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
