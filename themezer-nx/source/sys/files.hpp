#pragma once

#include "services/themezer.hpp"
#include <span>

namespace tz::files
{
// Complete and close a sibling temporary file before replacing the final name.
std::expected<void, themezer::Error> write(const std::string& path, std::span<const uint8_t> bytes);
}
