#include "app/app.hpp"

#include "sys/log.hpp"
#include "sys/session.hpp"
#include <exception>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
#ifdef TZ_DESKTOP
    if (argc > 1 && std::string_view(argv[1]) == "--test-https")
        return themezerDesktopTestHttps(argc, argv);
#endif
    std::set_terminate([] {
        std::fputs("Themezer-NX: the app stopped on a fatal error\n", stderr);
        tz::session::saveFatal();
        std::abort();
    });
    // where hbmenu started it from, "sdmc:/switch/themezer-nx.nro", for updating it
    return tz::App::get().run(argc > 0 ? argv[0] : nullptr);
}
