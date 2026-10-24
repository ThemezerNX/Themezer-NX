// The UI's sounds and the background music (romfs/sfx and romfs/bgm.wav, made by tools/make_audio.sh from
// Themezer-3DS's), mixed by one SDL audio device.

#pragma once

#include <cstdint>

namespace tz::audio
{

bool init();
void exit();
void setMusic(bool on);

// The HOME Menu's own sound (its background music, with the qlaunch BGM mod) silent under the app's, faded out in
// 200 ms, while the app runs in a library applet's slot (hbmenu's): as an application, HOME is suspended, its sound
// with it. On the way out it's faded back to what the system gives HOME under a library applet, as under the real
// Album, or full when that can't be read. The system resets it anyway when the slot closes.
void quietHome();
void restoreHome();

} // namespace tz::audio

namespace tz::sfx
{

enum class Sound : uint8_t
{
    // The cursor's sounds share a channel: each one cuts the one before, however fast the cursor goes.
    Cursor,
    Page, // a page of a list further
    Tab,
    // The others share the other channel, so of two in one frame the later one plays: a Dialog's Open after the
    // Confirm that opened it.
    Open,
    Close,
    Confirm,
    Back, // a step back, or something taken away
    Done,
    Error,
    Count,
};

void setEnabled(bool enabled);
void play(Sound sound);

} // namespace tz::sfx
