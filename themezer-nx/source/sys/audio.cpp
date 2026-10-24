#include "sys/audio.hpp"

#include <SDL2/SDL.h>
#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace tz
{

namespace
{

constexpr int kSfxVolume = SDL_MIX_MAXVOLUME / 2;
constexpr int kMusicVolume = SDL_MIX_MAXVOLUME / 4;
// bgm.wav plays its intro once, then loops from here to its end
constexpr double kMusicLoopStart = 16.2;

constexpr const char* kSoundNames[] = {"cursor", "page", "tab", "open", "close", "confirm", "back", "done", "error"};
static_assert(std::size(kSoundNames) == static_cast<size_t>(sfx::Sound::Count));

struct Clip
{
    Uint8* buffer = nullptr;
    Uint32 length = 0;
};

// Like on the 3DS, a new sound replaces the one on its channel: navigation sounds on one, the rest on the other.
struct Channel
{
    const Clip* clip = nullptr;
    Uint32 position = 0;
};

SDL_AudioDeviceID device = 0;
SDL_AudioSpec spec;
Clip sounds[static_cast<size_t>(sfx::Sound::Count)];
Clip music;
Uint32 musicPosition = 0;
Uint32 musicLoopStart = 0;
bool musicOn = false;
bool sfxOn = true;
Channel channels[2];

void mix(void*, Uint8* stream, int length)
{
    SDL_memset(stream, spec.silence, length);
    const auto total = static_cast<Uint32>(length);
    for (Uint32 done = 0; musicOn && music.buffer && done < total;)
    {
        const Uint32 n = std::min(total - done, music.length - musicPosition);
        SDL_MixAudioFormat(stream + done, music.buffer + musicPosition, spec.format, n, kMusicVolume);
        done += n;
        musicPosition += n;
        if (musicPosition >= music.length)
            musicPosition = musicLoopStart;
    }
    for (Channel& channel : channels)
    {
        if (!channel.clip)
            continue;
        const Uint32 n = std::min(total, channel.clip->length - channel.position);
        SDL_MixAudioFormat(stream, channel.clip->buffer + channel.position, spec.format, n, kSfxVolume);
        channel.position += n;
        if (channel.position >= channel.clip->length)
            channel.clip = nullptr;
    }
}

// A WAV converted to the device's format.
Clip load(const char* path)
{
    SDL_AudioSpec wav;
    Uint8* buffer = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV(path, &wav, &buffer, &length))
    {
        std::printf("Loading %s failed: %s\n", path, SDL_GetError());
        return {};
    }
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, wav.format, wav.channels, wav.freq, spec.format, spec.channels, spec.freq) < 0)
    {
        SDL_FreeWAV(buffer);
        return {};
    }
    cvt.len = static_cast<int>(length);
    cvt.buf = static_cast<Uint8*>(SDL_malloc(length * cvt.len_mult));
    Clip clip;
    if (cvt.buf)
    {
        std::memcpy(cvt.buf, buffer, length);
        if (SDL_ConvertAudio(&cvt) == 0)
            clip = {cvt.buf, static_cast<Uint32>(cvt.len_cvt)};
        else
            SDL_free(cvt.buf);
    }
    SDL_FreeWAV(buffer);
    return clip;
}

} // namespace

bool audio::init()
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return false;
    SDL_AudioSpec want{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix;
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &spec, 0);
    if (!device)
    {
        std::printf("Opening audio failed: %s\n", SDL_GetError());
        return false;
    }
    for (size_t i = 0; i < std::size(kSoundNames); ++i)
    {
        char path[64];
        std::snprintf(path, sizeof(path), "romfs:/sfx/%s.wav", kSoundNames[i]);
        sounds[i] = load(path);
    }
    music = load("romfs:/bgm.wav");
    const Uint32 frameSize = SDL_AUDIO_BITSIZE(spec.format) / 8 * spec.channels;
    musicLoopStart = static_cast<Uint32>(kMusicLoopStart * spec.freq) * frameSize;
    if (musicLoopStart >= music.length)
        musicLoopStart = 0;
    SDL_PauseAudioDevice(device, 0);
    return true;
}

void audio::exit()
{
    if (device)
        SDL_CloseAudioDevice(device);
    device = 0;
    for (Clip& clip : sounds)
        SDL_free(clip.buffer);
    SDL_free(music.buffer);
    for (Clip& clip : sounds)
        clip = {};
    music = {};
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void audio::setMusic(bool on)
{
    if (device)
        SDL_LockAudioDevice(device);
    if (on && !musicOn)
        musicPosition = 0;
    musicOn = on;
    if (device)
        SDL_UnlockAudioDevice(device);
}

namespace
{
uint64_t homeFade = 0;   // the fade HOME's volume was changed with, in ns; 0 while it wasn't
float homeVolume = 1.0f; // what it goes back to
} // namespace

void audio::quietHome()
{
    if (appletGetAppletType() != AppletType_LibraryApplet)
        return;
    // the system's own level for HOME under a library applet (0.25 measured on 21.2.0)
    if (float expected = 0;
        R_SUCCEEDED(appletGetExpectedMasterVolume(&expected, nullptr)) && expected > 0 && expected <= 1)
        homeVolume = expected;
    // a fade of 200 ms, in ns: up to 1 s is taken, 2 s refused (qlaunch-bgm's measurements)
    constexpr uint64_t kFade = 200'000'000;
    if (R_FAILED(appletChangeMainAppletMasterVolume(0.0f, kFade)))
    {
        std::printf("HOME's sound: unchanged, its volume couldn't be set\n");
        return;
    }
    homeFade = kFade;
    std::printf("HOME's sound: off, back to %.0f%% on the way out\n", homeVolume * 100);
}

void audio::restoreHome()
{
    if (homeFade == 0)
        return;
    appletChangeMainAppletMasterVolume(homeVolume, homeFade);
    homeFade = 0;
}

void sfx::setEnabled(bool enabled)
{
    sfxOn = enabled;
}

void sfx::play(Sound sound)
{
    const Clip& clip = sounds[static_cast<size_t>(sound)];
    if (!sfxOn || !device || !clip.buffer)
        return;
    SDL_LockAudioDevice(device);
    channels[sound <= Sound::Tab ? 0 : 1] = {&clip, 0};
    SDL_UnlockAudioDevice(device);
}

} // namespace tz
