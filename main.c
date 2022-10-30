// Note to self: In this program +Z axis is up

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
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

float lerpf(float a, float b, float t) {
	return a + (b - a) * t;
}

typedef struct rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} rgb;

typedef struct rgba {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} rgba;

typedef struct vec2 {
	float x;
	float y;
} vec2;

vec2 vec2_scale(vec2 a, float s) {
	return (vec2){a.x * s, a.y * s};
}

float vec2_dot(vec2 a, vec2 b) {
	return a.x * b.x + a.y * b.y;
}

vec2 vec2_normalize(vec2 a) {
	float m = sqrtf(a.x * a.x + a.y * a.y);
	return (vec2){a.x / m, a.y / m};
}

typedef struct vec3 {
	float x;
	float y;
	float z;
} vec3;

vec3 vec3_add(vec3 a, vec3 b) {
	return (vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

vec3 vec3_sub(vec3 a, vec3 b) {
	return (vec3){a.x - b.x, a.y - b.y, a.z - b.z};
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

vec2 linear_solve2(float a, float b, float c, float d, float e, float f) {
	float s = a * d - b * c;

	return (vec2){
		(e * d - b * f) / s,
		(a * f - e * c) / s
	};
}

typedef struct mat3 {
	float a, b, c, d, e, f, g, h, i;
} mat3;

void mat3_invert(mat3* m) {
	float 
		a = m->a,
		b = m->b,
		c = m->c,
		d = m->d,
		e = m->e,
		f = m->f,
		g = m->g,
		h = m->h,
		i = m->i;

	float s = 1 / (a*e*i-a*f*h-b*d*i+b*f*g+c*d*h-c*e*g);

	m->a = (e*i-f*h)*s;
	m->b = (c*h-b*i)*s;
	m->c = (b*f-c*e)*s;
	
	m->d = (f*g-d*i)*s;
	m->e = (a*i-c*g)*s;
	m->f = (c*d-a*f)*s;

	m->g = (d*h-e*g)*s;
	m->h = (b*g-a*h)*s;
	m->i = (a*e-b*d)*s;
}

vec3 mat3_mul(mat3 m, vec3 v) {
	return (vec3){
		m.a * v.x + m.b * v.y + m.c * v.z,
		m.d * v.x + m.e * v.y + m.f * v.z,
		m.g * v.x + m.h * v.y + m.i * v.z,
	};
}

#define MAX_SPRITE_ANGLES 16

typedef struct Sprite {
	SDL_Surface* img;
	int numAngles;
	int w;
	int h;
	vec3 pos;
	float angle;
} Sprite;

#define MAX_SPRITES 1024
Sprite sprites[MAX_SPRITES];
int numSprites = 0;

int AddSprite(const char* path);

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* frameTexture;
void* textureData;
int rowPitch;

void SetPixel(int x, int y, rgb colour) {
	if (x > 0 && x < GAME_WIDTH && y > 0 && y < GAME_HEIGHT) {
		uint8_t* pixelData = textureData;
		pixelData[y * rowPitch + x * 3 + 0] = colour.r;
		pixelData[y * rowPitch + x * 3 + 1] = colour.g;
		pixelData[y * rowPitch + x * 3 + 2] = colour.b;
	}
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
	FreeFlyCamera,
	FirstPerson,
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

rgb SampleSurface(SDL_Surface* surf, int x, int y) {
	uint8_t* pixelData = surf->pixels;
	uint8_t r = pixelData[y * surf->pitch + x * 3 + 0];
	uint8_t g = pixelData[y * surf->pitch + x * 3 + 1];
	uint8_t b = pixelData[y * surf->pitch + x * 3 + 2];
	return (rgb){r,g,b};
}

rgba SampleSurface_rgba(SDL_Surface* surf, int x, int y) {
	uint8_t* pixelData = surf->pixels;
	uint8_t r = pixelData[y * surf->pitch + x * 4 + 0];
	uint8_t g = pixelData[y * surf->pitch + x * 4 + 1];
	uint8_t b = pixelData[y * surf->pitch + x * 4 + 2];
	uint8_t a = pixelData[y * surf->pitch + x * 4 + 3];
	return (rgba){r,g,b,a};
}

typedef struct Track {
	SDL_Surface* trackImage;
	SDL_Surface* attributeImage;
	int size_log2;
} Track;

void Track_Load(Track* tr, const char* path, const char* attr_path) {
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

	SDL_Surface* attr_surf = IMG_Load(attr_path);
	SDL_Surface* attr_surf2 = SDL_ConvertSurfaceFormat(attr_surf, SDL_PIXELFORMAT_RGB24, 0);
	tr->attributeImage = attr_surf2;

	SDL_FreeSurface(surf);
	SDL_FreeSurface(attr_surf);

	for (int i = 0; i < tr->attributeImage->h; i++) {
		for (int j = 0; j < tr->attributeImage->w; j++) {
			rgb c = SampleSurface(tr->attributeImage, j, i);
			if (memcmp(&c, &(rgb){0, 0, 255}, sizeof(rgb)) == 0) {
				int s = AddSprite("tree.png");
				sprites[s].pos = (vec3){j, i, 0};
			}
		}
	}
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

int AddSprite(const char* path) {
	if (numSprites == MAX_SPRITES) {
		fprintf(stderr, "Ran out of sprites\n");
		exit(EXIT_FAILURE);
	}

	SDL_Surface* surf = IMG_Load(path);
	if (surf == NULL) {
		fprintf(stderr, "Unable to load sprite: %s\n", IMG_GetError());
		exit(EXIT_FAILURE);
	}

	sprites[numSprites].img = surf;
	sprites[numSprites].numAngles = 1;
	sprites[numSprites].w = surf->w;
	sprites[numSprites].h = surf->h;
	return numSprites++;
}

int AddSpriteRotations(const char* path) {
	if (numSprites == MAX_SPRITES) {
		fprintf(stderr, "Ran out of sprites\n");
		exit(EXIT_FAILURE);
	}

	SDL_Surface* surf = IMG_Load(path);
	if (surf == NULL) {
		fprintf(stderr, "Unable to load sprite: %s\n", IMG_GetError());
		exit(EXIT_FAILURE);
	}

	sprites[numSprites].img = surf;
	sprites[numSprites].numAngles = surf->w / surf->h;
	sprites[numSprites].w = surf->h;
	sprites[numSprites].h = surf->h;
	return numSprites++;
}

vec2 ProjectPoint(vec3 p) {
	Camera* cam = &mainCamera;

	p = vec3_sub(p, cam->position);
	mat3 m = {
		cam->forward.x * cam->cam_dist, cam->right.x * GAME_WIDTH / 2, cam->up.x * GAME_HEIGHT / 2,
		cam->forward.y * cam->cam_dist, cam->right.y * GAME_WIDTH / 2, cam->up.y * GAME_HEIGHT / 2,
		cam->forward.z * cam->cam_dist, cam->right.z * GAME_WIDTH / 2, cam->up.z * GAME_HEIGHT / 2,
	};
	mat3_invert(&m);
	vec3 v = mat3_mul(m, p);
	v.y /= v.x;
	v.z /= v.x;

	return (vec2){
		mapf(v.y, -1, 1, 0, GAME_WIDTH),
		mapf(v.z, 1, -1, 0, GAME_HEIGHT)
	};
}

int cmp_sprites(const void* a, const void* b) {
	Camera* cam = &mainCamera;

	const Sprite* sa = a;
	const Sprite* sb = b;

	float da = vec3_dot(cam->forward, vec3_sub(sa->pos, cam->position));
	float db = vec3_dot(cam->forward, vec3_sub(sb->pos, cam->position));

	if (da < db) {
		return 1;
	}
	else {
		return -1;
	}
}

void DrawSprites() {
	// TODO: Fix sprite rotations

	Camera* cam = &mainCamera;

	qsort(sprites, numSprites, sizeof(Sprite), cmp_sprites);

	for (int i = 0; i < numSprites; i++) {
		Sprite* spr = &sprites[i];

		int rotationIndex = 0;
		bool flipX = false;
		if (spr->numAngles > 1) {
			// Select which sprite
			//rotationIndex = (frame / 10) % spr->numAngles;

			vec2 diff = (vec2){spr->pos.x - cam->position.x, spr->pos.y - cam->position.y};
			vec2 right = (vec2){cam->right.x, cam->right.y};

			float theta = spr->angle - cam->yaw - atanf(GAME_WIDTH/(2*cam->cam_dist)* vec2_dot(diff, right) / vec2_dot(diff, cam->forward_2d));
			// printf("%f\n", theta);
			if (theta < 0) {
				flipX = true;
				theta = -theta;
			}
			rotationIndex = mapf(theta, 0, M_PI, 0, spr->numAngles);
			rotationIndex %= spr->numAngles * 2;
			if (rotationIndex > spr->numAngles) {
				rotationIndex = rotationIndex - spr->numAngles;
				flipX = true;
			}
			printf("%d\n", rotationIndex);
		}

		vec3 left3 = vec3_sub(spr->pos, vec3_scale(cam->right, spr->w/2.0f));
		vec3 right3 = vec3_add(spr->pos, vec3_scale(cam->right, spr->w/2.0f));

		vec3 top3 = vec3_add(spr->pos, vec3_scale(cam->up, spr->h));

		vec2 left = ProjectPoint(left3);
		vec2 right = ProjectPoint(right3);
		vec2 top = ProjectPoint(top3);

		int leftx = clampf(left.x, 0, GAME_WIDTH);
		int rightx = clampf(right.x, 0, GAME_WIDTH);

		int topy = clampf(top.y, 0, GAME_HEIGHT);
		int bottomy = clampf(left.y, 0, GAME_HEIGHT);

		for (int y = topy; y < bottomy; y++) {
			for (int x = leftx; x < rightx; x++) {
				int tx;
				if (flipX) {
					tx = mapf(x, right.x, left.x, 0, spr->w) + spr->w * rotationIndex;
				}
				else {
					tx = mapf(x, left.x, right.x, 0, spr->w) + spr->w * rotationIndex;
				}
				int ty = mapf(y, top.y, left.y, 0, spr->h);
				rgba c = SampleSurface_rgba(spr->img, tx, ty);
				if (c.a < 1) {
					continue;
				}
				SetPixel(x, y, (rgb){c.r,c.g,c.b});
			}
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

	if (keyboardState[SDL_SCANCODE_I]) {
		Camera_SetFovX(cam, cam->fov_x + deg2rad(10) * global_dt);
	}
	if (keyboardState[SDL_SCANCODE_K]) {
		Camera_SetFovX(cam, cam->fov_x - deg2rad(10) * global_dt);
	}
}

vec2 velocity = {0};
void UpdateFirstPersonCamera() {
	const float acceleration = 20;
	const float turnSpeed = deg2rad(90);
	
	Camera* cam = &mainCamera;

	float drag;
	rgb c = SampleSurface(track.attributeImage, cam->position.x, cam->position.y);
	if (memcmp(&c, &(rgb){255,0,0}, sizeof(rgb)) == 0) {
		drag = 5;
	}
	else if (memcmp(&c, &(rgb){0,255,0}, sizeof(rgb)) == 0) {
		// drag = 30;
	}
	else {
		drag = 15;
	}

	if (keyboardState[SDL_SCANCODE_W]) {
		velocity.x += cam->forward_2d.x * acceleration * global_dt;
		velocity.y += cam->forward_2d.y * acceleration * global_dt;
	}
	if (keyboardState[SDL_SCANCODE_S]) {
		velocity.x -= cam->forward_2d.x * acceleration * global_dt;
		velocity.y -= cam->forward_2d.y * acceleration * global_dt;
	}

	if (keyboardState[SDL_SCANCODE_A]) {
		Camera_SetYawPitch(cam, cam->yaw - turnSpeed * global_dt, cam->pitch);
	}
	if (keyboardState[SDL_SCANCODE_D]) {
		Camera_SetYawPitch(cam, cam->yaw + turnSpeed * global_dt, cam->pitch);
	}

	velocity.x -= drag * velocity.x * global_dt;
	velocity.y -= drag * velocity.y * global_dt;

	cam->position.x += velocity.x;
	cam->position.y += velocity.y;
}

void UpdateCamera() {
	switch (mainCamera.mode) {
	case FreeFlyCamera:
		UpdateFreeFlyCamera();
		break;
	case FirstPerson:
		UpdateFirstPersonCamera();
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
		DrawSprites();
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
    
    window = SDL_CreateWindow("HackTheMidlands", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, GAME_WIDTH, GAME_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

	SDL_RenderSetLogicalSize(renderer, GAME_WIDTH, GAME_HEIGHT);
	SDL_RenderSetIntegerScale(renderer, true);

	frameTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, GAME_WIDTH, GAME_HEIGHT);

	SDL_SetRelativeMouseMode(true);

	mainCamera.position = (vec3){920, 585, 10};
	// mainCamera.position = (vec3){200, 200, 50};
	Camera_SetFovX(&mainCamera, deg2rad(90));
	Camera_SetYawPitch(&mainCamera, deg2rad(-90), deg2rad(-20));
	mainCamera.mode = FreeFlyCamera;

	Track_Load(&track, "mario_circuit_1.png", "mario_circuit_1_attributes.png");

	int bowser = AddSpriteRotations("bowser.png");
	sprites[bowser].pos = (vec3){920, 485, 0};
	sprites[bowser].angle = deg2rad(-90);

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
