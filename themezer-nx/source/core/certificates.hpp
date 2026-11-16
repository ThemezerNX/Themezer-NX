#pragma once

#include <cstdint>
#include <string_view>

namespace tz::certificates
{
constexpr int64_t kLifetimeSeconds = 1096LL * 24 * 60 * 60;
// The bundle's Mozilla timestamp, or the build date when that header is absent or invalid.
int64_t bundleTime(std::string_view bundle, std::string_view buildDate);
bool outdated(int64_t bundleTime, int64_t now);
} // namespace tz::certificates
