// Shapes, icons and the logo. Everything takes the Layers' offset and opacity.

#pragma once

#include "gfx/palette.hpp"

#include <cstdint>

namespace tz::gfx
{

// Lucide icons, as on the Themezer website (romfs/icons, made by tools/make_icons.py), drawn tinted.
enum class Icon : uint8_t
{
    Theme,
    Pack,
    Splash,
    Download,
    Install,
    Check,
    CheckCircle,
    Search,
    SearchEmpty,
    Settings,
    Info,
    Refresh,
    Sort,
    Filter,
    Wifi,
    WifiOff,
    Warning,
    Error,
    Trash,
    Eye,
    Square,
    SquareCheck,
    Battery,
    BatteryCharging,
    Keyboard,
    Power,
    Music,
    Volume,
    VolumeOff,
    Queue,
    Save,
    Heart,
    Calendar,
    User,
    Hash,
    Image,
    Flame,
    Bookmark,
    CalendarPlus,
    CalendarCog,
    House,
    Lock,
    LayoutGrid,
    Newspaper,
    Users,
    Layers,
    SdCard,
    Globe,
    MonitorCheck,
    Folder,
    Count,
};

bool loadIcons();
void freeIcons();
// Sharpest at metric::kIcon and metric::kIconLarge, the sizes they're made at.
void icon(Icon which, float x, float y, float size, Color color);

// Themezer's logo (romfs/brand, made by tools/make_brand.mjs), drawn at its own size.
enum class Brand : uint8_t
{
    Logo, // the controller, 40 pixels high
    Icon, // the controller, 128 pixels high
};
void brand(Brand which, float x, float y, float alpha = 1.0f);
float brandWidth(Brand which);
float brandHeight(Brand which);

void rect(float x, float y, float w, float h, Color color);
void gradientV(float x, float y, float w, float h, Color top, Color bottom);
void gradientH(float x, float y, float w, float h, Color left, Color right);
void roundedRect(float x, float y, float w, float h, float radius, Color color);
// Just the outline, width pixels wide, inside the box.
void roundedOutline(float x, float y, float w, float h, float radius, float width, Color color);
void circle(float cx, float cy, float radius, Color color);
void triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color);
void line(float x0, float y0, float x1, float y1, float width, Color color);
// A soft shadow around a rectangle, size pixels wide.
void shadow(float x, float y, float w, float h, float radius, float size, float strength = 1.0f);
void spinner(float cx, float cy, float radius, Color color);
void progressBar(float x, float y, float w, float h, float fraction, Color track, Color fill);
void fill(Color color);

} // namespace tz::gfx
