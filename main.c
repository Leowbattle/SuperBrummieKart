#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include <SDL2/SDL.h>

#define GAME_WIDTH 640
#define GAME_HEIGHT 480

typedef struct rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} rgb;

typedef struct vec2 {
	float x;
	float y;
} vec2;

typedef struct vec3 {
	float x;
	float y;
	float z;
} vec3;

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* frameTexture;
uint8_t* pixelData;
int rowPitch;

bool gameRunning;

int main(int argc, char** argv) {
    SDL_Init(SDL_INIT_EVERYTHING);

    SDL_version sdlVersion;
    SDL_GetVersion(&sdlVersion);
    printf("SDL Version: %d.%d.%d\n", sdlVersion.major, sdlVersion.minor, sdlVersion.patch);
    
    window = SDL_CreateWindow("HackTheMidlands", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, GAME_WIDTH, GAME_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_ALWAYS_ON_TOP);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

	frameTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, GAME_WIDTH, GAME_HEIGHT);

    gameRunning = true;
    while (gameRunning) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
				gameRunning = false; 
                break;

            default:
                break;
            }
        }

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
		SDL_RenderClear(renderer);

		SDL_LockTexture(frameTexture, NULL, (void**)&pixelData, &rowPitch);
		// All rendering must be in here
		memset(pixelData, 127, rowPitch * GAME_HEIGHT);

		SDL_UnlockTexture(frameTexture);

		SDL_RenderCopy(renderer, frameTexture, NULL, NULL);

		SDL_RenderPresent(renderer);
    }

    return 0;
}
