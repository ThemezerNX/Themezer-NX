#include "domain/report.hpp"

#include <cstdio>
#include <string_view>
namespace tz::report
{
namespace
{
std::string quote(std::string_view text)
{
    std::string out = "\"";
    // Cap each escaped string at 8 KiB so fixed fields leave room for recent log lines.
    for (size_t i = 0; i < text.size() && out.size() < 8192;)
    {
        unsigned char c = text[i];
        if (c < 0x20 || c == '"' || c == '\\')
        {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            out += escaped;
            ++i;
        }
        else if (c < 0x80)
        {
            out += text[i++];
        }
        else
        {
            size_t length = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
            bool valid = length && i + length <= text.size();
            for (size_t j = 1; valid && j < length; ++j)
                valid = (static_cast<unsigned char>(text[i + j]) & 0xc0) == 0x80;
            if (valid && length >= 3)
            {
                unsigned char second = text[i + 1];
                valid = !(c == 0xe0 && second < 0xa0) && !(c == 0xed && second >= 0xa0) &&
                        !(c == 0xf0 && second < 0x90) && !(c == 0xf4 && second >= 0x90);
            }
            if (valid)
            {
                out.append(text.substr(i, length));
                i += length;
            }
            else
            {
                out += "\xef\xbf\xbd";
                ++i;
            }
        }
    }
    return out + '"';
}
} // namespace

std::string toJson(const Report& report)
{
    std::string base = "{\"version\":" + quote(report.version) + ",\"firmware\":" + quote(report.firmware) +
                       ",\"atmosphere\":" + quote(report.atmosphere) +
                       ",\"kind\":" + quote(themezer::kindName(report.kind)) + ",\"message\":" + quote(report.message) +
                       ",\"detail\":" + quote(report.detail) + ",\"log\":[";
    std::vector<std::string> lines;
    size_t size = base.size() + 2;
    for (auto it = report.log.rbegin(); it != report.log.rend(); ++it)
    {
        auto line = quote(*it);
        if (size + line.size() + (lines.empty() ? 0 : 1) > kMaxBody)
            break;
        size += line.size() + (lines.empty() ? 0 : 1);
        lines.push_back(std::move(line));
    }
    for (auto it = lines.rbegin(); it != lines.rend(); ++it)
    {
        if (it != lines.rbegin())
            base += ',';
        base += *it;
    }
    return base + "]}";
}
} // namespace tz::report
