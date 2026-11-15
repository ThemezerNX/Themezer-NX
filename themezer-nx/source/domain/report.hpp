#pragma once

#include "domain/error.hpp"
#include <string>
#include <vector>

namespace tz::report
{
// Leave room below the API's 64 KiB limit.
constexpr size_t kMaxBody = 60 * 1024;
struct Report
{
    std::string version, firmware, atmosphere;
    themezer::ErrorKind kind;
    std::string message, detail;
    std::vector<std::string> log;
};
std::string toJson(const Report& report);
} // namespace tz::report
