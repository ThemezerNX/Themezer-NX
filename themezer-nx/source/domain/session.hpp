#pragma once

#include "domain/report.hpp"
#include <optional>
#include <string_view>

namespace tz::session
{
constexpr uint64_t kFreezeMs = 10000;
struct Snapshot
{
    report::Report report{};
    uint64_t programId = 0, started = 0, stalledMs = 0;
    bool foreground = true;
};
std::string toText(const Snapshot& snapshot);
std::optional<Snapshot> parse(std::string_view text);
struct Crash
{
    uint64_t timestamp = 0, programId = 0;
    std::string detail;
};
std::optional<Crash> crashReport(std::string_view filename, std::string_view text);
struct Ending
{
    std::string kind, detail;
};
Ending ending(const Snapshot& snapshot, const std::optional<Crash>& crash);
}
