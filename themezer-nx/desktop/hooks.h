// Force-included in the desktop build. The frame's present goes through the script runner, which takes screenshots;
// and the SD card's paths (/Themes, /switch) go to $THEMEZER_SD when it's set, so installing and downloading work on
// the computer. The system's headers come first, so the macros only touch the app's own code.
#pragma once
#ifdef __cplusplus
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <curl/curl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <expected>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" void desktopPresent(SDL_Renderer* renderer);
const char* desktopSd(const char* path);
inline const char* desktopSd(const std::string& path)
{
    return desktopSd(path.c_str());
}

#define SDL_RenderPresent(renderer) desktopPresent(renderer)
#define mkdir(path, mode) mkdir(desktopSd(path), mode)
#define access(path, mode) access(desktopSd(path), mode)
#define opendir(path) opendir(desktopSd(path))
#define unlink(path) unlink(desktopSd(path))
#define fopen(path, mode) fopen(desktopSd(path), mode)
#define remove(path) remove(desktopSd(path))
#endif
