#include "services/reporter.hpp"

#include "domain/report.hpp"
#include "sys/log.hpp"
#include "ui/overlays.hpp"
#include <switch.h>
#ifdef TZ_DESKTOP
#include <cstdlib>
#endif

namespace tz::report
{
void share(const themezer::Error& error)
{
    if (!themezer::shareable(error.kind))
        return;
    Report report{APP_VERSION, "unknown", "unknown", error.kind, error.message, error.detail, log::tail()};
    const auto launch = appletGetAppletType();
    report.launch = launch == AppletType_Application || launch == AppletType_SystemApplication ? "application" : "applet";
    const auto memory = log::memory();
    report.memoryUsed = memory.used;
    report.memoryAvailable = memory.available;
    if (R_SUCCEEDED(setsysInitialize()))
    {
        SetSysFirmwareVersion version{};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&version)))
            report.firmware = version.display_version;
        setsysExit();
    }
    if (R_SUCCEEDED(splInitialize()))
    {
        u64 version = 0;
        if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65000), &version)))
            report.atmosphere = std::to_string((version >> 56) & 255) + "." + std::to_string((version >> 48) & 255) +
                                "." + std::to_string((version >> 40) & 255);
        splExit();
    }
    std::string url = "https://api.themezer.net/switch/app/v1/reports";
#ifdef TZ_DESKTOP
    if (const char* override = themezerDesktopReportUrl())
        url = override;
#endif
    jobs::run(jobs::Lane::Network,
              [url, body = toJson(report)] {
                  auto response = net::post(url, body, "application/json");
                  bool ok = response.curlCode == 0 && response.status == 204;
                  jobs::post([ok] { ui::toast(ok ? "Error shared. Thanks!" : "Couldn't share the error"); });
              });
}
} // namespace tz::report
