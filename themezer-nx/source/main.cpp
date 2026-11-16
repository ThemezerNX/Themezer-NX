#include "app/app.hpp"

int main(int argc, char** argv)
{
#ifdef TZ_DESKTOP
    if (argc > 1 && std::string_view(argv[1]) == "--test-https")
        return themezerDesktopTestHttps(argc, argv);
#endif
    // where hbmenu started it from, "sdmc:/switch/themezer-nx.nro", for updating it
    return tz::App::get().run(argc > 0 ? argv[0] : nullptr);
}
