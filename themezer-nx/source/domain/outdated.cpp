#include "domain/outdated.hpp"

#include "domain/names.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <unordered_map>

namespace tz::outdated
{

namespace
{

// IDs in capitals, each once, in the order they come; none for an empty one
std::vector<std::string> canonicals(const std::vector<std::string>& ids)
{
    std::vector<std::string> out;
    for (const std::string& id : ids)
    {
        if (std::string upper = canonical(id); !upper.empty() && std::find(out.begin(), out.end(), upper) == out.end())
            out.push_back(std::move(upper));
    }
    return out;
}

// One file under two names, which FAT's case folding makes of more than ASCII: the same size and time (and the same
// inode where the file system has them)
bool sameFile(const std::string& a, const std::string& b)
{
    struct stat x{}, y{};
    return stat(a.c_str(), &x) == 0 && stat(b.c_str(), &y) == 0 && x.st_size == y.st_size &&
           x.st_mtime == y.st_mtime && (x.st_ino == 0 || x.st_ino == y.st_ino);
}

} // namespace

std::string canonical(std::string_view id)
{
    std::string out(id);
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::optional<int64_t> millisOfId(std::string_view id)
{
    if (!id.starts_with("Themezer:"))
        return std::nullopt;
    const std::string_view tail = id.substr(id.rfind(':') + 1);
    // digits alone: from_chars would take a sign, and stop at a letter without saying so
    if (tail.empty() || !std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    int64_t millis = 0;
    if (std::from_chars(tail.data(), tail.data() + tail.size(), millis).ec != std::errc())
        return std::nullopt;
    return millis;
}

std::optional<int64_t> versionOf(std::optional<int64_t> idMillis, int64_t modifiedSeconds)
{
    if (idMillis)
        return idMillis;
    if (modifiedSeconds <= 0)
        return std::nullopt;
    return modifiedSeconds * 1000;
}

std::optional<int64_t> packVersion(const std::vector<int64_t>& modifiedSeconds)
{
    int64_t oldest = 0;
    for (int64_t seconds : modifiedSeconds)
    {
        if (seconds > 0 && (oldest == 0 || seconds < oldest))
            oldest = seconds;
    }
    return versionOf(std::nullopt, oldest);
}

std::vector<Query> queries(const std::vector<File>& files)
{
    std::vector<Query> out;
    std::vector<int64_t> newest; // the version of each query, as a number until all the copies are in
    std::unordered_map<std::string, size_t> at;
    for (const File& file : files)
    {
        if (std::string_view("TSP").find(file.letter) == std::string_view::npos || !names::hex(file.id) ||
            file.version <= 0)
            continue;
        std::string quickId = std::string(1, file.letter) + canonical(file.id);
        const auto [seen, added] = at.try_emplace(quickId, out.size());
        if (added)
        {
            out.push_back({std::move(quickId), {}});
            newest.push_back(file.version);
        }
        else
        {
            newest[seen->second] = std::max(newest[seen->second], file.version);
        }
    }
    for (size_t i = 0; i < out.size(); ++i)
        out[i].version = std::to_string(newest[i]);
    return out;
}

std::vector<std::string> variables(const std::vector<Query>& queries)
{
    std::vector<std::string> out;
    for (size_t first = 0; first < queries.size(); first += kBatch)
    {
        // IDs are hex and versions digits: nothing in them to escape
        std::string json = "{\"items\":[";
        const size_t end = std::min(queries.size(), first + kBatch);
        for (size_t i = first; i < end; ++i)
        {
            json += i > first ? "," : "";
            json += "{\"quickId\":\"" + queries[i].quickId + "\",\"version\":\"" + queries[i].version + "\"}";
        }
        out.push_back(json + "]}");
    }
    return out;
}

PackDiff diffPack(const std::vector<std::string>& pack, const std::vector<std::string>& folder)
{
    const std::vector<std::string> inPack = canonicals(pack), inFolder = canonicals(folder);
    const auto has = [](const std::vector<std::string>& ids, const std::string& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    PackDiff diff;
    for (const std::string& id : inPack)
    {
        if (!has(inFolder, id))
            diff.added.push_back(id);
    }
    for (const std::string& id : inFolder)
    {
        if (!has(inPack, id))
            diff.removed.push_back(id);
    }
    return diff;
}

bool samePath(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool replace(const std::string& staged, const std::string& path, const std::vector<std::string>& others,
             std::vector<std::string>& replaced, bool (*put)(const std::string&, const std::string&))
{
    if (!put(staged, path))
        return false;
    for (const std::string& other : others)
    {
        if (other == path)
            continue;
        // the same file spelled otherwise is the new one: deleting it would delete the update
        if (samePath(other, path) || sameFile(other, path) || unlink(other.c_str()) == 0)
            replaced.push_back(other);
    }
    return true;
}

std::string followed(const std::string& path, const std::string& from, const std::string& to)
{
    if (from.empty())
        return path;
    if (path == from)
        return to;
    // "/a/b" is not what "/a/bc" is in
    if (path.size() > from.size() && path.starts_with(from) && path[from.size()] == '/')
        return to + path.substr(from.size());
    return path;
}

std::string rebased(std::string path, const std::vector<std::pair<std::string, std::string>>& moves)
{
    for (const auto& [from, to] : moves)
        path = followed(path, from, to);
    return path;
}

} // namespace tz::outdated
