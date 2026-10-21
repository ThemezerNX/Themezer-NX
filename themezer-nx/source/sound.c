#include "sound.h"
#undef MakeMenu
#include <switch.h>
#include <string.h>
#include <stdio.h>
#include "libs/cJSON.h"

#define SFX_VOLUME (SDL_MIX_MAXVOLUME / 2)
#define MUSIC_VOLUME (SDL_MIX_MAXVOLUME / 4)
// bgm.wav plays its intro once, then loops from here to its end
#define MUSIC_LOOP_START_SECONDS 16.2
#define MAX_MENU_DEPTH 16
#define SETTINGS_PATH "/Themes/ThemezerNX/settings.json"

static const char *sfxNames[SfxCount] = {"cursor", "page", "tab", "open", "close", "confirm", "back", "done", "error"};

typedef struct {
    Uint8 *buf;
    Uint32 len;
} Sound_t;

// Like on the 3DS, a new sound replaces the one on its channel: navigation sounds on one, the rest on the other
typedef struct {
    const Sound_t *sound;
    Uint32 pos;
} Channel_t;

typedef struct {
    func_ptr buttonHandler, runEveryFrame;
    int offset, highlight;
    unsigned played;
} MenuSound_t;

static Sound_t sounds[SfxCount];
static Sound_t music;
static Uint32 musicPos = 0, musicLoopStart = 0;
static bool musicOn = true, sfxOn = true;
static Channel_t channels[2];
static SDL_AudioDeviceID device = 0;
static SDL_AudioSpec spec;
static unsigned playCount = 0;
static MenuSound_t menus[MAX_MENU_DEPTH];
static int depth = -1;

static void MixCallback(void *userdata, Uint8 *stream, int len){
    SDL_memset(stream, spec.silence, len);
    for (Uint32 done = 0; musicOn && music.buf && done < (Uint32)len;){
        Uint32 n = SDL_min((Uint32)len - done, music.len - musicPos);
        SDL_MixAudioFormat(stream + done, music.buf + musicPos, spec.format, n, MUSIC_VOLUME);
        done += n;
        musicPos += n;
        if (musicPos >= music.len)
            musicPos = musicLoopStart;
    }
    for (int i = 0; i < 2; i++){
        Channel_t *ch = &channels[i];
        if (!ch->sound)
            continue;

        Uint32 n = SDL_min((Uint32)len, ch->sound->len - ch->pos);
        SDL_MixAudioFormat(stream, ch->sound->buf + ch->pos, spec.format, n, SFX_VOLUME);
        ch->pos += n;
        if (ch->pos >= ch->sound->len)
            ch->sound = NULL;
    }
}

static void LoadSound(Sound_t *out, const char *path){

    SDL_AudioSpec wav;
    Uint8 *buf;
    Uint32 len;
    if (!SDL_LoadWAV(path, &wav, &buf, &len)){
        printf("Loading %s failed: %s\n", path, SDL_GetError());
        return;
    }

    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, wav.format, wav.channels, wav.freq, spec.format, spec.channels, spec.freq) < 0){
        SDL_FreeWAV(buf);
        return;
    }

    cvt.len = len;
    cvt.buf = SDL_malloc(len * cvt.len_mult);
    if (cvt.buf){
        memcpy(cvt.buf, buf, len);
        if (SDL_ConvertAudio(&cvt) == 0){
            out->buf = cvt.buf;
            out->len = cvt.len_cvt;
        }
        else
            SDL_free(cvt.buf);
    }
    SDL_FreeWAV(buf);
}

static void LoadSettings(void){
    FILE *fp = fopen(SETTINGS_PATH, "rb");
    if (!fp)
        return;

    char buf[256];
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    cJSON *item = cJSON_GetObjectItem(root, "music");
    if (cJSON_IsBool(item))
        musicOn = cJSON_IsTrue(item);
    item = cJSON_GetObjectItem(root, "sfx");
    if (cJSON_IsBool(item))
        sfxOn = cJSON_IsTrue(item);
    cJSON_Delete(root);
}

static void SaveSettings(void){
    FILE *fp = fopen(SETTINGS_PATH, "wb");
    if (!fp)
        return;

    fprintf(fp, "{\"music\":%s,\"sfx\":%s}\n", musicOn ? "true" : "false", sfxOn ? "true" : "false");
    fclose(fp);
}

bool SoundGetMusic(void){
    return musicOn;
}

bool SoundGetSfx(void){
    return sfxOn;
}

void SoundSetMusic(bool on){
    if (device)
        SDL_LockAudioDevice(device);
    musicOn = on;
    musicPos = 0;
    if (device)
        SDL_UnlockAudioDevice(device);
    SaveSettings();
}

void SoundSetSfx(bool on){
    sfxOn = on;
    SaveSettings();
}

void SoundInit(void){
    LoadSettings();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        return;

    SDL_AudioSpec want = {.freq = 48000, .format = AUDIO_S16SYS, .channels = 2, .samples = 1024, .callback = MixCallback};
    device = SDL_OpenAudioDevice(NULL, 0, &want, &spec, 0);
    if (!device){
        printf("Opening audio failed: %s\n", SDL_GetError());
        return;
    }

    for (int i = 0; i < SfxCount; i++){
        char path[64];
        snprintf(path, sizeof(path), "romfs:/sfx/%s.wav", sfxNames[i]);
        LoadSound(&sounds[i], path);
    }

    LoadSound(&music, "romfs:/bgm.wav");
    Uint32 frameSize = SDL_AUDIO_BITSIZE(spec.format) / 8 * spec.channels;
    musicLoopStart = (Uint32)(MUSIC_LOOP_START_SECONDS * spec.freq) * frameSize;
    if (musicLoopStart >= music.len)
        musicLoopStart = 0;

    SDL_PauseAudioDevice(device, 0);
}

void SoundExit(void){
    if (device)
        SDL_CloseAudioDevice(device);
    device = 0;

    for (int i = 0; i < SfxCount; i++){
        SDL_free(sounds[i].buf);
        sounds[i].buf = NULL;
    }
    SDL_free(music.buf);
    music.buf = NULL;

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SoundPlay(Sfx_t sfx){
    playCount++;
    if (!sfxOn || !device || !sounds[sfx].buf)
        return;

    SDL_LockAudioDevice(device);
    channels[(sfx <= SfxTab) ? 0 : 1] = (Channel_t){&sounds[sfx], 0};
    SDL_UnlockAudioDevice(device);
}

static int SelectedHighlight(Context_t *ctx){
    if (ctx->selected->type == ListViewType)
        return ((ListView_t *)ctx->selected->item)->highlight;
    if (ctx->selected->type == ListGridType)
        return ((ListGrid_t *)ctx->selected->item)->highlight;
    return 0;
}

static int SoundButtonHandler(Context_t *ctx){
    MenuSound_t *menu = &menus[depth];
    if (ctx->kDown & HidNpadButton_B)
        SoundPlay(SfxBack);

    return menu->buttonHandler ? menu->buttonHandler(ctx) : 0;
}

// JAGL doesn't report cursor moves (list scrolling or jumps between boxes), so compare with the last frame
static int SoundFrame(Context_t *ctx){
    MenuSound_t *menu = &menus[depth];

    if (ctx->kDown & HidNpadButton_A)
        SoundPlay(SfxConfirm);
    else if (menu->offset >= 0 && playCount == menu->played && (ctx->curOffset != menu->offset || SelectedHighlight(ctx) != menu->highlight))
        SoundPlay(SfxCursor);

    int res = menu->runEveryFrame ? menu->runEveryFrame(ctx) : 0;

    // Taken after the menu's own frame callback, so content it swaps in doesn't count as a move
    menu->offset = ctx->curOffset;
    menu->highlight = SelectedHighlight(ctx);
    menu->played = playCount;
    return res;
}

Context_t SoundMakeMenu(ShapeLinker_t *in, func_ptr buttonHandler, func_ptr runEveryFrame){
    if (depth + 1 >= MAX_MENU_DEPTH)
        return MakeMenu(in, buttonHandler, runEveryFrame);

    // A nested menu is a popup or a screen opened from another; skip its open/close when a sound (error, done, back) already says it
    bool nested = depth >= 0;
    if (nested && playCount == menus[depth].played)
        SoundPlay(SfxOpen);

    depth++;
    menus[depth] = (MenuSound_t){buttonHandler, runEveryFrame, -1, -1, playCount};
    Context_t ctx = MakeMenu(in, SoundButtonHandler, SoundFrame);
    unsigned played = menus[depth].played;
    depth--;

    if (nested && playCount == played)
        SoundPlay(SfxClose);

    return ctx;
}
