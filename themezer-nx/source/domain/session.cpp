#include "domain/session.hpp"

#include <charconv>
#include <sstream>

namespace tz::session
{
namespace
{
template <typename T> bool number(std::string_view text, T& value, int base = 10)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return error == std::errc{} && end == text.data() + text.size();
}
}

std::string toText(const Snapshot& snapshot)
{
    std::ostringstream out;
    out << "Themezer-NX session 1\nversion " << snapshot.report.version << "\nlaunch " << snapshot.report.launch
        << "\nmemory " << snapshot.report.memoryUsed << ' ' << snapshot.report.memoryAvailable
        << "\nprogram " << std::hex << snapshot.programId << std::dec << "\nstarted " << snapshot.started
        << "\nstalled " << snapshot.stalledMs << "\nforeground " << snapshot.foreground << "\nlog\n";
    for (const auto& line : snapshot.report.log)
        out << line << '\n';
    return out.str();
}

std::optional<Snapshot> parse(std::string_view text)
{
    if (text.size() > 1024 * 1024 || !text.starts_with("Themezer-NX session 1\n"))
        return std::nullopt;
    Snapshot snapshot;
    std::istringstream in{std::string(text)};
    std::string line;
    std::getline(in, line);
    const auto field = [&in](const char* prefix) -> std::optional<std::string> {
        std::string line;
        if (!std::getline(in, line) || !line.starts_with(prefix))
            return std::nullopt;
        return line.substr(std::char_traits<char>::length(prefix));
    };
    auto version = field("version ");
    auto launch = field("launch ");
    auto memory = field("memory ");
    auto program = field("program ");
    auto started = field("started ");
    auto stalled = field("stalled ");
    auto foreground = field("foreground ");
    if (!version || version->empty() || version->size() > 128 || !launch ||
        (*launch != "application" && *launch != "applet") || !memory || !program || !started || !stalled ||
        !foreground || (*foreground != "0" && *foreground != "1"))
        return std::nullopt;
    const auto gap = memory->find(' ');
    if (gap == std::string::npos || !number(std::string_view(*memory).substr(0, gap), snapshot.report.memoryUsed) ||
        !number(std::string_view(*memory).substr(gap + 1), snapshot.report.memoryAvailable) ||
        !number(*program, snapshot.programId, 16) || !number(*started, snapshot.started) ||
        !number(*stalled, snapshot.stalledMs) || !std::getline(in, line) || line != "log")
        return std::nullopt;
    snapshot.report.version = *version;
    snapshot.report.launch = *launch;
    snapshot.foreground = *foreground == "1";
    while (std::getline(in, line))
    {
        if (line.size() > 8192 || snapshot.report.log.size() == 200)
            return std::nullopt;
        snapshot.report.log.push_back(line);
    }
    return snapshot;
}

std::optional<Crash> crashReport(std::string_view filename, std::string_view text)
{
    // Atmosphere names reports with its local-system-clock timestamp and the runtime host program ID.
    const auto gap = filename.find('_');
    Crash crash;
    if (gap == std::string_view::npos || !filename.ends_with(".log") || filename.size() != gap + 21 ||
        !number(filename.substr(0, gap), crash.timestamp) || !number(filename.substr(gap + 1, 16), crash.programId, 16) ||
        !text.starts_with("Atmosphère Crash Report"))
        return std::nullopt;
    std::istringstream lines{std::string(text.substr(0, 64 * 1024))};
    std::string line;
    bool exception = false, crashedThread = false;
    while (std::getline(lines, line) && crash.detail.size() < 3500)
    {
        if (line == "Exception Info:")
            exception = true;
        else if (line == "Crashed Thread Info:")
        {
            exception = false;
            crashedThread = true;
        }
        else if (!line.empty() && line.front() != ' ')
        {
            exception = false;
            crashedThread = false;
        }
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        const std::string_view trimmed(line.data() + start, line.size() - start);
        if ((exception && (trimmed.starts_with("Type:") || trimmed.starts_with("Address:") ||
                           trimmed.starts_with("Fault Address:"))) ||
            (crashedThread && (trimmed.starts_with("PC:") || trimmed.starts_with("LR:") ||
                              trimmed.starts_with("SP:") || trimmed.starts_with("FAR:") ||
                              trimmed.starts_with("pc:") || trimmed.starts_with("lr:") || trimmed.starts_with("sp:"))))
        {
            if (!crash.detail.empty())
                crash.detail += '\n';
            crash.detail.append(trimmed.substr(0, 256)); // formatted addresses retain Atmosphere's module offsets
        }
    }
    return crash;
}

Ending ending(const Snapshot& snapshot, const std::optional<Crash>& crash)
{
    const bool matched = crash && snapshot.started != 0 && snapshot.programId != 0 && crash->programId == snapshot.programId &&
                         crash->timestamp >= snapshot.started;
    // A deliberate fatal/abort line explains the termination, even when Atmosphere also wrote a crash report.
    if (!snapshot.report.log.empty() && snapshot.report.log.back().find("the app stopped on a fatal error") != std::string::npos)
        return {"fatal error", snapshot.report.log.back() + (matched ? "\n" + crash->detail : "")};
    if (matched)
        return {"crash", crash->detail};
    if (snapshot.foreground && snapshot.stalledMs >= kFreezeMs)
        return {"freeze", "The main loop had stopped for " + std::to_string(snapshot.stalledMs / 1000) + " seconds."};
    return {"unclean exit", "The session ended without a clean exit or a matching crash report."};
}
}
