#include "core/version.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace tz::version
{

namespace
{

// "nxt-3.0.2" → 3, 0, 2; nothing without a digit
std::vector<uint64_t> numbers(std::string_view text)
{
    std::vector<uint64_t> out;
    const size_t first = text.find_first_of("0123456789");
    if (first == std::string_view::npos)
        return out;
    const char* at = text.data() + first;
    const char* end = text.data() + text.size();
    while (at < end)
    {
        uint64_t number = 0;
        const auto [next, error] = std::from_chars(at, end, number);
        if (error != std::errc())
            break;
        out.push_back(number);
        if (next >= end || *next != '.')
            break;
        at = next + 1;
    }
    return out;
}

uint32_t le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16)) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t le64(const uint8_t* p)
{
    return le32(p) | (static_cast<uint64_t>(le32(p + 4)) << 32);
}

bool readAt(FILE* file, long offset, void* out, size_t size)
{
    return std::fseek(file, offset, SEEK_SET) == 0 && std::fread(out, 1, size, file) == size;
}

} // namespace

bool newer(std::string_view candidate, std::string_view current)
{
    const auto a = numbers(candidate);
    const auto b = numbers(current);
    if (a.empty() || b.empty())
        return false;
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i)
    {
        const uint64_t x = i < a.size() ? a[i] : 0;
        const uint64_t y = i < b.size() ? b[i] : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

bool isNro(const char* path)
{
    char magic[4] = {};
    FILE* file = std::fopen(path, "rb");
    const bool read = file && readAt(file, 16, magic, 4);
    if (file)
        std::fclose(file);
    return read && std::memcmp(magic, "NRO0", 4) == 0;
}

std::string ofNro(const char* path)
{
    FILE* file = std::fopen(path, "rb");
    if (!file)
        return {};
    // the header's size of the program; after it the assets, their header saying where the NACP is; in the NACP the
    // display version, 16 bytes at 0x3060
    std::string version;
    uint8_t header[0x20];
    uint8_t assets[0x38];
    char text[0x10] = {};
    if (readAt(file, 0, header, sizeof(header)) && std::memcmp(header + 0x10, "NRO0", 4) == 0)
    {
        const long size = static_cast<long>(le32(header + 0x18));
        if (readAt(file, size, assets, sizeof(assets)) && std::memcmp(assets, "ASET", 4) == 0 &&
            le64(assets + 0x20) >= 0x3060 + sizeof(text) &&
            readAt(file, size + static_cast<long>(le64(assets + 0x18)) + 0x3060, text, sizeof(text)))
            version.assign(text, strnlen(text, sizeof(text)));
    }
    std::fclose(file);
    return version;
}

} // namespace tz::version
