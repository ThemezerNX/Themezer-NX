// Versions of things: compared as numbers ("3.0.10" is after "3.0.9"), and an .nro's own, from the NACP in its
// assets, the version hbmenu shows under it.

#pragma once

#include <string>
#include <string_view>

namespace tz::version
{

// Whether candidate is after current: their numbers compared in turn, a missing one as 0, what comes before the first
// digit left out ("nxt-3.0.2", "v4.8"). False when either has no number.
bool newer(std::string_view candidate, std::string_view current);

// Whether the file is a homebrew app: an .nro has "NRO0" 16 bytes in, where an error page wouldn't.
bool isNro(const char* path);

// The version the .nro at path says it is, or empty when it isn't one or says none.
std::string ofNro(const char* path);

} // namespace tz::version
