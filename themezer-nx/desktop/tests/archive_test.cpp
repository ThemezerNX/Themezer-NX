// make -C desktop test: the .nxtheme reader on archives made here, a zip (stored and deflated), a SARC, and a SARC in
// Yaz0, with literals and back-references.

#include "core/archive.hpp"

#include <zlib.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

using Bytes = std::vector<uint8_t>;

void put16(Bytes& out, uint16_t v)
{
    out.push_back(v & 0xFF);
    out.push_back(v >> 8);
}

void put32(Bytes& out, uint32_t v)
{
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}

Bytes text(const std::string& s)
{
    return Bytes(s.begin(), s.end());
}

// A little-endian SARC with the files, named.
Bytes sarc(const std::vector<std::pair<std::string, Bytes>>& files)
{
    Bytes names;
    std::vector<uint32_t> nameOffsets;
    for (const auto& [name, data] : files)
    {
        nameOffsets.push_back(static_cast<uint32_t>(names.size() / 4));
        names.insert(names.end(), name.begin(), name.end());
        names.push_back(0);
        while (names.size() % 4)
            names.push_back(0);
    }
    const uint32_t header = 0x14, sfat = 0x0C + 16 * static_cast<uint32_t>(files.size()), sfnt = 8;
    const uint32_t dataOffset = header + sfat + sfnt + static_cast<uint32_t>(names.size());
    Bytes data;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    for (const auto& [name, contents] : files)
    {
        ranges.emplace_back(static_cast<uint32_t>(data.size()), static_cast<uint32_t>(data.size() + contents.size()));
        data.insert(data.end(), contents.begin(), contents.end());
    }
    Bytes out = text("SARC");
    put16(out, header);
    out.push_back(0xFF);
    out.push_back(0xFE);
    put32(out, dataOffset + static_cast<uint32_t>(data.size()));
    put32(out, dataOffset);
    put16(out, 0x0100);
    put16(out, 0);
    out.insert(out.end(), {'S', 'F', 'A', 'T'});
    put16(out, 0x0C);
    put16(out, static_cast<uint16_t>(files.size()));
    put32(out, 0x65);
    for (size_t i = 0; i < files.size(); ++i)
    {
        put32(out, 0); // the name's hash, which the reader doesn't need
        put32(out, 0x01000000 | nameOffsets[i]);
        put32(out, ranges[i].first);
        put32(out, ranges[i].second);
    }
    out.insert(out.end(), {'S', 'F', 'N', 'T'});
    put16(out, 8);
    put16(out, 0);
    out.insert(out.end(), names.begin(), names.end());
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

// Yaz0 of in, greedy: the longest match in the last 4 KiB when it's 3 bytes or more, else a literal. Both forms of
// back-reference come up: long ones in the image, which repeats every 256 bytes, short ones in the SARC's header.
Bytes yaz0(const Bytes& in)
{
    Bytes out = text("Yaz0");
    out.insert(out.end(), {static_cast<uint8_t>(in.size() >> 24), static_cast<uint8_t>(in.size() >> 16),
                           static_cast<uint8_t>(in.size() >> 8), static_cast<uint8_t>(in.size())});
    out.insert(out.end(), 8, 0);
    size_t at = 0;
    while (at < in.size())
    {
        const size_t header = out.size();
        out.push_back(0);
        for (int bit = 7; bit >= 0 && at < in.size(); --bit)
        {
            size_t best = 0, distance = 0;
            for (size_t back = 1; back <= 0x1000 && back <= at; ++back)
            {
                // a match may run into the bytes it makes, as a run does
                size_t length = 0;
                while (length < 0x111 && at + length < in.size() && in[at + length - back] == in[at + length])
                    ++length;
                if (length > best)
                {
                    best = length;
                    distance = back;
                }
            }
            if (best < 3)
            {
                out[header] |= static_cast<uint8_t>(1 << bit);
                out.push_back(in[at++]);
                continue;
            }
            const size_t back = distance - 1;
            if (best < 0x12)
            {
                out.push_back(static_cast<uint8_t>(((best - 2) << 4) | (back >> 8)));
                out.push_back(static_cast<uint8_t>(back & 0xFF));
            }
            else
            {
                out.push_back(static_cast<uint8_t>(back >> 8));
                out.push_back(static_cast<uint8_t>(back & 0xFF));
                out.push_back(static_cast<uint8_t>(best - 0x12));
            }
            at += best;
        }
    }
    return out;
}

// A zip with a stored and a deflated file, written to path.
void zip(const char* path, const std::string& storedName, const Bytes& stored, const std::string& deflatedName,
         const Bytes& plain)
{
    Bytes packed(compressBound(static_cast<uLong>(plain.size())) + 16);
    z_stream stream{};
    deflateInit2(&stream, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    stream.next_in = const_cast<uint8_t*>(plain.data());
    stream.avail_in = static_cast<uInt>(plain.size());
    stream.next_out = packed.data();
    stream.avail_out = static_cast<uInt>(packed.size());
    deflate(&stream, Z_FINISH);
    packed.resize(stream.total_out);
    deflateEnd(&stream);

    Bytes out, directory;
    const auto entry = [&](const std::string& name, const Bytes& data, uint16_t method, size_t size) {
        const uint32_t offset = static_cast<uint32_t>(out.size());
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0);
        put16(out, method);
        put32(out, 0);
        put32(out, 0);
        put32(out, static_cast<uint32_t>(data.size()));
        put32(out, static_cast<uint32_t>(size));
        put16(out, static_cast<uint16_t>(name.size()));
        put16(out, 0);
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), data.begin(), data.end());
        put32(directory, 0x02014b50);
        put16(directory, 20);
        put16(directory, 20);
        put16(directory, 0);
        put16(directory, method);
        put32(directory, 0);
        put32(directory, 0);
        put32(directory, static_cast<uint32_t>(data.size()));
        put32(directory, static_cast<uint32_t>(size));
        put16(directory, static_cast<uint16_t>(name.size()));
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, 0);
        put32(directory, offset);
        directory.insert(directory.end(), name.begin(), name.end());
    };
    entry(storedName, stored, 0, stored.size());
    entry(deflatedName, packed, 8, plain.size());
    const uint32_t directoryOffset = static_cast<uint32_t>(out.size());
    out.insert(out.end(), directory.begin(), directory.end());
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, 2);
    put16(out, 2);
    put32(out, static_cast<uint32_t>(directory.size()));
    put32(out, directoryOffset);
    put16(out, 0);
    FILE* file = std::fopen(path, "wb");
    std::fwrite(out.data(), 1, out.size(), file);
    std::fclose(file);
}

} // namespace

int main()
{
    using tz::archive::readFile;
    constexpr size_t kMax = 1024 * 1024;
    const Bytes info = text(R"({"Version":17,"ThemeName":"Test","Author":"Me","Target":"home"})");
    Bytes image(5000);
    for (size_t i = 0; i < image.size(); ++i)
        image[i] = static_cast<uint8_t>(i * 7);

    const Bytes archive = sarc({{"info.json", info}, {"image.jpg", image}});
    assert(readFile(archive, "INFO.json", kMax) == info);
    assert(readFile(archive, "image.jpg", kMax) == image);
    assert(!readFile(archive, "layout.json", kMax));

    assert(!readFile(archive, "image.jpg", image.size() - 1));

    // the same in Yaz0, which back-references make much smaller
    const Bytes packed = yaz0(archive);
    assert(packed.size() < archive.size() / 2);
    assert(readFile(packed, "image.jpg", kMax) == image);
    assert(readFile(packed, "info.json", kMax) == info);

    // a back-reference that runs into what it makes: "abc", then 6 bytes from 3 back
    const Bytes repeated = text("abcabcabc");
    assert(readFile(yaz0(sarc({{"x.txt", repeated}})), "x.txt", kMax) == repeated);

    // a Yaz0 that ends early, or reaches back before its start, is no archive
    Bytes cut = packed;
    cut.resize(cut.size() / 2);
    assert(!readFile(cut, "image.jpg", kMax));
    Bytes before = text("Yaz0");
    before.insert(before.end(), {0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x10, 0x05});
    assert(!readFile(before, "x", kMax));

    const char* path = "/tmp/themezer-archive-test.zip";
    Bytes plain(20000, 'z');
    zip(path, "info.json", info, "image.jpg", plain);
    assert(readFile(std::string(path), "info.json", kMax) == info);
    assert(readFile(std::string(path), "IMAGE.JPG", kMax) == plain);
    assert(!readFile(std::string(path), "missing.png", kMax));
    assert(!readFile(std::string(path), "IMAGE.JPG", plain.size() - 1));

    // a central directory said to be where adding its offset and size wraps round to 0 isn't read
    FILE* file = std::fopen(path, "r+b");
    std::fseek(file, -22 + 12, SEEK_END);
    const uint8_t wrapping[8] = {0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xFC};
    std::fwrite(wrapping, 1, sizeof(wrapping), file);
    std::fclose(file);
    assert(!readFile(std::string(path), "info.json", kMax));
    std::remove(path);

    assert(readFile(text("SARC"), "x", kMax).error().find("bad SARC header") != std::string::npos);
    assert(readFile(text("Yaz0"), "x", kMax).error().find("bad Yaz0 header") != std::string::npos);
    const auto write = [path](const Bytes& bytes) {
        FILE* file = std::fopen(path, "wb");
        assert(file);
        assert(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
        std::fclose(file);
    };
    write(text("PK incomplete"));
    auto broken = readFile(std::string(path), "info.json", kMax);
    assert(!broken && broken.error().find("13 bytes starting 504B2069") != std::string::npos);
    zip(path, "info.json", info, "image.jpg", plain);
    file = std::fopen(path, "r+b");
    std::fseek(file, 2, SEEK_SET);
    std::fputc(255, file);
    std::fclose(file);
    broken = readFile(std::string(path), "info.json", kMax);
    assert(!broken && broken.error().find("no local header at 0") != std::string::npos);
    assert(broken.error().find("504BFF04") != std::string::npos);
    zip(path, "info.json", info, "image.jpg", plain);
    file = std::fopen(path, "r+b");
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::rewind(file);
    Bytes bytes(length);
    assert(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    std::fclose(file);
    for (size_t at = 0; at + 46 <= bytes.size(); ++at)
    {
        if (bytes[at] == 'P' && bytes[at+1] == 'K' && bytes[at+2] == 1 && bytes[at+3] == 2)
        {
            bytes[at+10] = 99;
            break;
        }
    }
    write(bytes);
    broken = readFile(std::string(path), "info.json", kMax);
    assert(!broken && broken.error().find("unknown zip compression method 99") != std::string::npos);
    std::remove(path);

    std::printf("archive: all good\n");
    return 0;
}
