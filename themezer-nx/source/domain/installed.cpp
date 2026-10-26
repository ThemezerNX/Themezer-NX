#include "domain/installed.hpp"

#include "domain/installs.hpp"
#include "domain/library.hpp"
#include "domain/outdated.hpp"
#include "libs/cJSON.h"

#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <array>
#include <cstdio>
#include <optional>
#include <vector>

namespace tz::installed
{

namespace
{

constexpr const char* kRecord = "/Themes/ThemezerNX/installed.json";

// Where NXThemes Installer puts each part's theme: the layouts of the HOME Menu's applets (qlaunch, the player
// selection, the user page) in Atmosphère's romfs folders; a Home Menu theme's common.szs too. The splash is Hekate's.
struct Files
{
    const char* main;
    const char* extra;
};
constexpr Files kFiles[installs::kSlotCount] = {
    {"/atmosphere/contents/0100000000001000/romfs/lyt/ResidentMenu.szs",
     "/atmosphere/contents/0100000000001000/romfs/lyt/common.szs"},
    {"/atmosphere/contents/0100000000001000/romfs/lyt/Entrance.szs", nullptr},
    {"/atmosphere/contents/0100000000001000/romfs/lyt/Flaunch.szs", nullptr},
    {"/atmosphere/contents/0100000000001000/romfs/lyt/Set.szs", nullptr},
    {"/atmosphere/contents/0100000000001007/romfs/lyt/Psl.szs", nullptr},
    {"/atmosphere/contents/0100000000001013/romfs/lyt/MyPage.szs", nullptr},
    {"/atmosphere/contents/0100000000001000/romfs/lyt/Notification.szs", nullptr},
    {"/bootloader/bootlogo.bmp", nullptr},
};

struct Record
{
    std::string name;
    std::string author;
    std::string source;
    std::string themezerId;
    std::string fingerprint; // of the installed file, when it was found to be this
};

// Queued on the way out: becomes the record when the installer turns out to have changed the file.
struct Pending
{
    Record record;
    std::string before; // the file's fingerprint before
};

std::array<std::optional<Record>, installs::kSlotCount> records;
std::array<std::optional<Pending>, installs::kSlotCount> pending;
std::array<Part, installs::kSlotCount> parts;

std::string fingerprint(const char* path)
{
    return library::fingerprint(path);
}

std::string text(const cJSON* object, const char* name)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

Record readRecord(const cJSON* object)
{
    return {text(object, "name"), text(object, "author"), text(object, "source"), text(object, "themezerId"),
            text(object, "fingerprint")};
}

cJSON* writeRecord(const Record& record)
{
    cJSON* object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "name", record.name.c_str());
    cJSON_AddStringToObject(object, "author", record.author.c_str());
    cJSON_AddStringToObject(object, "source", record.source.c_str());
    cJSON_AddStringToObject(object, "themezerId", record.themezerId.c_str());
    cJSON_AddStringToObject(object, "fingerprint", record.fingerprint.c_str());
    return object;
}

void load()
{
    records = {};
    pending = {};
    FILE* file = std::fopen(kRecord, "rb");
    if (!file)
        return;
    std::string json;
    char buffer[2048];
    size_t read;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        json.append(buffer, read);
    std::fclose(file);
    cJSON* root = cJSON_Parse(json.c_str());
    for (int slot = 0; slot < installs::kSlotCount; ++slot)
    {
        const std::string key = std::to_string(slot);
        if (const cJSON* record = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "installed"),
                                                                   key.c_str());
            cJSON_IsObject(record))
            records[static_cast<size_t>(slot)] = readRecord(record);
        if (const cJSON* queued = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "queued"),
                                                                   key.c_str());
            cJSON_IsObject(queued))
            pending[static_cast<size_t>(slot)] = Pending{readRecord(queued), text(queued, "before")};
    }
    cJSON_Delete(root);
}

void save()
{
    cJSON* root = cJSON_CreateObject();
    cJSON* installed = cJSON_AddObjectToObject(root, "installed");
    cJSON* queued = cJSON_AddObjectToObject(root, "queued");
    for (int slot = 0; slot < installs::kSlotCount; ++slot)
    {
        const std::string key = std::to_string(slot);
        if (const auto& record = records[static_cast<size_t>(slot)])
            cJSON_AddItemToObject(installed, key.c_str(), writeRecord(*record));
        if (const auto& waiting = pending[static_cast<size_t>(slot)])
        {
            cJSON* object = writeRecord(waiting->record);
            cJSON_AddStringToObject(object, "before", waiting->before.c_str());
            cJSON_AddItemToObject(queued, key.c_str(), object);
        }
    }
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed)
        return;
    if (FILE* file = std::fopen(kRecord, "wb"))
    {
        std::fputs(printed, file);
        std::fclose(file);
    }
    cJSON_free(printed);
}

} // namespace

void refresh()
{
    load();
    bool changed = false;
    for (int slot = 0; slot < installs::kSlotCount; ++slot)
    {
        const auto index = static_cast<size_t>(slot);
        const std::string now = fingerprint(kFiles[index].main);
        // queued last time: installed, when the installer changed the file
        if (auto& waiting = pending[index])
        {
            if (!now.empty() && now != waiting->before)
            {
                records[index] = waiting->record;
                records[index]->fingerprint = now;
            }
            waiting.reset();
            changed = true;
        }
        // the file isn't what was installed any more: gone, or something else is
        if (records[index] && records[index]->fingerprint != now)
        {
            records[index].reset();
            changed = true;
        }
        Part& part = parts[index];
        part = {};
        part.custom = !now.empty();
        part.fingerprint = now;
        if (const auto& record = records[index])
        {
            part.name = record->name;
            part.author = record->author;
            part.source = record->source;
            part.themezerId = record->themezerId;
        }
    }
    if (changed)
        save();
}

const Part& part(int slot)
{
    static const Part kNone;
    return slot >= 0 && slot < installs::kSlotCount ? parts[static_cast<size_t>(slot)] : kNone;
}

void noteQueued()
{
    load();
    for (int slot = 0; slot < installs::kSlotCount; ++slot)
    {
        const installs::Queued* queued = installs::queued(slot);
        if (!queued)
            continue;
        Record record{queued->name, queued->author, queued->path, queued->themezerId, {}};
        pending[static_cast<size_t>(slot)] = Pending{std::move(record), fingerprint(kFiles[slot].main)};
    }
    save();
}

bool uninstall(int slot)
{
    if (slot < 0 || slot >= installs::kSlotCount)
        return false;
    bool ok = true;
    for (const char* path : {kFiles[slot].main, kFiles[slot].extra})
    {
        struct stat info;
        if (path && stat(path, &info) == 0 && unlink(path) != 0)
            ok = false;
    }
    load();
    records[static_cast<size_t>(slot)].reset();
    save();
    refresh();
    return ok;
}

void moved(const std::vector<std::pair<std::string, std::string>>& moves)
{
    load();
    bool changed = false;
    const auto follow = [&](std::string& source) {
        for (const auto& [from, to] : moves)
        {
            std::string now = outdated::followed(source, from, to);
            changed = changed || now != source;
            source = std::move(now);
        }
    };
    for (auto& record : records)
    {
        if (record)
            follow(record->source);
    }
    for (auto& waiting : pending)
    {
        if (waiting)
            follow(waiting->record.source);
    }
    if (!changed)
        return;
    save();
    refresh();
}

} // namespace tz::installed
