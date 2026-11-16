#include "core/archive.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>

namespace tz::archive
{

namespace
{

// size cap for an archive read whole (a SARC, or what a Yaz0 holds): a theme's is a few MB
constexpr size_t kMaxSize = 16 * 1024 * 1024;

std::string hex(const uint8_t* bytes, size_t size)
{
    std::string out;
    constexpr char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < size; ++i)
    {
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 15];
    }
    return out;
}

uint16_t le16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16)) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | static_cast<uint32_t>(p[1] << 16 | p[2] << 8 | p[3]);
}

bool sameName(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

struct FileCloser
{
    void operator()(FILE* file) const { std::fclose(file); }
};
using File = std::unique_ptr<FILE, FileCloser>;

bool readAt(FILE* file, long offset, void* out, size_t size)
{
    return std::fseek(file, offset, SEEK_SET) == 0 && std::fread(out, 1, size, file) == size;
}

std::expected<std::vector<uint8_t>, std::string> unpack(int method, std::vector<uint8_t> data, size_t size)
{
    if (method == 0)
    {
        if (data.size() != size) return std::unexpected("stored zip entry size mismatch");
        return data;
    }
    if (method != 8)
        return std::unexpected("unknown zip compression method " + std::to_string(method));
    std::vector<uint8_t> out(size);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return std::unexpected("corrupt zip deflate data");
    stream.next_in = data.data();
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END || stream.total_out != size)
        return std::unexpected("corrupt zip deflate data");
    return out;
}

// --- zip: the central directory at the end names each file and says where it is.

std::expected<std::vector<uint8_t>, std::string> zipFile(FILE* file, long fileSize, std::string_view name, size_t maxSize)
{
    // the end of the central directory record: 22 bytes, then a comment of up to 64 KiB
    const long tail = std::min<long>(fileSize, 22 + 0xFFFF);
    std::vector<uint8_t> end(static_cast<size_t>(tail));
    if (!readAt(file, fileSize - tail, end.data(), end.size()))
        return std::unexpected("could not read archive bytes");
    long record = -1;
    for (long i = tail - 22; i >= 0; --i)
    {
        if (le32(&end[static_cast<size_t>(i)]) == 0x06054b50)
        {
            record = i;
            break;
        }
    }
    if (record < 0)
    {
        uint8_t first[4]{};
        const size_t count = std::min<size_t>(fileSize, sizeof(first));
        readAt(file, 0, first, count);
        return std::unexpected("no end-of-central-directory record in " + std::to_string(fileSize) +
                               " bytes starting " + hex(first, count));
    }
    const uint8_t* eocd = &end[static_cast<size_t>(record)];
    const uint16_t entries = le16(eocd + 10);
    const uint32_t directorySize = le32(eocd + 12);
    const uint32_t directoryOffset = le32(eocd + 16);
    if (directorySize > kMaxSize ||
        static_cast<uint64_t>(directoryOffset) + directorySize > static_cast<uint64_t>(fileSize))
        return std::unexpected("invalid zip directory");
    std::vector<uint8_t> directory(directorySize);
    if (!readAt(file, static_cast<long>(directoryOffset), directory.data(), directory.size()))
        return std::unexpected("could not read archive bytes");

    size_t at = 0;
    for (uint16_t i = 0; i < entries && at + 46 <= directory.size(); ++i)
    {
        const uint8_t* entry = &directory[at];
        if (le32(entry) != 0x02014b50)
            return std::unexpected("invalid zip directory");
        const uint16_t method = le16(entry + 10);
        const uint32_t packed = le32(entry + 20);
        const uint32_t size = le32(entry + 24);
        const uint16_t nameLength = le16(entry + 28);
        const uint16_t extraLength = le16(entry + 30);
        const uint16_t commentLength = le16(entry + 32);
        const uint32_t localOffset = le32(entry + 42);
        if (at + 46 + nameLength > directory.size())
            return std::unexpected("invalid zip directory");
        const std::string_view entryName(reinterpret_cast<const char*>(entry + 46), nameLength);
        at += 46 + nameLength + extraLength + commentLength;
        if (!sameName(entryName, name))
            continue;

        // the local header has its own name and extra field lengths, before the data
        uint8_t local[30]{};
        if (size > maxSize || packed > maxSize)
            return std::unexpected("zip entry exceeds size limit: " + std::string(entryName));
        if (!readAt(file, static_cast<long>(localOffset), local, sizeof(local)) || le32(local) != 0x04034b50)
            return std::unexpected("no local header at " + std::to_string(localOffset) + " of " +
                                   std::to_string(fileSize) + " bytes, found " + hex(local, 4));
        const long data = static_cast<long>(localOffset) + 30 + le16(local + 26) + le16(local + 28);
        if (data + static_cast<long>(packed) > fileSize)
            return std::unexpected("archive entry not found or outside file bounds");
        std::vector<uint8_t> bytes(packed);
        if (!readAt(file, data, bytes.data(), bytes.size()))
            return std::unexpected("could not read archive bytes");
        return unpack(method, std::move(bytes), size);
    }
    return std::unexpected("archive entry not found or outside file bounds");
}

// --- Yaz0, Nintendo's run-length and back-reference compression, which SARCs are often in.

std::expected<std::vector<uint8_t>, std::string> yaz0(const std::vector<uint8_t>& in)
{
    if (in.size() < 16)
        return std::unexpected("bad Yaz0 header");
    const uint32_t size = be32(&in[4]);
    if (size > kMaxSize)
        return std::unexpected("archive exceeds size limit");
    // the header can say anything: Yaz0 makes at most 88 times what it reads (8 references of 273 bytes from 25)
    std::vector<uint8_t> out;
    out.reserve(std::min<size_t>(size, in.size() * 88));
    size_t at = 16;
    while (out.size() < size && at < in.size())
    {
        const uint8_t header = in[at++];
        for (int bit = 7; bit >= 0 && out.size() < size; --bit)
        {
            if (header & (1 << bit))
            {
                if (at >= in.size())
                    return std::unexpected("truncated or invalid Yaz0 stream");
                out.push_back(in[at++]);
                continue;
            }
            if (at + 2 > in.size())
                return std::unexpected("truncated or invalid Yaz0 stream");
            const uint8_t a = in[at++];
            const uint8_t b = in[at++];
            const size_t back = static_cast<size_t>(((a & 0x0F) << 8) | b) + 1;
            size_t count = a >> 4;
            if (count == 0)
            {
                if (at >= in.size())
                    return std::unexpected("truncated or invalid Yaz0 stream");
                count = static_cast<size_t>(in[at++]) + 0x12;
            }
            else
            {
                count += 2;
            }
            if (back > out.size())
                return std::unexpected("truncated or invalid Yaz0 stream");
            for (size_t k = 0; k < count && out.size() < size; ++k)
                out.push_back(out[out.size() - back]);
        }
    }
    if (out.size() != size)
        return std::unexpected("truncated or invalid Yaz0 stream");
    return out;
}

// --- SARC: a table of files (SFAT), their names (SFNT), then their data.

std::expected<std::vector<uint8_t>, std::string> sarcFile(const std::vector<uint8_t>& sarc, std::string_view name, size_t maxSize)
{
    if (sarc.size() < 0x20 || std::memcmp(sarc.data(), "SARC", 4) != 0)
        return std::unexpected("bad SARC header");
    // the byte order mark: FF FE for little-endian, as on the Switch
    const bool little = sarc[6] == 0xFF && sarc[7] == 0xFE;
    const auto u16 = [&](size_t at) {
        return little ? le16(&sarc[at]) : static_cast<uint16_t>((sarc[at] << 8) | sarc[at + 1]);
    };
    const auto u32 = [&](size_t at) { return little ? le32(&sarc[at]) : be32(&sarc[at]); };
    const size_t headerSize = u16(4);
    const uint32_t dataOffset = u32(12);
    const size_t sfat = headerSize;
    if (sfat + 12 > sarc.size() || std::memcmp(&sarc[sfat], "SFAT", 4) != 0)
        return std::unexpected("bad SARC SFAT header");
    const uint16_t nodes = u16(sfat + 6);
    const size_t table = sfat + u16(sfat + 4);
    const size_t sfnt = table + static_cast<size_t>(nodes) * 16;
    if (sfnt + 8 > sarc.size() || std::memcmp(&sarc[sfnt], "SFNT", 4) != 0)
        return std::unexpected("bad SARC SFNT header");
    const size_t names = sfnt + u16(sfnt + 4);
    for (uint16_t i = 0; i < nodes; ++i)
    {
        const size_t node = table + static_cast<size_t>(i) * 16;
        const uint32_t attributes = u32(node + 4);
        // without a name (bit 24 clear), it can only be found by its hash
        if (!(attributes & 0x01000000))
            continue;
        const size_t nameAt = names + static_cast<size_t>(attributes & 0xFFFF) * 4;
        if (nameAt >= sarc.size())
            continue;
        const char* entryName = reinterpret_cast<const char*>(&sarc[nameAt]);
        const size_t length = strnlen(entryName, sarc.size() - nameAt);
        if (!sameName(std::string_view(entryName, length), name))
            continue;
        const uint64_t start = static_cast<uint64_t>(dataOffset) + u32(node + 8);
        const uint64_t end = static_cast<uint64_t>(dataOffset) + u32(node + 12);
        if (end < start || end > sarc.size() || end - start > maxSize)
            return std::unexpected("archive entry not found or outside file bounds");
        return std::vector<uint8_t>(sarc.begin() + static_cast<long>(start), sarc.begin() + static_cast<long>(end));
    }
    return std::unexpected("invalid zip directory");
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> readFile(const std::vector<uint8_t>& archive, std::string_view name,
                                             size_t maxSize)
{
    if (archive.size() >= 4 && std::memcmp(archive.data(), "Yaz0", 4) == 0)
    {
        auto sarc = yaz0(archive);
        return sarc ? sarcFile(*sarc, name, maxSize) : std::unexpected(sarc.error());
    }
    return sarcFile(archive, name, maxSize);
}

std::expected<std::vector<uint8_t>, std::string> readFile(const std::string& path, std::string_view name, size_t maxSize)
{
    File file(std::fopen(path.c_str(), "rb"));
    if (!file)
        return std::unexpected("could not read archive bytes");
    uint8_t magic[4] = {};
    if (std::fread(magic, 1, 4, file.get()) != 4 || std::fseek(file.get(), 0, SEEK_END) != 0)
        return std::unexpected("could not read archive bytes");
    const long size = std::ftell(file.get());
    if (size <= 0)
        return std::unexpected("could not read archive bytes");
    if (magic[0] == 'P' && magic[1] == 'K')
        return zipFile(file.get(), size, name, maxSize);
    // a SARC is read whole: it has no index to seek by at its end
    if (static_cast<size_t>(size) > kMaxSize)
        return std::unexpected("archive entry not found or outside file bounds");
    std::vector<uint8_t> whole(static_cast<size_t>(size));
    if (!readAt(file.get(), 0, whole.data(), whole.size()))
        return std::unexpected("could not read archive bytes");
    return readFile(whole, name, maxSize);
}

} // namespace tz::archive
