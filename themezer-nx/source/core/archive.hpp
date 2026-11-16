// The files inside a .nxtheme: a zip, as Themezer makes them, or a SARC, Yaz0-compressed or not, as SwitchThemeInjector
// makes them.

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace tz::archive
{

// The bytes of the file called name in the archive at path (the name's case doesn't matter), or a diagnostic when the
// archive can't be read, hasn't got it, or has it larger than maxSize: what it says of its files isn't trusted.
std::expected<std::vector<uint8_t>, std::string> readFile(const std::string& path, std::string_view name, size_t maxSize);

// The same, from an archive in memory.
std::expected<std::vector<uint8_t>, std::string> readFile(const std::vector<uint8_t>& archive, std::string_view name,
                                             size_t maxSize);

} // namespace tz::archive
