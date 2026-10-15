// Desktop and Emscripten implementation of the platform layer: everything is
// read straight from SDL2, which the host (or the browser's virtual filesystem)
// provides. See platform_wii.c for the console port.

#include "platform.h"

#include <SDL2/SDL_image.h>

void Platform_Init(void) {}

void Platform_Exit(void) {}

FILE* Asset_OpenFile(const char* path) {
	return fopen(path, "r");
}

// Which SDL subsystems the game needs. Everything the desktop can provide is
// fine there, but the console builds only initialise what the game uses.
uint32_t Platform_GetSubsystemFlags(void) {
	return SDL_INIT_EVERYTHING;
}

SDL_Surface* Asset_LoadSurface(const char* path) {
	return IMG_Load(path);
}

SDL_Texture* Asset_LoadTexture(SDL_Renderer* renderer, const char* path) {
	return IMG_LoadTexture(renderer, path);
}

TTF_Font* Asset_OpenFont(const char* path, int pointSize) {
	return TTF_OpenFont(path, pointSize);
}

void Platform_GetWindowSize(int gameWidth, int gameHeight, int* windowWidth, int* windowHeight) {
	*windowWidth = gameWidth * 2;
	*windowHeight = gameHeight * 2;
}

const uint8_t* Platform_PollKeyboard(int* numKeys) {
	return SDL_GetKeyboardState(numKeys);
}
