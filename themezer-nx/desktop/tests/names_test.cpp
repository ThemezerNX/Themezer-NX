// make -C desktop test: what theme, splash and pack names say, Themezer's and Themezer-NX's, and download names.

#include "domain/names.hpp"

#include <cassert>
#include <cstdio>
#include <string>

namespace
{

tz::names::Parts file(const char* name, bool expected = true)
{
    tz::names::Parts parts;
    const bool parsed = tz::names::parseFile(name, parts);
    if (parsed != expected)
        std::printf("%s: %s\n", name, parsed ? "parsed" : "not parsed");
    assert(parsed == expected);
    return parts;
}

void is(const tz::names::Parts& parts, const char* name, const char* author, int target, const char* id)
{
    if (parts.name != name || parts.author != author || parts.target != target || parts.id != id)
        std::printf("got \"%s\" by \"%s\", part %d, ID \"%s\"\n", parts.name.c_str(), parts.author.c_str(), parts.target,
                    parts.id.c_str());
    assert(parts.name == name && parts.author == author && parts.target == target && parts.id == id);
}

} // namespace

int main()
{
    using tz::names::fromDisposition;

    // Themezer's downloads, as its Content-Disposition names them
    is(file("Assassin's Creed Odyssey by migush (Home Menu, JAG Layout by migush) (Themezer-TE204).nxtheme"),
       "Assassin's Creed Odyssey", "migush", 0, "E204");
    is(file("Forest Pixel Locksreen by soulsart (Lockscreen) (Themezer-T1E93E).nxtheme"), "Forest Pixel Locksreen",
       "soulsart", 1, "1E93E");
    // a layout with brackets, a comma and a dash of its own; a name with " by " in it
    is(file("Stand by Me by The Band (All Apps, Left-Aligned (v2), Big by Someone) (Themezer-T1A).nxtheme"),
       "Stand by Me", "The Band", 2, "1A");
    is(file("MASHI ORQUESTEY by six-seven-six-one (Themezer-S19CE2).bmp"), "MASHI ORQUESTEY", "six-seven-six-one", -1,
       "19CE2");
    is(file("Yoshi by Nintendo (Player Select) (Themezer Remote Install).nxtheme"), "Yoshi", "Nintendo", 4, "");
    // downloaded before it was published: the part, no ID
    is(file("Draft by Me (News, Clean Layout by X).nxtheme"), "Draft", "Me", 6, "");

    // Themezer-NX's own
    is(file("Forest Pixel Home Menu by soulsart (Home Menu-1E93D).nxtheme"), "Forest Pixel Home Menu", "soulsart", 0,
       "1E93D");
    is(file("Forest Pixel Locksreen by soulsart (Lock Screen-1E93E).nxtheme"), "Forest Pixel Locksreen", "soulsart", 1,
       "1E93E");
    is(file("Pikachu, Eevee by Ash (User Page-AB12C).nxtheme"), "Pikachu, Eevee", "Ash", 5, "AB12C");
    is(file("gatinho by gabriye (splash-1EB6B).bmp"), "gatinho", "gabriye", -1, "1EB6B");
    is(file("Far by Away (remote-splash-R12AB).bmp"), "Far", "Away", -1, "R12AB");

    // someone's own names: what they say, or nothing
    is(file("my theme (Settings).nxtheme"), "my theme", "", 3, "");
    file("cool theme.nxtheme", false);
    file("no parts by anyone (nothing).nxtheme", false);
    file("Theme by Someone (Themezer-Tips).nxtheme", false);

    // packs' folders
    tz::names::Parts pack;
    assert(tz::names::parseFolder("Pixel Forest by soulsart (Themezer-P1E93C)", pack));
    is(pack, "Pixel Forest", "soulsart", -1, "1E93C");
    assert(tz::names::parseFolder("Pixel Forest by soulsart (1E93C)", pack));
    is(pack, "Pixel Forest", "soulsart", -1, "1E93C");
    assert(!tz::names::parseFolder("My themes (old)", pack));
    assert(!tz::names::parseFolder("My themes (cafe)", pack));
    assert(!tz::names::parseFolder("Not hex by Someone (zzz)", pack));
    assert(!tz::names::parseFolder("Zelda", pack));

    // Content-Disposition, plain and RFC 5987, and what the SD card can't hold
    assert(fromDisposition("attachment; filename=\"BotW Forest by migush (Home Menu, JAG Layout by migush) "
                           "(Themezer-TE205).nxtheme\"") ==
           "BotW Forest by migush (Home Menu, JAG Layout by migush) (Themezer-TE205).nxtheme");
    assert(fromDisposition("attachment; filename*=UTF-8''%E3%81%95%E3%81%8F%E3%82%89%20by%20Me%20(Themezer-S1).bmp") ==
           "\xE3\x81\x95\xE3\x81\x8F\xE3\x82\x89 by Me (Themezer-S1).bmp");
    assert(fromDisposition("attachment; FILENAME=a.bmp; size=3") == "a.bmp");
    assert(fromDisposition("attachment; filename=\"../../evil:name?.nxtheme\"") == "evilname.nxtheme");
    assert(fromDisposition("inline") == "");

    // the names it gives when a download doesn't say, read back
    is(file(tz::names::themeFile("Forest", "soulsart", 1, "1E93E").c_str()), "Forest", "soulsart", 1, "1E93E");
    is(file(tz::names::splashFile("cat", "me", "1EB6B").c_str()), "cat", "me", -1, "1EB6B");
    assert(tz::names::parseFolder(tz::names::packFolder("Pixel Forest", "soulsart", "1E93C"), pack));
    is(pack, "Pixel Forest", "soulsart", -1, "1E93C");

    // One provenance reader, independent of case and copy suffixes; the last real tag wins.
    const auto last = tz::names::idInFileName("Old (Themezer-TABC) by me (tHeMeZeR-tDeF) (2).nxtheme");
    assert(last && last->id == "DEF");
    assert(tz::names::fromThemezer("/Themes/Korn (THEMEZER-t1ed6c) (2).nxtheme"));
    assert(tz::names::fromThemezer("Plain (themezer-1ed6c).bmp"));
    assert(tz::names::fromThemezer("Copy by me (Home Menu-1E93D) (2).nxtheme"));
    assert(!tz::names::fromThemezer("/Themes/(Themezer-TABC)/download.nxtheme"));
    for (const char* name : {"Mario (2).nxtheme", "Mario (Themezer-).nxtheme", "Mario (Themezer-TG1).nxtheme",
                             "Mario (12345).bmp", "Mario.nxtheme"})
        assert(!tz::names::fromThemezer(name));
    is(file("Korn by me (Home Menu) (themezer-t1ed6c) (2).nxtheme"), "Korn", "me", 0, "1ED6C");

    std::printf("names: all good\n");
    return 0;
}
