#include "app/app.hpp"

int main(int argc, char** argv)
{
    // where hbmenu started it from, "sdmc:/switch/themezer-nx.nro", for updating it
    return tz::App::get().run(argc > 0 ? argv[0] : nullptr);
}
