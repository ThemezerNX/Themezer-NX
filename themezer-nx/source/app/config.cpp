#include "app/config.hpp"
#include "sys/log.hpp"
#include "sys/files.hpp"

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
    const std::string text = std::string("{\"music\":") + (music ? "true" : "false") +
                             ",\"sfx\":" + (sfx ? "true" : "false") + "}\n";
    if (auto saved = files::write(kPath, {reinterpret_cast<const uint8_t*>(text.data()), text.size()}); !saved)
        log::error("Settings: %s (%s)", saved.error().message.c_str(), saved.error().detail.c_str());
}

} // namespace tz
