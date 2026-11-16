// make -C desktop test: versions compared as numbers, and an .nro's own read from its NACP.

#include "core/version.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

void put32(std::vector<uint8_t>& out, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

void put64(std::vector<uint8_t>& out, size_t at, uint64_t v)
{
    put32(out, at, static_cast<uint32_t>(v));
    put32(out, at + 4, static_cast<uint32_t>(v >> 32));
}

// An .nro as hbmenu reads one: "NRO0" and the program's size in its header, then the assets, their NACP with version.
void writeNro(const char* path, const char* version)
{
    const size_t program = 0x100, nacp = 0x40;
    std::vector<uint8_t> nro(program + nacp + 0x4000);
    std::memcpy(&nro[0x10], "NRO0", 4);
    put32(nro, 0x18, program);
    std::memcpy(&nro[program], "ASET", 4);
    put64(nro, program + 0x18, nacp);
    put64(nro, program + 0x20, 0x4000);
    std::memcpy(&nro[program + nacp + 0x3060], version, std::strlen(version));
    FILE* file = std::fopen(path, "wb");
    std::fwrite(nro.data(), 1, nro.size(), file);
    std::fclose(file);
}

} // namespace

int main()
{
    using tz::version::newer;
    assert(newer("4.1.0", "4.0.0"));
    assert(!newer("4.0.0", "4.0.0"));
    assert(!newer("3.3.1", "4.0.0"));
    assert(newer("3.0.10", "3.0.9"));
    assert(newer("4.0.1", "4.0"));
    assert(!newer("4.0", "4.0.0"));
    assert(!newer("nxt-3.0.2", "3.0.2"));
    assert(newer("nxt-3.0.2", "2.8.3"));
    assert(newer("v4.8.3", "2.9"));
    // the desktop build's version, and nothing to compare
    assert(!newer("3.3.1", "4.0.0-desktop"));
    assert(!newer("latest", "4.0.0"));
    assert(!newer("4.0.0", ""));

    using tz::version::isNro;
    using tz::version::ofNro;
    const char* path = "/tmp/themezer-version-test.nro";
    writeNro(path, "3.0.2");
    assert(isNro(path));
    assert(ofNro(path) == "3.0.2");
    writeNro(path, "123456789012345"); // 15 of the field's 16
    assert(ofNro(path) == "123456789012345");
    FILE* file = std::fopen(path, "wb");
    std::fputs("<html>Not Found</html>", file);
    std::fclose(file);
    assert(!isNro(path));
    assert(ofNro(path).empty());
    assert(ofNro("/tmp/themezer-version-test-missing.nro").empty());
    std::remove(path);

    std::printf("version: all good\n");
    return 0;
}
