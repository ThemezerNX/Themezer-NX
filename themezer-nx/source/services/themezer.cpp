#include "services/themezer.hpp"

#include "libs/cJSON.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace tz::themezer
{

namespace
{

constexpr const char* kApi = "https://api.themezer.net/graphql";
constexpr long kJsonTimeout = 20;

constexpr const char* kTargetApiNames[kTargetCount] = {"ResidentMenu", "Entrance",     "Flaunch", "Set",
                                                       "Psl",          "MyPage",       "Notification"};
constexpr const char* kTargetLabels[kTargetCount] = {"Home Menu",        "Lockscreen", "All Apps", "Settings",
                                                     "Player Selection", "User Page",  "News"};
constexpr const char* kSortApiNames[] = {"Trending", "Created", "Updated", "Downloads", "Saves"};
// by sort: highest first, then lowest first
constexpr const char* kSortLabels[][2] = {
    {"Trending now", "Least trending"},     {"Newest first", "Oldest first"},
    {"Recently updated", "Least recently updated"}, {"Most downloaded", "Least downloaded"},
    {"Most saved", "Least saved"},
};

#define THEME_FIELDS                                                                                                  \
    "hexId creator{username} name description createdAt updatedAt downloadCount saveCount target "                   \
    "screenshotThumbHash screenshotPreview{hdUrl thumbUrl} downloadUrl"
#define SPLASH_FIELDS                                                                                                 \
    "hexId creator{username} name description createdAt updatedAt downloadCount saveCount previewThumbHash "          \
    "preview{hdUrl thumbUrl} downloadUrl quickId"
#define PACK_FIELDS                                                                                                   \
    "hexId creator{username} name description updatedAt downloadCount saveCount collageThumbHash "                   \
    "collagePreview{hdUrl thumbUrl} themes{" THEME_FIELDS "}"
#define PAGE_ARGS "paginationArgs:$paginationArgs,sort:$sort,order:$order,query:$query,isNSFW:false"
#define PAGE_INFO "pageInfo{itemCount limit page pageCount}"

constexpr const char* kPacksQuery =
    "query Packs($paginationArgs:PaginationInput,$sort:ItemSort,$order:SortOrder,$query:String){switch{packs(" PAGE_ARGS
    "){nodes{" PACK_FIELDS "}" PAGE_INFO "}}}";
constexpr const char* kThemesQuery =
    "query Themes($target:Target,$isPartOfPack:Boolean,$paginationArgs:PaginationInput,$sort:ItemSort,$order:SortOrder,"
    "$query:String){switch{themes(target:$target,isPartOfPack:$isPartOfPack," PAGE_ARGS "){nodes{" THEME_FIELDS " pack{hexId name creator{username}}}" PAGE_INFO "}}}";
constexpr const char* kSplashesQuery =
    "query Splashes($paginationArgs:PaginationInput,$sort:ItemSort,$order:SortOrder,$query:String){switch{splashes(" PAGE_ARGS
    "){nodes{" SPLASH_FIELDS "}" PAGE_INFO "}}}";
constexpr const char* kLookupQuery =
    "query LookupByQuickId($quickId:String!){switch{lookupByQuickId(quickId:$quickId){__typename "
    "... on SwitchPack{" PACK_FIELDS "} "
    "... on SwitchTheme{" THEME_FIELDS " pack{hexId name creator{username}}} "
    "... on SwitchSplash{" SPLASH_FIELDS "} "
    "... on SwitchRemoteInstallTheme{author createdAt downloadUrl name quickId target} "
    "... on SwitchRemoteInstallSplash{author createdAt downloadUrl name quickId}}}}";

struct JsonDeleter
{
    void operator()(cJSON* json) const { cJSON_Delete(json); }
};
using Json = std::unique_ptr<cJSON, JsonDeleter>;

cJSON* get(const cJSON* object, const char* name)
{
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

std::string string(const cJSON* object, const char* name)
{
    const cJSON* item = get(object, name);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

int number(const cJSON* object, const char* name)
{
    const cJSON* item = get(object, name);
    return cJSON_IsNumber(item) ? item->valueint : 0;
}

Preview preview(const cJSON* object, const char* field, const char* hashField)
{
    const cJSON* images = get(object, field);
    return {string(images, "hdUrl"), string(images, "thumbUrl"), string(object, hashField)};
}

void common(Item& item, const cJSON* node)
{
    item.id = string(node, "hexId");
    item.name = string(node, "name");
    item.creator = string(get(node, "creator"), "username");
    item.description = string(node, "description");
    item.createdAt = string(node, "createdAt");
    item.updatedAt = string(node, "updatedAt");
    item.downloadUrl = string(node, "downloadUrl");
    item.downloads = number(node, "downloadCount");
    item.saves = number(node, "saveCount");
}

Item parseTheme(const cJSON* node)
{
    Item item;
    item.kind = Kind::Themes;
    common(item, node);
    item.target = targetFromApi(string(node, "target").c_str());
    item.preview = preview(node, "screenshotPreview", "screenshotThumbHash");
    item.quickId = "T" + item.id;
    if (const cJSON* pack = get(node, "pack"); cJSON_IsObject(pack))
    {
        item.packId = string(pack, "hexId");
        item.packName = string(pack, "name");
        item.packCreator = string(get(pack, "creator"), "username");
    }
    return item;
}

Item parseSplash(const cJSON* node)
{
    Item item;
    item.kind = Kind::Splashes;
    common(item, node);
    item.quickId = string(node, "quickId");
    item.preview = preview(node, "preview", "previewThumbHash");
    return item;
}

Item parsePack(const cJSON* node)
{
    Item item;
    item.kind = Kind::Packs;
    common(item, node);
    item.quickId = "P" + item.id;
    item.preview = preview(node, "collagePreview", "collageThumbHash");
    const cJSON* theme = nullptr;
    cJSON_ArrayForEach(theme, get(node, "themes"))
    {
        Item child = parseTheme(theme);
        child.packId = item.id;
        child.packName = item.name;
        child.packCreator = item.creator;
        item.themes.push_back(std::move(child));
    }
    return item;
}

Item parseRemote(const cJSON* node, bool splash)
{
    Item item;
    item.kind = splash ? Kind::Splashes : Kind::Themes;
    item.remote = true;
    item.name = string(node, "name");
    item.creator = string(node, "author");
    item.createdAt = string(node, "createdAt");
    item.updatedAt = item.createdAt;
    item.downloadUrl = string(node, "downloadUrl");
    item.quickId = string(node, "quickId");
    item.id = item.quickId;
    if (!splash)
        item.target = targetFromApi(string(node, "target").c_str());
    return item;
}

// The API's own words for what went wrong, when it says.
std::string apiError(const cJSON* root)
{
    if (const cJSON* message = get(root, "message"); cJSON_IsNumber(get(root, "statusCode")) && cJSON_IsString(message))
        return message->valuestring;
    if (const cJSON* errors = get(root, "errors"); cJSON_IsArray(errors))
    {
        const std::string message = string(cJSON_GetArrayItem(errors, 0), "message");
        return message.empty() ? "The server couldn't answer the request." : message;
    }
    return {};
}

Error connectionError(const net::Response& response)
{
    Error error;
    error.cancelled = response.cancelled();
    if (response.curlCode != 0)
    {
        error.message = "Couldn't reach Themezer. Check the console's internet connection and try again.";
        error.detail = response.error;
    }
    else
    {
        error.message = "Themezer's server had a problem (HTTP " + std::to_string(response.status) + "). Try again later.";
    }
    return error;
}

std::string escapeJson(const std::string& text)
{
    Json value(cJSON_CreateString(text.c_str()));
    char* printed = value ? cJSON_PrintUnformatted(value.get()) : nullptr;
    std::string out = printed ? printed : "\"\"";
    cJSON_free(printed);
    return out;
}

std::expected<Json, Error> request(const char* query, const std::string& variables, const CancelToken& token)
{
    const std::string url = std::string(kApi) + "?query=" + net::escape(query) + "&variables=" + net::escape(variables);
    net::Response response = net::get(url, &token, nullptr, kJsonTimeout);
    if (response.curlCode != 0)
        return std::unexpected(connectionError(response));
    Json root(cJSON_ParseWithLength(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
    if (!root)
        return std::unexpected(connectionError(response));
    if (std::string message = apiError(root.get()); !message.empty())
        return std::unexpected(Error{std::move(message), {}, false, false, true});
    if (!response.ok())
        return std::unexpected(connectionError(response));
    return root;
}

bool makeFolders(const std::string& path)
{
    for (size_t slash = path.find('/', 1); slash != std::string::npos; slash = path.find('/', slash + 1))
    {
        const std::string folder = path.substr(0, slash);
        if (mkdir(folder.c_str(), 0777) != 0 && errno != EEXIST)
            return false;
    }
    return true;
}

} // namespace

const char* targetLabel(int target)
{
    return target >= 0 && target < kTargetCount ? kTargetLabels[target] : "Unknown";
}

int targetFromApi(const char* name)
{
    for (int i = 0; i < kTargetCount; ++i)
    {
        if (std::strcmp(name, kTargetApiNames[i]) == 0)
            return i;
    }
    return -1;
}

const char* sortLabel(Sort sort, bool ascending)
{
    return kSortLabels[static_cast<size_t>(sort)][ascending ? 1 : 0];
}

std::expected<Page, Error> fetchPage(const Query& query, const CancelToken& token)
{
    std::string variables = "{\"paginationArgs\":{\"page\":" + std::to_string(query.page) +
                            ",\"limit\":" + std::to_string(query.limit) + "},\"sort\":\"" +
                            kSortApiNames[static_cast<size_t>(query.sort)] + "\",\"order\":\"" +
                            (query.ascending ? "Asc" : "Desc") + "\",\"query\":" +
                            (query.search.empty() ? std::string("null") : escapeJson(query.search));
    const char* text = kPacksQuery;
    const char* field = "packs";
    if (query.kind == Kind::Themes)
    {
        text = kThemesQuery;
        field = "themes";
        variables += ",\"target\":";
        variables += query.target >= 0 && query.target < kTargetCount
                         ? "\"" + std::string(kTargetApiNames[query.target]) + "\""
                         : std::string("null");
        variables += ",\"isPartOfPack\":";
        variables += !query.inPack ? "null" : *query.inPack ? "true" : "false";
    }
    else if (query.kind == Kind::Splashes)
    {
        text = kSplashesQuery;
        field = "splashes";
    }
    variables += "}";

    auto root = request(text, variables, token);
    if (!root)
        return std::unexpected(root.error());
    const cJSON* list = get(get(get(root->get(), "data"), "switch"), field);
    const cJSON* info = get(list, "pageInfo");
    if (!cJSON_IsObject(list) || !cJSON_IsObject(info))
        return std::unexpected(Error{"Themezer's answer wasn't what the app expected. Try again later.", {}, false, false});

    Page page;
    page.page = number(info, "page");
    page.pageCount = number(info, "pageCount");
    page.itemCount = number(info, "itemCount");
    const cJSON* node = nullptr;
    cJSON_ArrayForEach(node, get(list, "nodes"))
    {
        if (query.kind == Kind::Packs)
            page.items.push_back(parsePack(node));
        else if (query.kind == Kind::Themes)
            page.items.push_back(parseTheme(node));
        else
            page.items.push_back(parseSplash(node));
    }
    return page;
}

std::expected<Item, Error> lookupQuickId(const std::string& quickId, const CancelToken& token)
{
    auto root = request(kLookupQuery, "{\"quickId\":" + escapeJson(quickId) + "}", token);
    // the API answers a Quick ID it can't read with an error of its own
    if (!root && root.error().fromApi)
        return std::unexpected(Error{"Nothing has the Quick ID " + quickId + ". Check it on themezer.net.",
                                     root.error().message, false, true});
    if (!root)
        return std::unexpected(root.error());
    const cJSON* found = get(get(get(root->get(), "data"), "switch"), "lookupByQuickId");
    if (!cJSON_IsObject(found))
        return std::unexpected(Error{"Nothing has the Quick ID " + quickId + ". Check it on themezer.net.", {}, false, true});
    const std::string type = string(found, "__typename");
    if (type == "SwitchPack")
        return parsePack(found);
    if (type == "SwitchTheme")
        return parseTheme(found);
    if (type == "SwitchSplash")
        return parseSplash(found);
    if (type == "SwitchRemoteInstallTheme" || type == "SwitchRemoteInstallSplash")
        return parseRemote(found, type == "SwitchRemoteInstallSplash");
    return std::unexpected(Error{"This Quick ID is for something the Switch app can't install.", type, false, true});
}

std::expected<void, Error> download(const std::string& url, const std::string& path, const CancelToken& token,
                                    net::Progress* progress)
{
    net::Response response = net::get(url, &token, progress);
    if (response.curlCode != 0)
        return std::unexpected(connectionError(response));
    // a theme or splash comes as a file; JSON is the API saying what went wrong
    if (!response.ok() || response.contentType.find("json") != std::string::npos)
    {
        Json root(cJSON_ParseWithLength(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
        std::string message = root ? apiError(root.get()) : std::string();
        if (!message.empty())
            return std::unexpected(Error{std::move(message), {}, false, false});
        return std::unexpected(connectionError(response));
    }
    if (!makeFolders(path))
        return std::unexpected(Error{"Couldn't make a folder for it on the SD card.", path, false, false});
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file)
        return std::unexpected(Error{"Couldn't write it to the SD card.", path, false, false});
    const size_t written = std::fwrite(response.body.data(), 1, response.body.size(), file);
    if (std::fclose(file) != 0 || written != response.body.size())
    {
        unlink(path.c_str());
        return std::unexpected(Error{"Couldn't write it to the SD card. Is it full?", path, false, false});
    }
    return {};
}

std::string dateOf(const std::string& iso)
{
    return iso.substr(0, iso.find('T'));
}

} // namespace tz::themezer
