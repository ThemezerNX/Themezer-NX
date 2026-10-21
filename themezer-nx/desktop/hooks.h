// Force-included in the desktop build: the frame's present goes through the script runner, which takes screenshots.
#pragma once
#ifdef __cplusplus
#include <SDL2/SDL.h>
extern "C" void desktopPresent(SDL_Renderer* renderer);
#define SDL_RenderPresent(renderer) desktopPresent(renderer)
#endif
