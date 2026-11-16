#pragma once

#include "domain/session.hpp"

namespace tz::session
{
std::optional<report::Report> takeLastSession();
void begin();
void frame();
void holdWatch(bool held);
void saveNow();
void saveFatal() noexcept;
void end();
#ifdef TZ_DESKTOP
void simulate(const std::string& kind);
#endif
}
