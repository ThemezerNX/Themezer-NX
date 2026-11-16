#include "core/certificates.hpp"

#include <cstdio>
#include <string>

namespace tz::certificates
{
namespace
{
int monthNumber(const char* name)
{
    constexpr std::string_view months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const auto at = months.find(std::string_view(name, 3));
    return at != std::string_view::npos && at % 3 == 0 ? static_cast<int>(at / 3 + 1) : 0;
}

int64_t timestamp(int year, int month, int day, int hour = 0, int minute = 0, int second = 0)
{
    if (year < 1970 || month < 1 || month > 12 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59)
        return -1;
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day < 1 || day > days[month - 1] + (month == 2 && leap ? 1 : 0))
        return -1;
    // Gregorian civil days since 1970, independent of the host's time zone.
    year -= month <= 2;
    const int era = year / 400;
    const int y = year - era * 400;
    const int d = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t total = era * 146097LL + y * 365 + y / 4 - y / 100 + d - 719468;
    return total * 86400 + hour * 3600 + minute * 60 + second;
}
} // namespace

int64_t bundleTime(std::string_view bundle, std::string_view buildDate)
{
    constexpr std::string_view marker = "## Certificate data from Mozilla as of: ";
    const auto at = bundle.substr(0, 4096).find(marker);
    if (at != std::string_view::npos)
    {
        const std::string line(bundle.substr(at + marker.size(), 48));
        char weekday[4]{}, month[4]{}, zone[4]{};
        int day = 0, hour = 0, minute = 0, second = 0, year = 0;
        if (std::sscanf(line.c_str(), "%3s %3s %d %d:%d:%d %d %3s", weekday, month, &day, &hour, &minute,
                        &second, &year, zone) == 8 && std::string_view(zone) == "GMT")
        {
            const auto value = timestamp(year, monthNumber(month), day, hour, minute, second);
            if (value >= 0)
                return value;
        }
    }
    char month[4]{};
    int day = 0, year = 0;
    const std::string fallback(buildDate);
    if (std::sscanf(fallback.c_str(), "%3s %d %d", month, &day, &year) == 3)
        return timestamp(year, monthNumber(month), day);
    return -1;
}

bool outdated(int64_t bundleTime, int64_t now)
{
    // An unreadable date must not disable verification.
    return bundleTime >= 0 && now > bundleTime && now - bundleTime > kLifetimeSeconds;
}
} // namespace tz::certificates
