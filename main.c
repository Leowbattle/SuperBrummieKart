// Note to self: In this program +Z axis is up

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#define GAME_WIDTH 1280
#define GAME_HEIGHT 720
#define ASPECT_RATIO ((float)GAME_WIDTH / GAME_HEIGHT)
#define INV_ASPECT_RATIO ((float)GAME_HEIGHT / GAME_WIDTH)

// https://stackoverflow.com/questions/600293/how-to-check-if-a-number-is-a-power-of-2
bool IsPowerOfTwo(int x) {
    return (x != 0) && ((x & (x - 1)) == 0);
}

float deg2rad(float deg) {
	return deg * M_PI / 180;
}

float rad2deg(float rad) {
	return rad * 180 / M_PI;
}

float mapf(float x, float a1, float b1, float a2, float b2) {
	float t = (x - a1) / (b1 - a1);
	return a2 + t * (b2 - a2);
}

float clampf(float x, float a, float b) {
	if (x < a) {
		return a;
	}
	if (x > b) {
		return b;
	}
	return x;
}

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

vec3 vec3_add(vec3 a, vec3 b) {
	return (vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

vec3 vec3_scale(vec3 a, float s) {
	return (vec3){a.x * s, a.y * s, a.z * s};
}

float vec3_dot(vec3 a, vec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 vec3_cross(vec3 a, vec3 b) {
	return (vec3){
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

vec3 vec3_normalize(vec3 a) {
	float m = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
	return (vec3){a.x / m, a.y / m, a.z / m};
}

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* frameTexture;
void* textureData;
int rowPitch;

void SetPixel(int x, int y, rgb colour) {
	uint8_t* pixelData = textureData;
	pixelData[y * rowPitch + x * 3 + 0] = colour.r;
	pixelData[y * rowPitch + x * 3 + 1] = colour.g;
	pixelData[y * rowPitch + x * 3 + 2] = colour.b;
}

bool gameRunning;
int frame;
float globalTime = 0;
float lastGlobalTime = 0;
float global_dt = 1.0f / 60;

uint8_t* keyboardState = NULL;
uint8_t* lastKeyboardState = NULL;

int mousexrel;
int mouseyrel;

typedef enum CameraMode {
	FreeFlyCamera
} CameraMode;

typedef struct Camera {
	vec3 position;
	float yaw;
	float pitch;
	vec2 forward_2d;
	vec3 forward;
	vec3 up;
	vec3 right;
	float fov_x;
	float fov_y;
	float cam_dist;

	CameraMode mode;
} Camera;

void Camera_SetYawPitch(Camera* cam, float yaw, float pitch) {
	cam->yaw = yaw;
	cam->pitch = pitch;

	float cy = cosf(yaw);
	float sy = sinf(yaw);
	float cp = cosf(pitch);
	float sp = sinf(pitch);

	cam->forward_2d = (vec2){cy, sy};
	cam->forward = (vec3){cy * cp, sy * cp, sp};
	cam->right = (vec3){-sy, cy, 0.0f};
	cam->up = vec3_cross(cam->forward, cam->right);
}

void Camera_SetFovX(Camera* cam, float fx) {
	cam->fov_x = fx;
	cam->fov_y = 2 * atanf(INV_ASPECT_RATIO * tanf(fx / 2));
	cam->cam_dist = GAME_WIDTH / (2 * tanf(fx / 2));
}

void Camera_SetFovY(Camera* cam, float fy) {
	cam->fov_y = fy;
	cam->fov_x = 2 * atanf(ASPECT_RATIO * tanf(fy / 2));
	cam->cam_dist = GAME_HEIGHT / (2 * tanf(fy / 2));
}

typedef struct Track {
	SDL_Surface* trackImage;
	int size_log2;
} Track;

void Track_Load(Track* tr, const char* path) {
	SDL_Surface* surf = IMG_Load(path);
	if (surf == NULL) {
		fprintf(stderr, "Unable to load track: %s\n", IMG_GetError());
		exit(EXIT_FAILURE);
	}

	if (surf->w != surf->h || !IsPowerOfTwo(surf->w)) {
		fprintf(stderr, "Invalid track size: %dx%d. Track width and height must be the same and a power of 2.\n", surf->w, surf->h);
	}

	int size_log2 = log2f(surf->w);

	SDL_Surface* surf2 = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGB24, 0);
	tr->trackImage = surf2;
	tr->size_log2 = size_log2;
	SDL_FreeSurface(surf);
}

void Track_Unload(Track* tr) {
	SDL_FreeSurface(tr->trackImage);
}

Camera mainCamera;
Track track;

void VisualizeRayDirections() {
	for (int i = 0; i < GAME_HEIGHT; i++) {
		for (int j = 0; j < GAME_WIDTH; j++) {
			float x = mapf(j, 0, GAME_WIDTH, -1, 1);
			float y = mapf(i, 0, GAME_HEIGHT, 1, -1);

			vec3 dir = vec3_scale(vec3_normalize(vec3_add(
				vec3_scale(mainCamera.forward, mainCamera.cam_dist), 
				vec3_add(vec3_scale(mainCamera.right, x * GAME_WIDTH / 2), vec3_scale(mainCamera.up, y * GAME_HEIGHT / 2)))), 255);

			// SetPixel(j, i, (rgb){(uint8_t)(j ^ i), 0, 0});
			SetPixel(j, i, (rgb){fabsf(dir.x), fabsf(dir.y), fabsf(dir.z)});
		}
	}
}

void DrawFloor() {
	vec3 pos = mainCamera.position;

	for (int i = 0; i < GAME_HEIGHT; i++) {
		for (int j = 0; j < GAME_WIDTH; j++) {
			float x = mapf(j, 0, GAME_WIDTH, -1, 1);
			float y = mapf(i, 0, GAME_HEIGHT, 1, -1);

			vec3 dir = vec3_add(
				vec3_scale(mainCamera.forward, mainCamera.cam_dist), 
				vec3_add(vec3_scale(mainCamera.right, x * GAME_WIDTH / 2), vec3_scale(mainCamera.up, y * GAME_HEIGHT / 2)));

			float t = -pos.z / dir.z;

			if (t < 0) {
				continue;
			}

			float fx = pos.x + t * dir.x;
			float fy = pos.y + t * dir.y;

			int mask = (1 << track.size_log2) - 1;
			int tx = (int)fx & mask;
			int ty = (int)fy & mask;

			SDL_Surface* surf = track.trackImage;
			uint8_t* pixelData = surf->pixels;
			uint8_t r = pixelData[ty * surf->pitch + tx * 3 + 0];
			uint8_t g = pixelData[ty * surf->pitch + tx * 3 + 1];
			uint8_t b = pixelData[ty * surf->pitch + tx * 3 + 2];

			SetPixel(j, i, (rgb){r, g, b});
		}
	}
}

void UpdateFreeFlyCamera() {
	const float flySpeed = 200;
	const float mouseSensitivity = 1;

	Camera* cam = &mainCamera;

	float newYaw = cam->yaw + mousexrel * mouseSensitivity * global_dt;
	float newPitch = cam->pitch - mouseyrel * mouseSensitivity * global_dt;

	newPitch = clampf(newPitch, -M_PI_2, M_PI_2);

	Camera_SetYawPitch(cam, newYaw, newPitch);

	if (keyboardState[SDL_SCANCODE_W]) {
		cam->position.x += cam->forward_2d.x * flySpeed * global_dt;
		cam->position.y += cam->forward_2d.y * flySpeed * global_dt;
	}
	if (keyboardState[SDL_SCANCODE_S]) {
		cam->position.x -= cam->forward_2d.x * flySpeed * global_dt;
		cam->position.y -= cam->forward_2d.y * flySpeed * global_dt;
	}
	if (keyboardState[SDL_SCANCODE_D]) {
		cam->position.x += cam->right.x * flySpeed * global_dt;
		cam->position.y += cam->right.y * flySpeed * global_dt;
	}
	if (keyboardState[SDL_SCANCODE_A]) {
		cam->position.x -= cam->right.x * flySpeed * global_dt;
		cam->position.y -= cam->right.y * flySpeed * global_dt;
	}

	if (keyboardState[SDL_SCANCODE_SPACE]) {
		cam->position.z += flySpeed * global_dt;
	}
	if (keyboardState[SDL_SCANCODE_LSHIFT]) {
		cam->position.z -= flySpeed * global_dt;
	}
}

void UpdateCamera() {
	switch (mainCamera.mode) {
	case FreeFlyCamera:
		UpdateFreeFlyCamera();
		break;
	default:
		fprintf(stderr, "Invalid camera mode %d\n", mainCamera.mode);
		exit(EXIT_FAILURE);
	}
}

void updateKeyboard() {
	int numKeys;
	const uint8_t* kb = SDL_GetKeyboardState(&numKeys);

	if (keyboardState == NULL) {
		keyboardState = malloc(numKeys);
		lastKeyboardState = malloc(numKeys);
	}

	memcpy(lastKeyboardState, keyboardState, numKeys);
	memcpy(keyboardState, kb, numKeys);
}

void update() {
	lastGlobalTime = globalTime;
	globalTime += global_dt;

	updateKeyboard();

	UpdateCamera();
}

void draw() {
	////////////////////
	// Prepare draw

	// Clear screen
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	// Get pointer to pixel data for frame
	SDL_LockTexture(frameTexture, NULL, &textureData, &rowPitch);
	memset(textureData, 127, rowPitch * GAME_HEIGHT);

	////////////////////
	// Actual drawing
	{
		// VisualizeRayDirections();
		DrawFloor();
	}
	////////////////////

	// Present frame
	SDL_UnlockTexture(frameTexture);
	SDL_RenderCopy(renderer, frameTexture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

int main() {
    SDL_Init(SDL_INIT_EVERYTHING);
	IMG_Init(IMG_INIT_PNG);
	TTF_Init();

    SDL_version sdlVersion;
    SDL_GetVersion(&sdlVersion);
    printf("SDL Version: %d.%d.%d\n", sdlVersion.major, sdlVersion.minor, sdlVersion.patch);
    
    window = SDL_CreateWindow("HackTheMidlands", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, GAME_WIDTH, GAME_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALWAYS_ON_TOP);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

	SDL_RenderSetLogicalSize(renderer, GAME_WIDTH, GAME_HEIGHT);
	SDL_RenderSetIntegerScale(renderer, true);

	frameTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, GAME_WIDTH, GAME_HEIGHT);

	SDL_SetRelativeMouseMode(true);

	mainCamera.position = (vec3){0, 0, 200};
	Camera_SetFovX(&mainCamera, deg2rad(90));
	Camera_SetYawPitch(&mainCamera, 0, 0);
	mainCamera.mode = FreeFlyCamera;

	Track_Load(&track, "mario_circuit_1.png");

    gameRunning = true;
	frame = 0;
    while (gameRunning) {
		mousexrel = 0;
		mouseyrel = 0;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
				gameRunning = false; 
                break;

			case SDL_KEYDOWN:
				if (ev.key.keysym.sym == SDLK_ESCAPE) {
					gameRunning = false;
				}
				break;

			case SDL_MOUSEMOTION:
				mousexrel = ev.motion.xrel;
				mouseyrel = ev.motion.yrel;
				break;

            default:
                break;
            }
        }

		update();
		draw();

		frame++;
    }

    return 0;
}
