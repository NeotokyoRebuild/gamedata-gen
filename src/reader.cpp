#include "reader.hpp"

#define __LIBELF_INTERNAL__ 1

#include "gelf.h"
#include "libelf.h"

#define R_386_32 1

#include <cassert>
#include <cstdio>
#include <cstring>

#include <string>

#include <sys/mman.h>
#include <sys/stat.h>
#include <cxxabi.h>
#include <unistd.h>
#include <fcntl.h>

struct VTableFieldOffsetDataRaw
{
    uint64_t class_name_ptr;
    uint64_t member_name_ptr;
    uint64_t offset;
};

LargeNumber::LargeNumber() : high{}, low{}, isUnsigned{}
{

}

LargeNumber::operator unsigned long long() const
{
    return ((unsigned long long)high << 32) | low;
}

LargeNumber& LargeNumber::operator=(unsigned long long i)
{
    high = i >> 32;
    low = i & 0xFFFFFFFF;
    isUnsigned = true;
    return *this;
}

bool LargeNumber::operator!=(const LargeNumber& other) const
{
    return high != other.high && low != other.low && isUnsigned != other.isUnsigned;
}

std::ostream& operator<<(std::ostream& os, const LargeNumber& number)
{
    os << fmt::format("{:#018x}", static_cast<unsigned long long>(number));
    return os;
}

ProgramInfo process(char *image, std::size_t size)
{
    ProgramInfo programInfo = {};

    if (elf_version(EV_CURRENT) == EV_NONE)
    {
        programInfo.error = "Failed to init libelf.";
        return programInfo;
    }

    Elf *elf = elf_memory(image, size);
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

    size_t numberOfSections = 0;
    if (elf_getshdrnum(elf, &numberOfSections) != 0)
    {
        programInfo.error = "Failed to get number of ELF sections. (" + std::string(elf_errmsg(-1)) + ")";
        return programInfo;
    }

    size_t sectionNameStringTableIndex = 0;
    if (elf_getshdrstrndx(elf, &sectionNameStringTableIndex) != 0)
    {
        programInfo.error = "Failed to get ELF section names. (" + std::string(elf_errmsg(-1)) + ")";
        return programInfo;
    }

    Elf_Scn *relocationTableScn = nullptr;

    Elf_Scn *dynamicSymbolTableScn = nullptr;

    Elf_Scn *symbolTableScn = nullptr;

    size_t stringTableIndex = SHN_UNDEF;
    const Elf_Scn *stringTableScn = nullptr;

    size_t rodataIndex = SHN_UNDEF;
    Elf64_Addr rodataOffset = 0;
    Elf_Scn *rodataScn = nullptr;

    size_t relRodataIndex = SHN_UNDEF;
    Elf64_Addr relRodataOffset = 0;
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
        else if(elfSectionHeader.sh_type == SHT_PROGBITS && strcmp(name, ".member_offsets") == 0)
        {
            Elf_Data* data = elf_getdata(elfScn, nullptr);
            if (data && data->d_size > 0)
            {
                size_t entry_count = data->d_size / sizeof(VTableFieldOffsetDataRaw);
                auto entries = static_cast<const VTableFieldOffsetDataRaw*>(data->d_buf);

                for (size_t i = 0; i < entry_count; ++i)
                {
#if 0
                    std::cout << "VTableFieldOffsetData entry " << i << ":\n";
                    std::cout << "  Class name: " << &image[entries[i].class_name_ptr] << "\n";
                    std::cout << "  Member name: " << &image[entries[i].member_name_ptr] << "\n";
                    std::cout << "  Offset: " << entries[i].offset << " (0x" << std::hex << entries[i].offset << std::dec << ")\n";
#endif
                    programInfo.vtableFieldDataEntries.emplace_back(&image[entries[i].class_name_ptr], &image[entries[i].member_name_ptr], entries[i].offset);
                }
            }
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
            int relocationIndex = 0;
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
                    int symbolIndex = GELF_R_SYM(relocation.r_info);
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

