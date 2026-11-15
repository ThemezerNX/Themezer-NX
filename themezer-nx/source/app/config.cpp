#include "app/config.hpp"
#include "sys/log.hpp"

#include "libs/cJSON.h"

#include <cstdio>

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
    char text[512];
    const size_t length = std::fread(text, 1, sizeof(text) - 1, file);
    if (std::ferror(file))
        log::warn("Settings: could not read %s", kPath);
    std::fclose(file);
    text[length] = '\0';
    cJSON* root = cJSON_Parse(text);
    if (!root)
        log::warn("Settings: invalid JSON in %s", kPath);
    if (const cJSON* item = cJSON_GetObjectItem(root, "music"); cJSON_IsBool(item))
        music = cJSON_IsTrue(item);
    if (const cJSON* item = cJSON_GetObjectItem(root, "sfx"); cJSON_IsBool(item))
        sfx = cJSON_IsTrue(item);
    cJSON_Delete(root);
}

void Config::save() const
{
    FILE* file = std::fopen(kPath, "wb");
    if (!file)
    {
        log::warn("Settings: could not open %s", kPath);
        return;
    }
    const int written =
        std::fprintf(file, "{\"music\":%s,\"sfx\":%s}\n", music ? "true" : "false", sfx ? "true" : "false");
    if (std::fclose(file) != 0 || written < 0)
        log::error("Settings: could not save %s", kPath);
}

} // namespace tz
