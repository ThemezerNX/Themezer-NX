#include "domain/names.hpp"

#include <algorithm>
#include <cctype>

namespace tz::names
{

namespace
{

constexpr int kTargets = 7;
// in themezer::targetLabel's order: Themezer's names for the parts in its file names, and Themezer-NX's own
constexpr const char* kThemezerParts[kTargets] = {"Home Menu",     "Lockscreen", "All Apps", "Settings",
                                                  "Player Select", "User Page",  "News"};
constexpr const char* kOldParts[kTargets] = {"Home Menu",     "Lock Screen", "All Apps", "Settings",
                                             "Player Select", "User Page",   "News"};

bool alnum(std::string_view text)
{
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
}

std::string_view stem(std::string_view name)
{
    const size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

// "<name> by <author>": the last " by " parts them, as a name may have one of its own
void nameAndAuthor(std::string_view text, Parts& parts)
{
    const size_t by = text.rfind(" by ");
    if (by == std::string_view::npos)
    {
        parts.name = text;
        parts.author.clear();
        return;
    }
    parts.name = text.substr(0, by);
    parts.author = text.substr(by + 4);
}

// The last " (…)" text ends in: what's inside, and where it starts.
bool lastGroup(std::string_view text, std::string_view& inside, size_t& start)
{
    if (text.size() < 4 || text.back() != ')')
        return false;
    start = text.rfind(" (");
    if (start == std::string_view::npos)
        return false;
    inside = text.substr(start + 2, text.size() - start - 3);
    return true;
}

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

int fromHex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

std::string percentDecoded(std::string_view text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size() && fromHex(text[i + 1]) >= 0 && fromHex(text[i + 2]) >= 0)
        {
            out += static_cast<char>(fromHex(text[i + 1]) * 16 + fromHex(text[i + 2]));
            i += 2;
        }
        else
        {
            out += text[i];
        }
    }
    return out;
}

} // namespace

bool hex(std::string_view text)
{
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

int target(std::string_view label)
{
    for (int part = 0; part < kTargets; ++part)
    {
        if (label == kThemezerParts[part] || label == kOldParts[part])
            return part;
    }
    return -1;
}

std::optional<FileId> idInFileName(std::string_view fileName)
{
    fileName = fileName.substr(fileName.find_last_of("/\\") + 1);
    const std::string lowered = lower(fileName);
    std::optional<FileId> found;
    for (size_t at = lowered.find("(themezer-"); at != std::string::npos; at = lowered.find("(themezer-", at + 1))
    {
        const size_t end = lowered.find(')', at);
        if (end == std::string::npos)
            break;
        std::string id = lowered.substr(at + 10, end - at - 10);
        if (!id.empty() && std::string_view("tsp").find(id.front()) != std::string::npos)
            id.erase(0, 1);
        if (!hex(id))
            continue;
        std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return std::toupper(c); });
        found = FileId{std::move(id), at > 0 && fileName[at - 1] == ' ' ? at - 1 : at, end + 1};
    }
    return found;
}

bool fromThemezer(std::string_view fileName)
{
    fileName = fileName.substr(fileName.find_last_of("/\\") + 1);
    if (idInFileName(fileName))
        return true;
    Parts parts;
    return (parseFile(fileName, parts) && !parts.id.empty()) ||
           lower(fileName).find("(themezer remote install)") != std::string::npos;
}

bool parseFile(std::string_view fileName, Parts& parts)
{
    parts = {};
    std::string_view rest = stem(fileName);
    std::string_view inside;
    size_t start = 0;
    // A copy suffix is not an ID, for the older NX names either.
    while (lastGroup(rest, inside, start) && !inside.empty() &&
           inside.find_first_not_of("0123456789") == std::string_view::npos)
        rest = rest.substr(0, start);
    bool themezers = false;
    if (auto id = idInFileName(rest))
    {
        themezers = true;
        parts.id = id->id;
        rest = rest.substr(0, id->start);
    }
    else if (lastGroup(rest, inside, start) && lower(inside) == "themezer remote install")
    {
        themezers = true;
        rest = rest.substr(0, start);
    }
    // Themezer-NX's: "(Home Menu-1E93D)", "(splash-1EB6B)", "(remote-splash-<Quick ID>)"
    if (!themezers && lastGroup(rest, inside, start))
    {
        std::string_view id;
        int part = -1;
        if (inside.starts_with("remote-splash-"))
            id = inside.substr(14);
        else if (inside.starts_with("splash-"))
            id = inside.substr(7);
        else if (const size_t dash = inside.find('-');
                 dash != std::string_view::npos && (part = target(inside.substr(0, dash))) >= 0)
            id = inside.substr(dash + 1);
        if (alnum(id))
        {
            parts.id = id;
            parts.v3Name = true;
            parts.target = part;
            nameAndAuthor(rest.substr(0, start), parts);
            return true;
        }
    }
    // the part, with the layout after a comma, ending what's left: "(Home Menu, Chainsaw Layout by KKerim)". A
    // layout's name can have brackets and commas of its own: the group is found by the part's name, the last one.
    size_t best = std::string_view::npos;
    int part = -1;
    for (int t = 0; t < kTargets; ++t)
    {
        for (const char* label : {kThemezerParts[t], kOldParts[t]})
        {
            const std::string alone = std::string(" (") + label + ")";
            const std::string withLayout = std::string(" (") + label + ", ";
            size_t at = std::string_view::npos;
            if (rest.ends_with(alone))
                at = rest.size() - alone.size();
            else if (rest.ends_with(")"))
                at = rest.rfind(withLayout);
            if (at != std::string_view::npos && (best == std::string_view::npos || at > best))
            {
                best = at;
                part = t;
            }
        }
    }
    if (best != std::string_view::npos)
    {
        parts.target = part;
        rest = rest.substr(0, best);
    }
    else if (!themezers)
    {
        return false;
    }
    nameAndAuthor(rest, parts);
    return true;
}

bool parseFolder(std::string_view folder, Parts& parts)
{
    parts = {};
    // Themezer's zip unpacked, "(Themezer-1E93C)", or "(Themezer-P1E93C)" from before its IDs stopped overlapping:
    // the ID says it, with or without an author
    if (auto tag = idInFileName(folder))
    {
        nameAndAuthor(folder.substr(0, tag->start), parts);
        parts.id = tag->id;
        return true;
    }
    // Themezer-NX 3's "(1E93C)": Themezer's IDs are hex, and its packs' names have an author, so "My themes (cafe)" is
    // someone's own folder
    std::string_view inside;
    size_t start = 0;
    if (!lastGroup(folder, inside, start))
        return false;
    nameAndAuthor(folder.substr(0, start), parts);
    if (!hex(inside) || parts.author.empty())
    {
        parts = {};
        return false;
    }
    parts.id = inside;
    parts.v3Name = true;
    return true;
}

std::string safe(std::string_view name)
{
    std::string out;
    bool space = true; // none at the start
    for (const char c : name)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F || c == ' ')
        {
            if (!space)
                out += ' ';
            space = true;
            continue;
        }
        if (std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos)
            continue;
        out += c;
        space = false;
    }
    // FAT drops dots and spaces at a name's end, and a dot first hides a file from the scan
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    const size_t first = out.find_first_not_of(". ");
    return first == std::string::npos ? std::string() : out.substr(first);
}

std::string fromDisposition(std::string_view header)
{
    const std::string lowered = lower(header);
    std::string name;
    // RFC 5987's filename*=UTF-8''<percent-encoded> first, else filename="<as it is>"
    if (const size_t star = lowered.find("filename*="); star != std::string::npos)
    {
        std::string_view value = header.substr(star + 10);
        value = value.substr(0, value.find(';'));
        if (const size_t quotes = value.find("''"); quotes != std::string_view::npos)
            value = value.substr(quotes + 2);
        name = percentDecoded(value);
    }
    else if (const size_t plain = lowered.find("filename="); plain != std::string::npos)
    {
        std::string_view value = header.substr(plain + 9);
        if (!value.empty() && value.front() == '"')
        {
            value.remove_prefix(1);
            value = value.substr(0, value.find('"'));
        }
        else
        {
            value = value.substr(0, value.find(';'));
        }
        name = value;
    }
    return safe(name);
}

std::string themeFile(std::string_view name, std::string_view author, int target, std::string_view id)
{
    const std::string part = target >= 0 && target < kTargets ? std::string(" (") + kThemezerParts[target] + ")" : "";
    return safe(name) + " by " + safe(author) + part +
           (id.empty() ? " (Themezer Remote Install)" : " (Themezer-" + std::string(id) + ")") + ".nxtheme";
}

std::string splashFile(std::string_view name, std::string_view author, std::string_view id)
{
    return safe(name) + " by " + safe(author) +
           (id.empty() ? " (Themezer Remote Install)" : " (Themezer-" + std::string(id) + ")") + ".bmp";
}

std::string plainId(std::string_view name)
{
    const auto tag = idInFileName(name);
    if (!tag)
        return std::string(name);
    // the tag's own bracket: the start idInFileName gives takes in the space before it
    const size_t open = name.rfind('(', tag->end - 1);
    const char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(name[open + 10])));
    if (std::string_view("tsp").find(letter) == std::string_view::npos)
        return std::string(name);
    return std::string(name.substr(0, open)) + "(Themezer-" + tag->id + ")" + std::string(name.substr(tag->end));
}

std::string packFolder(std::string_view name, std::string_view author, std::string_view id)
{
    return safe(name) + " by " + safe(author) + " (Themezer-" + std::string(id) + ")";
}

} // namespace tz::names
