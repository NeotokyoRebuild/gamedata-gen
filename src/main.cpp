#include "reader.hpp"
#include "parser.hpp"
#include "formatter.hpp"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <cassert>
#include <cstring>

class mmapReader
{
public:
    explicit mmapReader(const std::string& path)
    {
        auto fd = ::open(path.c_str(), O_RDONLY);
        if(fd == -1)
        {
            throw std::runtime_error(std::format("Failed to open file \"{}\": {} (errno={}) ", path, strerror(errno), errno));
        }

        struct stat sb {};
        if(fstat(fd, &sb) == -1)
        {
            throw std::runtime_error(std::format("stat failed for file \"{}\": {} (errno={}) ", path, strerror(errno), errno));
        }

        auto file_size = static_cast<std::size_t>(sb.st_size);

        auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        std::size_t remainder = file_size % page_size;

        auto mem_size = file_size;
        if(remainder != 0)
        {
            mem_size += page_size - remainder;
        }

        auto data = mmap(nullptr, mem_size, PROT_READ, MAP_PRIVATE, fd, 0);

        if(file_size != 0)
        {
            if(data == MAP_FAILED)
            {
                throw std::runtime_error(std::format("mmap failed for file \"{}\": {} (errno={}) ", path, strerror(errno), errno));
            }
        }

        m_data = static_cast<char*>(data);
        m_fd = fd;
        m_file_size = file_size;
        m_mem_size = mem_size;
    }

    ~mmapReader()
    {
        assert(m_fd != -1);
        assert(m_data);

        if(m_mem_size != 0)
        {
            auto res = madvise(reinterpret_cast<void*>(m_data), m_mem_size, MADV_DONTNEED | MADV_FREE);
            if(res == -1)
            {
                std::cout << std::format("madvise failed: {} (errno={}) ", strerror(errno), errno);
            }

            res = munmap(reinterpret_cast<void*>(m_data), m_mem_size);
            if(res != 0)
            {
                std::cout << std::format("munmap failed with result {}: {} (errno={}) ", res, strerror(errno), errno);
            }
        }

        auto res = posix_fadvise(m_fd, 0, static_cast<off_t>(m_file_size), POSIX_FADV_DONTNEED); //POSIX_FADV_NOREUSE
        if(res != 0)
        {
            std::cout << std::format("posix_fadvise failed: {} (errno={}) ", strerror(errno), errno);
        }

        res = ::close(m_fd);
        if(res == -1)
        {
            std::cout << std::format("Failed to close file descriptor {}: {} (errno={}) ", m_fd, strerror(errno), errno);
        }
    }

    char *data()
    {
        return m_data;
    }

    std::size_t size()
    {
        return m_file_size;
    }

private:
    int m_fd{-1};
    char *m_data{};
    std::size_t m_file_size{0};
    std::size_t m_mem_size{0};
};

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <file-name>\n", argv[0]);
        return 1;
    }

#if 0
    std::ifstream file(argv[1], std::ios::binary);
    std::string image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto program = image.data();
    auto size = image.size();
#else
    mmapReader reader(argv[1]);
    auto program = reader.data();
    auto size = reader.size();
#endif

    ProgramInfo programInfo = process(program, size);

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
