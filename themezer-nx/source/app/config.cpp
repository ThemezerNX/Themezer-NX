#include "app/config.hpp"
#include "sys/log.hpp"
#include "sys/files.hpp"

#include "libs/cJSON.h"

#include <cstdio>
#include <string>

namespace tz
{

namespace
{
constexpr const char* kPath = "/Themes/ThemezerNX/settings.json";
} // namespace

void Config::load()
{
    FILE* file = std::fopen(kPath, "rb");
    if (!file)
    {
        log::warn("Settings: could not open %s", kPath);
        return;
    }
    // read whole: cut short, it would read as no settings, and every migration would run again
    std::string text;
    char chunk[512];
    while (const size_t length = std::fread(chunk, 1, sizeof(chunk), file))
        text.append(chunk, length);
    if (std::ferror(file))
        log::warn("Settings: could not read %s", kPath);
    std::fclose(file);
    cJSON* root = cJSON_Parse(text.c_str());
    if (!root)
        log::warn("Settings: invalid JSON in %s", kPath);
    if (const cJSON* item = cJSON_GetObjectItem(root, "music"); cJSON_IsBool(item))
        music = cJSON_IsTrue(item);
    if (const cJSON* item = cJSON_GetObjectItem(root, "sfx"); cJSON_IsBool(item))
        sfx = cJSON_IsTrue(item);
    if (const cJSON* item = cJSON_GetObjectItem(root, "updateCheck"); cJSON_IsBool(item))
        updateCheck = cJSON_IsTrue(item);
    const cJSON* done = nullptr;
    cJSON_ArrayForEach(done, cJSON_GetObjectItem(root, "migrations"))
    {
        if (cJSON_IsTrue(done) && done->string)
            migrations.insert(done->string);
    }
    cJSON_Delete(root);
}

void Config::save() const
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "music", music);
    cJSON_AddBoolToObject(root, "sfx", sfx);
    cJSON_AddBoolToObject(root, "updateCheck", updateCheck);
    cJSON* done = cJSON_AddObjectToObject(root, "migrations");
    for (const std::string& id : migrations)
        cJSON_AddBoolToObject(done, id.c_str(), true);
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed)
    {
        log::error("Settings: out of memory");
        return;
    }
    const std::string text = std::string(printed) + "\n";
    cJSON_free(printed);
    if (auto saved = files::write(kPath, {reinterpret_cast<const uint8_t*>(text.data()), text.size()}); !saved)
        log::error("Settings: %s (%s)", saved.error().message.c_str(), saved.error().detail.c_str());
}

} // namespace tz
