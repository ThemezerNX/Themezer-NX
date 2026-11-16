#include "services/updates.hpp"

#include "sys/log.hpp"
#include "core/version.hpp"
#include "domain/installs.hpp"
#include "libs/cJSON.h"
#include "sys/jobs.hpp"
#include "sys/net.hpp"

#include <unistd.h>

#include <array>
#include <cstdio>
#include <memory>
#include <optional>

namespace tz::updates
{

namespace
{

std::array<std::optional<Release>, 2> releases;

// The latest release's version and .nro, from GitHub's API; nothing when it couldn't be asked or has none.
std::optional<Release> latest(const char* repository)
{
    const net::Response response =
        net::get(std::string("https://api.github.com/repos/") + repository + "/releases/latest", nullptr, nullptr, 15);
    if (!response.ok())
    {
        log::warn("Updates: %s couldn't be checked (%s, HTTP %ld)", repository, response.error.c_str(),
                    response.status);
        return std::nullopt;
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLength(reinterpret_cast<const char*>(response.body.data()), response.body.size()), cJSON_Delete);
    const cJSON* tag = cJSON_GetObjectItemCaseSensitive(root.get(), "tag_name");
    if (!cJSON_IsString(tag))
    {
        log::warn("Updates: invalid release data from %s", repository);
        return std::nullopt;
    }
    const cJSON* body = cJSON_GetObjectItemCaseSensitive(root.get(), "body");
    const std::string notes = cJSON_IsString(body) ? body->valuestring : "";
    const cJSON* asset = nullptr;
    cJSON_ArrayForEach(asset, cJSON_GetObjectItemCaseSensitive(root.get(), "assets"))
    {
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(asset, "name");
        const cJSON* url = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
        const cJSON* size = cJSON_GetObjectItemCaseSensitive(asset, "size");
        const std::string file = cJSON_IsString(name) ? name->valuestring : "";
        // the version from its first digit: NXThemes Installer's tags are "nxt-3.0.2"
        const std::string tagName = tag->valuestring;
        const size_t digit = tagName.find_first_of("0123456789");
        if (file.size() > 4 && file.ends_with(".nro") && cJSON_IsString(url))
            return Release{digit == std::string::npos ? tagName : tagName.substr(digit), url->valuestring,
                           cJSON_IsNumber(size) && size->valuedouble > 0 ? static_cast<uint64_t>(size->valuedouble) : 0,
                           {}, notes};
    }
    return std::nullopt;
}

} // namespace

void check(std::function<void()> found)
{
    jobs::run(jobs::Lane::Images, [found = std::move(found)] {
        std::array<std::optional<Release>, 2> newer;
        if (auto app = latest("ThemezerNX/Themezer-NX"))
        {
            app->current = APP_VERSION;
            log::info("Updates: Themezer-NX %s, the latest %s", APP_VERSION, app->version.c_str());
            if (version::newer(app->version, app->current))
                newer[0] = std::move(app);
        }
        // only one that's here: a missing one is offered as a download (offerInstaller)
        if (const char* installer = installs::installerPath())
        {
            const std::string current = version::ofNro(installer);
            if (auto latestInstaller = !current.empty() ? latest("exelix11/SwitchThemeInjector") : std::nullopt)
            {
                latestInstaller->current = current;
                log::info("Updates: NXThemes Installer %s, the latest %s", current.c_str(),
                            latestInstaller->version.c_str());
                if (version::newer(latestInstaller->version, current))
                    newer[1] = std::move(latestInstaller);
            }
        }
        jobs::post([newer = std::move(newer), found] {
            releases = newer;
            if (found && availableCount() > 0)
                found();
        });
    });
}

const Release* available(Program program)
{
    const auto& release = releases[static_cast<size_t>(program)];
    return release ? &*release : nullptr;
}

#ifdef TZ_DESKTOP
void setTestRelease(Program program, Release release)
{
    releases[static_cast<size_t>(program)] = std::move(release);
}
#endif

int availableCount()
{
    return (releases[0] ? 1 : 0) + (releases[1] ? 1 : 0);
}

void done(Program program)
{
    releases[static_cast<size_t>(program)].reset();
}

std::expected<void, themezer::Error> replace(const std::string& staged, const std::string& path)
{
    // the old one aside first, to come back when the new one can't go in: FAT can't rename over a file
    const std::string old = path + ".old";
    unlink(old.c_str());
    const bool hadOld = access(path.c_str(), F_OK) == 0;
    if (hadOld && rename(path.c_str(), old.c_str()) != 0)
    {
        auto error = themezer::storageError(path);
        unlink(staged.c_str());
        return std::unexpected(error);
    }
    if (rename(staged.c_str(), path.c_str()) != 0)
    {
        auto error = themezer::storageError(path);
        if (hadOld)
            rename(old.c_str(), path.c_str());
        unlink(staged.c_str());
        return std::unexpected(error);
    }
    unlink(old.c_str());
    return {};
}

} // namespace tz::updates
