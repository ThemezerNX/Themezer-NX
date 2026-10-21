// The desktop build's libnx (see include/switch.h), and a script runner for screenshots:
//
//     THEMEZER_SCRIPT=script.txt THEMEZER_SHOTS=out ./themezer-nx-desktop
//
// SDL_VIDEODRIVER=dummy runs it without a window; THEMEZER_SD=folder puts the SD card's /Themes and /switch there
// (an empty NXThemesInstaller.nro in folder/switch lets installing be tried); THEMEZER_EXIT_AFTER_SCRIPT=1 quits when
// the script ends.
//
// A script has a command a line: "wait <frames>", "press <key>", "hold <key> <frames>", "tap <x> <y>",
// "type <text>" (what the next keyboard returns), "shot <name>" (out/<name>.png) and "quit". Keys: a b x y l r zl zr
// plus minus up down left right rup rdown. Without a script the keyboard is the controller: arrows, Z = A, X = B,
// C = X, V = Y, Q = L, E = R, 1 = ZL, 3 = ZR, = = +, - = -; the mouse is the touch screen.

#include <switch.h>
#include <mbedtls/base64.h>

#undef SDL_RenderPresent
#undef mkdir
#undef access
#undef opendir
#undef unlink
#undef fopen
#undef remove
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

bool quitting = false;
u64 scriptHeld = 0;
bool scriptTouch = false;
float touchX = 0, touchY = 0;
std::string typed;
bool haveTyped = false;
std::string pendingShot;

struct Step
{
    std::string command;
    std::string argument;
    int frames = 0;
};
std::deque<Step> script;
bool scripted = false;
int waitFrames = 0;
int holdFrames = 0;
u64 holdKey = 0;
int tapFrames = 0;

const std::map<std::string, u64> kKeys = {
    {"a", HidNpadButton_A},       {"b", HidNpadButton_B},          {"x", HidNpadButton_X},
    {"y", HidNpadButton_Y},       {"l", HidNpadButton_L},          {"r", HidNpadButton_R},
    {"zl", HidNpadButton_ZL},     {"zr", HidNpadButton_ZR},        {"plus", HidNpadButton_Plus},
    {"minus", HidNpadButton_Minus}, {"up", HidNpadButton_Up},     {"down", HidNpadButton_Down},
    {"left", HidNpadButton_Left}, {"right", HidNpadButton_Right},  {"rup", HidNpadButton_StickRUp},
    {"rdown", HidNpadButton_StickRDown},
};

void loadScript()
{
    const char* path = std::getenv("THEMEZER_SCRIPT");
    if (!path)
        return;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream words(line);
        Step step;
        words >> step.command;
        if (step.command.empty() || step.command[0] == '#')
            continue;
        if (step.command == "type")
        {
            std::getline(words, step.argument);
            if (!step.argument.empty() && step.argument[0] == ' ')
                step.argument.erase(0, 1);
        }
        else if (step.command == "tap")
        {
            float x, y;
            words >> x >> y;
            step.argument = std::to_string(x) + " " + std::to_string(y);
        }
        else
        {
            words >> step.argument;
            if (step.command == "wait")
                step.frames = std::atoi(step.argument.c_str());
            else if (step.command == "hold")
                words >> step.frames;
        }
        script.push_back(step);
    }
    scripted = true;
}

// One frame of the script: what's held this frame.
void runScript()
{
    scriptHeld = 0;
    if (tapFrames > 0 && --tapFrames == 0)
        scriptTouch = false;
    if (holdFrames > 0)
    {
        --holdFrames;
        scriptHeld = holdKey;
        return;
    }
    if (waitFrames > 0)
    {
        --waitFrames;
        return;
    }
    while (!script.empty())
    {
        Step step = script.front();
        script.pop_front();
        if (step.command == "wait")
        {
            waitFrames = step.frames;
            return;
        }
        if (step.command == "press" || step.command == "hold")
        {
            auto it = kKeys.find(step.argument);
            if (it != kKeys.end())
                scriptHeld = it->second;
            holdFrames = step.command == "hold" ? step.frames : 0;
            holdKey = scriptHeld;
            // a press is down for a frame, then up for one
            waitFrames = 1;
            return;
        }
        if (step.command == "tap")
        {
            std::sscanf(step.argument.c_str(), "%f %f", &touchX, &touchY);
            scriptTouch = true;
            tapFrames = 2;
            waitFrames = 2;
            return;
        }
        if (step.command == "type")
        {
            typed = step.argument;
            haveTyped = true;
            continue;
        }
        if (step.command == "shot")
        {
            pendingShot = step.argument;
            return;
        }
        if (step.command == "quit")
        {
            quitting = true;
            return;
        }
    }
    // the script's done: stay for a look unless it's run headless
    if (std::getenv("THEMEZER_EXIT_AFTER_SCRIPT"))
        quitting = true;
}

u64 keyboardButtons()
{
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    u64 held = 0;
    const std::pair<SDL_Scancode, u64> map[] = {
        {SDL_SCANCODE_UP, HidNpadButton_Up},       {SDL_SCANCODE_DOWN, HidNpadButton_Down},
        {SDL_SCANCODE_LEFT, HidNpadButton_Left},   {SDL_SCANCODE_RIGHT, HidNpadButton_Right},
        {SDL_SCANCODE_Z, HidNpadButton_A},         {SDL_SCANCODE_RETURN, HidNpadButton_A},
        {SDL_SCANCODE_X, HidNpadButton_B},         {SDL_SCANCODE_BACKSPACE, HidNpadButton_B},
        {SDL_SCANCODE_C, HidNpadButton_X},         {SDL_SCANCODE_V, HidNpadButton_Y},
        {SDL_SCANCODE_Q, HidNpadButton_L},         {SDL_SCANCODE_E, HidNpadButton_R},
        {SDL_SCANCODE_1, HidNpadButton_ZL},        {SDL_SCANCODE_3, HidNpadButton_ZR},
        {SDL_SCANCODE_EQUALS, HidNpadButton_Plus}, {SDL_SCANCODE_MINUS, HidNpadButton_Minus},
        {SDL_SCANCODE_PAGEUP, HidNpadButton_StickRUp}, {SDL_SCANCODE_PAGEDOWN, HidNpadButton_StickRDown},
    };
    for (const auto& [code, button] : map)
    {
        if (keys[code])
            held |= button;
    }
    return held;
}

std::vector<std::vector<unsigned char>> fonts(6);

} // namespace

const char* desktopSd(const char* path)
{
    static thread_local std::string mapped;
    const char* sd = std::getenv("THEMEZER_SD");
    if (!sd || !path || (std::strncmp(path, "/Themes", 7) != 0 && std::strncmp(path, "/switch", 7) != 0))
        return path;
    mapped = std::string(sd) + path;
    return mapped.c_str();
}

extern "C" void desktopPresent(SDL_Renderer* renderer)
{
    if (!pendingShot.empty())
    {
        const char* folder = std::getenv("THEMEZER_SHOTS");
        const std::string path = std::string(folder ? folder : ".") + "/" + pendingShot + ".png";
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
        if (surface && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ABGR8888, surface->pixels,
                                            surface->pitch) == 0)
            IMG_SavePNG(surface, path.c_str());
        SDL_FreeSurface(surface);
        std::printf("shot %s\n", path.c_str());
        pendingShot.clear();
    }
    SDL_RenderPresent(renderer);
    // 60 frames a second, as on the Switch, also where nothing waits for the screen
    static auto next = std::chrono::steady_clock::now();
    next += std::chrono::microseconds(16667);
    const auto now = std::chrono::steady_clock::now();
    if (next > now)
        std::this_thread::sleep_until(next);
    else
        next = now;
}

extern "C" {

void padConfigureInput(u32, u32) {}
void padInitializeDefault(PadState* pad)
{
    *pad = {};
    loadScript();
}
void padUpdate(PadState* pad)
{
    pad->buttonsOld = pad->buttons;
    if (scripted)
    {
        runScript();
        pad->buttons = scriptHeld;
    }
    else
    {
        pad->buttons = keyboardButtons();
    }
}
u64 padGetButtons(const PadState* pad)
{
    return pad->buttons;
}

void hidInitializeTouchScreen(void) {}
size_t hidGetTouchScreenStates(HidTouchScreenState* states, size_t)
{
    states->count = 0;
    if (scripted)
    {
        if (scriptTouch)
        {
            states->count = 1;
            states->touches[0] = {static_cast<u32>(touchX), static_cast<u32>(touchY)};
        }
        return 1;
    }
    int x = 0, y = 0;
    if (SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_LEFT))
    {
        states->count = 1;
        states->touches[0] = {static_cast<u32>(x), static_cast<u32>(y)};
    }
    return 1;
}

Result threadCreate(Thread* t, ThreadFunc entry, void* arg, void*, size_t, int, int)
{
    auto* start = new std::pair<ThreadFunc, void*>(entry, arg);
    t->handle = start;
    return 0;
}
Result threadStart(Thread* t)
{
    auto* start = static_cast<std::pair<ThreadFunc, void*>*>(t->handle);
    t->handle = new std::thread([start] {
        start->first(start->second);
        delete start;
    });
    return 0;
}
Result threadWaitForExit(Thread* t)
{
    static_cast<std::thread*>(t->handle)->join();
    return 0;
}
Result threadClose(Thread* t)
{
    delete static_cast<std::thread*>(t->handle);
    t->handle = nullptr;
    return 0;
}

void mutexInit(Mutex* m)
{
    m->handle = new std::mutex;
}
void mutexLock(Mutex* m)
{
    static_cast<std::mutex*>(m->handle)->lock();
}
void mutexUnlock(Mutex* m)
{
    static_cast<std::mutex*>(m->handle)->unlock();
}

void condvarInit(CondVar* c)
{
    c->handle = new std::condition_variable;
}
Result condvarWait(CondVar* c, Mutex* m)
{
    std::unique_lock<std::mutex> lock(*static_cast<std::mutex*>(m->handle), std::adopt_lock);
    static_cast<std::condition_variable*>(c->handle)->wait(lock);
    lock.release();
    return 0;
}
Result condvarWakeOne(CondVar* c)
{
    static_cast<std::condition_variable*>(c->handle)->notify_one();
    return 0;
}
Result condvarWakeAll(CondVar* c)
{
    static_cast<std::condition_variable*>(c->handle)->notify_all();
    return 0;
}

Result plInitialize(PlServiceType)
{
    return 0;
}
void plExit(void) {}
Result plGetSharedFontByType(PlFontData* font, PlSharedFontType type)
{
    // the computer's fonts in the shared fonts' places; none has the Switch's button symbols
    const char* paths[6] = {"/System/Library/Fonts/SFNSRounded.ttf", "/Library/Fonts/Arial Unicode.ttf", nullptr,
                            nullptr, nullptr, nullptr};
    const char* path = paths[type];
    if (!path)
        return 1;
    auto& data = fonts[type];
    if (data.empty())
    {
        std::ifstream file(path, std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(file), {});
    }
    if (data.empty())
        return 1;
    font->address = data.data();
    font->size = static_cast<u32>(data.size());
    return 0;
}

Result swkbdCreate(SwkbdConfig*, s32)
{
    return 0;
}
void swkbdClose(SwkbdConfig*) {}
void swkbdConfigMakePresetDefault(SwkbdConfig*) {}
void swkbdConfigSetGuideText(SwkbdConfig*, const char*) {}
void swkbdConfigSetInitialText(SwkbdConfig*, const char*) {}
void swkbdConfigSetStringLenMax(SwkbdConfig*, u32) {}
Result swkbdShow(SwkbdConfig*, char* out, size_t size)
{
    if (!haveTyped)
        return 1;
    std::snprintf(out, size, "%s", typed.c_str());
    haveTyped = false;
    return 0;
}

Result psmInitialize(void)
{
    return 0;
}
void psmExit(void) {}
Result psmGetBatteryChargePercentage(u32* out)
{
    *out = 76;
    return 0;
}
Result psmGetChargerType(PsmChargerType* out)
{
    *out = PsmChargerType_Unconnected;
    return 0;
}

Result nifmInitialize(NifmServiceType)
{
    return 0;
}
void nifmExit(void) {}
Result nifmGetInternetConnectionStatus(NifmInternetConnectionType*, u32*, NifmInternetConnectionStatus* status)
{
    *status = NifmInternetConnectionStatus_Connected;
    return 0;
}

Result romfsInit(void)
{
    return 0;
}
Result romfsExit(void)
{
    return 0;
}
Result socketInitializeDefault(void)
{
    return 0;
}
void socketExit(void) {}
int nxlinkStdio(void)
{
    return -1;
}
bool appletMainLoop(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            quitting = true;
    }
    return !quitting;
}
Result envSetNextLoad(const char* path, const char* argv)
{
    std::printf("next load: %s %s\n", path, argv);
    return 0;
}

int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen)
{
    auto value = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };
    size_t out = 0;
    unsigned bits = 0;
    int count = 0;
    for (size_t i = 0; i < slen; ++i)
    {
        const int v = value(src[i]);
        if (v < 0)
            continue;
        bits = (bits << 6) | static_cast<unsigned>(v);
        count += 6;
        if (count >= 8)
        {
            count -= 8;
            if (out >= dlen)
                return -1;
            dst[out++] = static_cast<unsigned char>((bits >> count) & 0xFF);
        }
    }
    *olen = out;
    return 0;
}

} // extern "C"
