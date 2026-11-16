#pragma once

#include "services/themezer.hpp"
#include "domain/report.hpp"
namespace tz::report
{
void share(const themezer::Error& error);
void shareLastSession(Report report);
}
