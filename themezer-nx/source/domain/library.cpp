#include "domain/library.hpp"

#include "core/archive.hpp"
#include "domain/names.hpp"
#include "domain/outdated.hpp"
#include "libs/cJSON.h"

#include <SDL2/SDL_image.h>
#include <zlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace tz::library
{

namespace
{

constexpr int kMaxDepth = 4;
// a Hekate splash, as the screen is: portrait, 720 wide and 1280 high
constexpr int kSplashWidth = 720;
constexpr int kSplashHeight = 1280;

bool endsWith(const std::string& text, const char* suffix)
{
    const size_t length = std::strlen(suffix);
    if (text.size() < length)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        if (std::tolower(static_cast<unsigned char>(text[text.size() - length + i])) != suffix[i])
            return false;
    }
    return true;
}

std::string stem(const std::string& fileName)
{
    const size_t dot = fileName.rfind('.');
    return dot == std::string::npos ? fileName : fileName.substr(0, dot);
}

// A BMP's header says it's a Hekate splash: 720 by 1280, either way up.
bool isSplash(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return false;
    uint8_t header[26] = {};
    const bool read = std::fread(header, 1, sizeof(header), file) == sizeof(header);
    std::fclose(file);
    if (!read || header[0] != 'B' || header[1] != 'M')
        return false;
    int32_t width, height;
    std::memcpy(&width, header + 18, 4);
    std::memcpy(&height, header + 22, 4);
    return width == kSplashWidth && (height == kSplashHeight || height == -kSplashHeight);
}

// A folder Themezer-NX made for a pack: "<name> by <author> (<ID>)", shown without the ID.
std::string folderName(const std::string& folder)
{
    const size_t open = folder.rfind(" (");
    if (open != std::string::npos && !folder.empty() && folder.back() == ')')
        return folder.substr(0, open);
    return folder;
}

void walk(const std::string& dir, const std::string& folder, int depth, std::vector<Entry>& out)
{
    DIR* handle = opendir(dir.c_str());
    if (!handle)
        return;
    std::vector<std::pair<std::string, bool>> children;
    while (dirent* entry = readdir(handle))
    {
        if (entry->d_name[0] == '.')
            continue;
        bool isDir = entry->d_type == DT_DIR;
        if (entry->d_type == DT_UNKNOWN)
        {
            struct stat info;
            isDir = stat((dir + "/" + entry->d_name).c_str(), &info) == 0 && S_ISDIR(info.st_mode);
        }
        children.emplace_back(entry->d_name, isDir);
    }
    closedir(handle);
    for (const auto& [name, isDir] : children)
    {
        const std::string path = dir + "/" + name;
        if (isDir)
        {
            // the installer's copies of the system's own files aren't themes
            if (depth < kMaxDepth && name != "systemData")
                walk(path, depth == 0 && name == "ThemezerNX" ? "" : name, depth + 1, out);
            continue;
        }
        Kind kind;
        if (endsWith(name, ".nxtheme"))
            kind = Kind::Theme;
        else if (endsWith(name, ".bmp") && isSplash(path))
            kind = Kind::Splash;
        else
            continue;
        Entry entry;
        entry.kind = kind;
        entry.path = path;
        entry.fileName = name;
        entry.folder = folderName(folder);
        struct stat info;
        if (stat(path.c_str(), &info) == 0)
        {
            entry.size = static_cast<uint64_t>(info.st_size);
            entry.modified = static_cast<int64_t>(info.st_mtime);
        }
        if (names::Parts parts; names::parseFile(name, parts))
        {
            entry.name = parts.name;
            entry.author = parts.author;
            entry.themezerId = parts.id;
            entry.v3Name = parts.v3Name;
            if (kind == Kind::Theme)
                entry.target = parts.target;
        }
        else
        {
            entry.name = stem(name);
        }
        if (kind == Kind::Splash)
            entry.fingerprint = fingerprint(path);
        out.push_back(std::move(entry));
    }
}

// info.json's names for the parts, in the API's order (see themezer::targetLabel)
constexpr const char* kInfoTargets[] = {"home", "lock", "apps", "set", "psl", "user", "news"};

int targetFromInfo(const char* name)
{
    for (int i = 0; i < static_cast<int>(std::size(kInfoTargets)); ++i)
    {
        if (std::strcmp(name, kInfoTargets[i]) == 0)
            return i;
    }
    return -1;
}

// As big as it may be, its sides halved while that keeps it wider than maxWidth, then scaled the rest of the way:
// smooth steps, where one big step would leave out most of the pixels.
SDL_Surface* fit(SDL_Surface* surface, int maxWidth)
{
    while (surface && surface->w > maxWidth)
    {
        const int width = std::max(maxWidth, surface->w / 2);
        const int height = std::max(1, surface->h * width / surface->w);
        SDL_Surface* smaller = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, surface->format->format);
        if (!smaller || SDL_SoftStretchLinear(surface, nullptr, smaller, nullptr) != 0)
        {
            SDL_FreeSurface(smaller);
            break;
        }
        SDL_FreeSurface(surface);
        surface = smaller;
    }
    return surface;
}

// The splash turned a quarter round clockwise, from how it's stored for the screen to how the screen is held.
SDL_Surface* turned(SDL_Surface* source)
{
    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, source->h, source->w, 32, source->format->format);
    if (!out)
        return nullptr;
    SDL_LockSurface(source);
    for (int y = 0; y < source->h; ++y)
    {
        const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(source->pixels) + y * source->pitch);
        for (int x = 0; x < source->w; ++x)
        {
            // (x, y) to (h - 1 - y, x)
            auto* to = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(out->pixels) + x * out->pitch);
            to[source->h - 1 - y] = row[x];
        }
    }
    SDL_UnlockSurface(source);
    return out;
}

// Text from a file, as long as a page can show: cut at most bytes, not in the middle of a character.
std::string cut(const char* text, size_t most)
{
    if (strnlen(text, most + 1) <= most)
        return text;
    size_t end = most;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
        --end;
    return std::string(text, end);
}

// A theme's info.json parsed, for cJSON_Delete; nullptr when it can't be read.
cJSON* parseInfo(const std::string& path, std::string* reason = nullptr)
{
    // a few hundred bytes, as the installer writes them
    auto json = archive::readFile(path, "info.json", 1024 * 1024);
    if (!json)
    {
        if (reason) *reason = path + ": " + json.error();
        return nullptr;
    }
    json->push_back('\0');
    cJSON* root = cJSON_Parse(reinterpret_cast<const char*>(json->data()));
    if (!cJSON_IsObject(root))
    {
        if (reason) *reason = path + ": invalid info.json object";
        cJSON_Delete(root);
        return nullptr;
    }
    return root;
}

// The Id in the info.json of a theme Themezer built, "Themezer:<ID>:<ms>"; empty for any other file. Only a zip is
// opened, as Themezer's are: archive::readFile would read a SARC whole, up to 16 MiB, for nothing.
std::string idText(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    char magic[2] = {};
    const bool zip = file && std::fread(magic, 1, sizeof(magic), file) == sizeof(magic) && magic[0] == 'P' &&
                     magic[1] == 'K';
    if (file)
        std::fclose(file);
    if (!zip)
        return {};
    cJSON* root = parseInfo(path);
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "Id");
    std::string text = cJSON_IsString(id) && id->valuestring ? id->valuestring : "";
    cJSON_Delete(root);
    return text;
}

} // namespace

std::vector<Entry> scan()
{
    std::vector<Entry> out;
    walk(kRoot, "", 0, out);
    // a theme Themezer built, named otherwise (NXThemes Installer's downloads, a renamed file), says its ID inside;
    // only builds from after Themezer-NX 3.3's release have it, so an older one stays unknown
    for (Entry& entry : out)
    {
        if (entry.kind == Kind::Theme && entry.themezerId.empty())
            entry.themezerId = outdated::idOfId(idText(entry.path));
    }
    return out;
}

bool readInfo(Entry& entry)
{
    entry.infoRead = true;
    entry.errorDetail.clear();
    if (entry.kind != Kind::Theme)
        return true;
    cJSON* root = parseInfo(entry.path, &entry.errorDetail);
    if (!root)
        return false;
    const auto text = [&](const char* name) {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
        return cJSON_IsString(item) && item->valuestring ? cut(item->valuestring, 256) : std::string();
    };
    // the file's own names are the real ones: Themezer-NX's file names keep to what the SD card allows
    if (std::string name = text("ThemeName"); !name.empty())
        entry.name = name;
    if (std::string author = text("Author"); !author.empty())
        entry.author = author;
    const int target = targetFromInfo(text("Target").c_str());
    if (target >= 0)
        entry.target = target;
    entry.layout = text("LayoutInfo");
    if (target < 0) entry.errorDetail = entry.path + ": unsupported or missing Target in info.json";
    cJSON_Delete(root);
    return entry.errorDetail.empty();
}

std::optional<int64_t> builtAt(const std::string& path)
{
    return outdated::millisOfId(idText(path));
}

SDL_Surface* readPreview(const Entry& entry, int maxWidth)
{
    SDL_Surface* surface = nullptr;
    if (entry.kind == Kind::Theme)
    {
        // Themezer's screenshot, else a JPEG background; the older DDS ones have no picture here
        auto jpeg = archive::readFile(entry.path, "preview.jpg", 8 * 1024 * 1024);
        if (!jpeg)
            jpeg = archive::readFile(entry.path, "image.jpg", 8 * 1024 * 1024);
        if (!jpeg)
            return nullptr;
        SDL_RWops* rw = SDL_RWFromConstMem(jpeg->data(), static_cast<int>(jpeg->size()));
        surface = rw ? IMG_Load_RW(rw, 1) : nullptr;
    }
    else
    {
        // read whole, as the SD card is best read: in one go; a splash is 3.7 MB at most
        if (entry.size > 8 * 1024 * 1024)
            return nullptr;
        std::vector<uint8_t> bytes(static_cast<size_t>(entry.size));
        FILE* file = std::fopen(entry.path.c_str(), "rb");
        const bool read = file && std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
        if (file)
            std::fclose(file);
        SDL_RWops* rw = read ? SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size())) : nullptr;
        SDL_Surface* bmp = rw ? SDL_LoadBMP_RW(rw, 1) : nullptr;
        if (bmp)
        {
            SDL_Surface* converted = SDL_ConvertSurfaceFormat(bmp, SDL_PIXELFORMAT_ABGR8888, 0);
            SDL_FreeSurface(bmp);
            if (converted)
            {
                surface = turned(converted);
                SDL_FreeSurface(converted);
            }
        }
    }
    if (!surface)
        return nullptr;
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surface);
    return converted ? fit(converted, maxWidth) : nullptr;
}

std::string fingerprint(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return {};
    std::vector<unsigned char> start(64 * 1024);
    const size_t read = std::fread(start.data(), 1, start.size(), file);
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fclose(file);
    char text[40];
    std::snprintf(text, sizeof(text), "%ld:%08lx", size, crc32(0L, start.data(), static_cast<uInt>(read)));
    return text;
}

bool remove(const Entry& entry)
{
    return unlink(entry.path.c_str()) == 0;
}

} // namespace tz::library
