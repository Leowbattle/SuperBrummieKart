#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stdio.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// SuperBrummieKart runs on desktop SDL2, in the browser (Emscripten) and on the
// Nintendo Wii. There is no working directory to load assets from on the
// console, so its assets are linked into the executable, and it gets its input
// from libogc rather than SDL. Everything that differs between the targets lives
// behind this header: see platform_sdl.c and platform_wii.c.

// Which SDL subsystems SDL_Init() should bring up. The desktop and the browser
// get SDL_INIT_EVERYTHING; the Wii only initialises what the game actually uses,
// because the console gets its input from libogc instead of SDL and has no
// sound.
uint32_t Platform_GetSubsystemFlags(void);

// Called once at the start of main(), before SDL_Init().
void Platform_Init(void);

// Called once after the main loop and the SDL cleanup. On the Wii this powers
// the console off, so it does not return.
void Platform_Exit(void);

// Asset loading. On the desktop and in the browser these read the files next to
// the working directory; on the Wii they read the copy embedded in the binary.
// "_Load" returns NULL if the asset is missing, failures are not logged here.
FILE* Asset_OpenFile(const char* path);
SDL_Surface* Asset_LoadSurface(const char* path);
SDL_Texture* Asset_LoadTexture(SDL_Renderer* renderer, const char* path);
TTF_Font* Asset_OpenFont(const char* path, int pointSize);

// Size of the window to create for a game resolution of gameWidth x gameHeight.
// SDL_RenderSetLogicalSize() then letterboxes the game inside it, so the two do
// not have to match.
void Platform_GetWindowSize(int gameWidth, int gameHeight, int* windowWidth, int* windowHeight);

// Current state of the keys the game reads, indexed by SDL scancode. The
// returned array stays valid until the next call; numKeys receives
// SDL_NUM_SCANCODES.
const uint8_t* Platform_PollKeyboard(int* numKeys);

#endif
