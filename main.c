// Note: In this program +Z axis is up

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

#include "platform.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define GAME_WIDTH 640
#define GAME_HEIGHT 360
#define ASPECT_RATIO ((float)GAME_WIDTH / GAME_HEIGHT)
#define INV_ASPECT_RATIO ((float)GAME_HEIGHT / GAME_WIDTH)

#define SHOW_PATH_MARKERS false
#define ENABLE_TREES true

enum TextAlignment {
	TEXT_ALIGN_LEFT = 0,
	TEXT_ALIGN_BELOW = 0,
	TEXT_ALIGN_CENTRE = 1,
	TEXT_ALIGN_RIGHT = 2,
	TEXT_ALIGN_ABOVE = 2,
};

typedef struct DrawStringInfo {
	TTF_Font* font;
	SDL_Color colour;
	int x;
	int y;

	enum TextAlignment alignX;
	enum TextAlignment alignY;
} DrawStringInfo;

void drawString(DrawStringInfo* dsi, const char* msg);
void drawStringf(DrawStringInfo* dsi, const char* fmt, ...);

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

vec2 vec2_add(vec2 a, vec2 b) {
	return (vec2){a.x + b.x, a.y + b.y};
}

vec2 vec2_sub(vec2 a, vec2 b) {
	return (vec2){a.x - b.x, a.y - b.y};
}

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

// Sprite images are shared between the sprites that use them: the track has
// one tree image for its ~75 trees, and uploading a texture per instance
// would cost tens of megabytes.
typedef struct SpriteImage {
	char path[256];
	SDL_Texture* texture;
	int w;
	int h;
	int numAngles;
} SpriteImage;

#define MAX_SPRITE_IMAGES 8
SpriteImage spriteImages[MAX_SPRITE_IMAGES];
int numSpriteImages = 0;

typedef struct Sprite {
	SpriteImage* img;
	vec3 pos;
	float angle;
} Sprite;

#define MAX_SPRITES 1024
Sprite sprites[MAX_SPRITES];
int numSprites = 0;

int AddSprite(const char* path);
int AddSpriteRotations(const char* path);

// Forward declarations for game state transitions.
// Without these, modern GCC (>= 14) rejects the implicit function
// declarations as errors, so the project would not build.
void Transition_init(void);
void Game_init(void);
void Over_init(void);

typedef struct Enemy {
	int sprite;
	vec2 pos;

	vec2* path;
	int pathLength;
	int pathIndex;
	vec2 dir;

	float speed;

	int lap;
} Enemy;

#define NUM_ENEMIES 2
Enemy enemies[NUM_ENEMIES];

void NextTrack();

SDL_Window* window;
SDL_Renderer* renderer;

TTF_Font* font;
TTF_Font* font_small;

void drawString(DrawStringInfo* dsi, const char* msg) {
	SDL_Surface* surf = TTF_RenderUTF8_Blended(dsi->font, msg, dsi->colour);
	SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);

	int x = dsi->x;
	int y = dsi->y;

	switch (dsi->alignX) {
	case TEXT_ALIGN_LEFT:
		break;

	case TEXT_ALIGN_RIGHT:
		x -= surf->w;
		break;

	case TEXT_ALIGN_CENTRE:
		x -= surf->w / 2;
		break;

	default:
		printf("Invalid text align\n");
		exit(-1);
	}

	switch (dsi->alignY) {
	case TEXT_ALIGN_BELOW:
		break;

	case TEXT_ALIGN_ABOVE:
		y -= surf->h;
		break;

	case TEXT_ALIGN_CENTRE:
		y -= surf->h / 2;
		break;

	default:
		printf("Invalid text align\n");
		exit(-1);
	}

	SDL_Rect dest = {
		x,
		y,
		surf->w,
		surf->h
	};

	SDL_RenderCopy(renderer, tex, NULL, &dest);

	SDL_FreeSurface(surf);
	SDL_DestroyTexture(tex);
}

void drawStringf(DrawStringInfo* dsi, const char* fmt, ...) {
	static char* buf;
	static int bufSize;

	while (true) {
		va_list args;
		va_start(args, fmt);

		if (buf == NULL) {
			bufSize = 4;
			buf = malloc(bufSize);
		}
		int ret = vsnprintf(buf, bufSize, fmt, args);
		// vsnprintf returns the length the formatted string *would* have.
		// If it is >= bufSize the output was truncated (the old ">" check
		// let a string of exactly bufSize chars through, losing the last
		// character).
		if (ret < 0 || ret >= bufSize) {
			bufSize = ret + 1;
			buf = realloc(buf, bufSize);
		}
		else {
			drawString(dsi, buf);
			return;
		}

		va_end(args);
	}
}

bool gameRunning;
int frame;
float globalTime = 0;
float lastGlobalTime = 0;
float global_dt = 1.0f / 60;

int trackNumber = 0;

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
	// Clamp to the surface bounds. Callers derive x/y from float maths that
	// can land exactly on (or beyond) the edge, which used to read past the
	// end of the pixel buffer (and crash on Windows).
	if (x < 0) x = 0;
	else if (x >= surf->w) x = surf->w - 1;
	if (y < 0) y = 0;
	else if (y >= surf->h) y = surf->h - 1;

	uint8_t* pixelData = surf->pixels;
	uint8_t r = pixelData[y * surf->pitch + x * 3 + 0];
	uint8_t g = pixelData[y * surf->pitch + x * 3 + 1];
	uint8_t b = pixelData[y * surf->pitch + x * 3 + 2];
	return (rgb){r,g,b};
}

typedef struct PlayerState {
	vec2* path;
	int pathLen;
	int markerNumber;

	int lapNumber;
} PlayerState;

PlayerState mainPlayerState;

void InitPlayerState(PlayerState* ps, const char* path) {
	FILE* f = Asset_OpenFile(path);
	if (f == NULL) {
		fprintf(stderr, "Unable to open player path file '%s'\n", path);
		exit(EXIT_FAILURE);
	}

	int numPoints = 0;
	if (fscanf(f, "%d", &numPoints) != 1 || numPoints <= 0) {
		fprintf(stderr, "Invalid player path file '%s'\n", path);
		fclose(f);
		exit(EXIT_FAILURE);
	}
	vec2* points = malloc(numPoints * sizeof(vec2));
	for (int i = 0; i < numPoints; i++) {
		if (fscanf(f, "%f,%f", &points[i].x, &points[i].y) != 2) {
			fprintf(stderr, "Invalid player path file '%s'\n", path);
			free(points);
			fclose(f);
			exit(EXIT_FAILURE);
		}

		if (SHOW_PATH_MARKERS) {
			vec2 point = points[i];
			int s = AddSprite("player_path_marker.png");
			sprites[s].pos = (vec3){point.x, point.y, 0};
		}
	}
	fclose(f);

	ps->path = points;
	ps->pathLen = numPoints;
	ps->markerNumber = 0;

	ps->lapNumber = 1;
}

void InitEnemyAI(Enemy* enemy, const char* path) {
	FILE* f = Asset_OpenFile(path);
	if (f == NULL) {
		fprintf(stderr, "Unable to open enemy path file '%s'\n", path);
		exit(EXIT_FAILURE);
	}
	
	char spritePath[1024];
	if (fscanf(f, "%1023s", spritePath) != 1) {
		fprintf(stderr, "Invalid enemy path file '%s'\n", path);
		fclose(f);
		exit(EXIT_FAILURE);
	}
	enemy->sprite = AddSpriteRotations(spritePath);
	
	int numPoints = 0;
	if (fscanf(f, "%d", &numPoints) != 1 || numPoints <= 0) {
		fprintf(stderr, "Invalid enemy path file '%s'\n", path);
		fclose(f);
		exit(EXIT_FAILURE);
	}
	vec2* points = malloc(numPoints * sizeof(vec2));
	for (int i = 0; i < numPoints; i++) {
		if (fscanf(f, "%f,%f", &points[i].x, &points[i].y) != 2) {
			fprintf(stderr, "Invalid enemy path file '%s'\n", path);
			free(points);
			fclose(f);
			exit(EXIT_FAILURE);
		}

		if (SHOW_PATH_MARKERS) {
			vec2 point = points[i];
			int s = AddSprite("path_marker.png");
			sprites[s].pos = (vec3){point.x, point.y, 0};
		}
	}
	fclose(f);
	enemy->path = points;
	enemy->pathLength = numPoints;
	enemy->pathIndex = -1;

	enemy->pos = enemy->path[0];

	enemy->speed = 200;

	enemy->lap = 1;
}

typedef struct Track {
	SDL_Texture* trackImage;
	SDL_Surface* attributeImage;
	int size_log2;
	char trackName[1024];
} Track;

Camera mainCamera;

// Turns a track image into a texture. The software renderer treated magenta
// as a hole in the track and let the sky show through, so those texels are
// punched out of the alpha channel. The image is large (1024x1024), so the
// keyed copy replaces the converted surface instead of being kept alongside
// it.
SDL_Texture* Track_CreateTexture(SDL_Surface* surf) {
	SDL_Surface* keyed = SDL_CreateRGBSurfaceWithFormat(0, surf->w, surf->h, 32, SDL_PIXELFORMAT_RGBA32);
	if (keyed == NULL) {
		fprintf(stderr, "Unable to allocate the track texture: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
	}

	for (int y = 0; y < surf->h; y++) {
		uint8_t* src = (uint8_t*)surf->pixels + y * surf->pitch;
		uint8_t* dst = (uint8_t*)keyed->pixels + y * keyed->pitch;

		for (int x = 0; x < surf->w; x++) {
			uint8_t r = src[x * 3 + 0];
			uint8_t g = src[x * 3 + 1];
			uint8_t b = src[x * 3 + 2];

			dst[x * 4 + 0] = r;
			dst[x * 4 + 1] = g;
			dst[x * 4 + 2] = b;
			dst[x * 4 + 3] = (r == 255 && g == 0 && b == 255) ? 0 : 255;
		}
	}

	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, keyed);
	SDL_FreeSurface(keyed);

	if (texture == NULL) {
		fprintf(stderr, "Unable to upload the track image: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
	}
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
	return texture;
}

void Track_Load(Track* tr, const char* path) {
	char buf[1024];

	//memset(buf, 0, sizeof(buf));
	sprintf(buf, "%s/track.png", path);

	SDL_Surface* surf = Asset_LoadSurface(buf);
	if (surf == NULL) {
		fprintf(stderr, "Unable to load track: %s\n", IMG_GetError());
		exit(EXIT_FAILURE);
	}

	if (surf->w != surf->h || !IsPowerOfTwo(surf->w)) {
		fprintf(stderr, "Invalid track size: %dx%d. Track width and height must be the same and a power of 2.\n", surf->w, surf->h);
		SDL_FreeSurface(surf);
		exit(EXIT_FAILURE);
	}

	int size_log2 = log2f(surf->w);

	SDL_Surface* surf2 = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGB24, 0);
	if (surf2 == NULL) {
		fprintf(stderr, "Unable to convert track image: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
	}
	tr->trackImage = Track_CreateTexture(surf2);
	tr->size_log2 = size_log2;
	SDL_FreeSurface(surf2);

	//memset(buf, 0, sizeof(buf));
	sprintf(buf, "%s/attributes.png", path);

	SDL_Surface* attr_surf = Asset_LoadSurface(buf);
	if (attr_surf == NULL) {
		fprintf(stderr, "Unable to load track attributes: %s\n", IMG_GetError());
		exit(EXIT_FAILURE);
	}
	SDL_Surface* attr_surf2 = SDL_ConvertSurfaceFormat(attr_surf, SDL_PIXELFORMAT_RGB24, 0);
	if (attr_surf2 == NULL) {
		fprintf(stderr, "Unable to convert track attributes: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
	}
	tr->attributeImage = attr_surf2;

	SDL_FreeSurface(surf);
	SDL_FreeSurface(attr_surf);

	if (ENABLE_TREES) {
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

	//memset(buf, 0, sizeof(buf));
	sprintf(buf, "%s/paths/player.txt", path);

	InitPlayerState(&mainPlayerState, buf);

	//memset(buf, 0, sizeof(buf));
	sprintf(buf, "%s/paths/1.txt", path);
	InitEnemyAI(&enemies[0], buf);

	//memset(buf, 0, sizeof(buf));
	sprintf(buf, "%s/paths/2.txt", path);
	InitEnemyAI(&enemies[1], buf);

	sprintf(buf, "%s/info.txt", path);
	FILE* f = Asset_OpenFile(buf);
	if (f == NULL) {
		fprintf(stderr, "Unable to open track info '%s'\n", buf);
		exit(EXIT_FAILURE);
	}

	char trackName[1024];
	if (fgets(trackName, sizeof(trackName), f) == NULL) {
		trackName[0] = '\0';
	}
	// Strip the trailing newline (and any '\r' from CRLF files). The old
	// code did `trackName[strlen(trackName) - 1] = 0` unconditionally, which
	// writes out of bounds when the line is empty.
	size_t nameLen = strlen(trackName);
	while (nameLen > 0 && (trackName[nameLen - 1] == '\n' || trackName[nameLen - 1] == '\r')) {
		trackName[--nameLen] = '\0';
	}
	strcpy(tr->trackName, trackName);

	float startX = 0, startY = 0;
	if (fscanf(f, "%f,%f", &startX, &startY) != 2) {
		fprintf(stderr, "Invalid track info '%s'\n", buf);
		fclose(f);
		exit(EXIT_FAILURE);
	}
	fclose(f);

	mainCamera.position = (vec3){
		startX,
		startY,
		20
	};

	Camera_SetFovX(&mainCamera, deg2rad(90));
	Camera_SetYawPitch(&mainCamera, deg2rad(-90), deg2rad(-20));
	mainCamera.mode = FirstPerson;
}

void FreeSprites() {
	for (int i = 0; i < numSpriteImages; i++) {
		SDL_DestroyTexture(spriteImages[i].texture);
		spriteImages[i].texture = NULL;
		spriteImages[i].path[0] = '\0';
	}
	numSpriteImages = 0;
	numSprites = 0;
}

void Track_Unload(Track* tr) {
	if (tr->trackImage != NULL) {
		SDL_DestroyTexture(tr->trackImage);
		tr->trackImage = NULL;
	}
	if (tr->attributeImage != NULL) {
		SDL_FreeSurface(tr->attributeImage);
		tr->attributeImage = NULL;
	}

	// Sprites and paths are owned by the track that created them.
	FreeSprites();

	free(mainPlayerState.path);
	mainPlayerState.path = NULL;
	mainPlayerState.pathLen = 0;

	for (int i = 0; i < NUM_ENEMIES; i++) {
		free(enemies[i].path);
		enemies[i].path = NULL;
		enemies[i].pathLength = 0;
	}
}

Track track;

// https://en.wikipedia.org/wiki/Cube_mapping
void convert_xyz_to_cube_uv(float x, float y, float z, int *index, float *u, float *v)
{
  float absX = fabs(x);
  float absY = fabs(y);
  float absZ = fabs(z);
  
  int isXPositive = x > 0 ? 1 : 0;
  int isYPositive = y > 0 ? 1 : 0;
  int isZPositive = z > 0 ? 1 : 0;
  
  // Initialise up front: a zero-length direction vector matches none of the
  // faces below, which would otherwise leave maxAxis/uc/vc/*index uninitialised.
  float maxAxis = 0.0f, uc = 0.0f, vc = 0.0f;
  *index = 0;
  
  // POSITIVE X
  if (isXPositive && absX >= absY && absX >= absZ) {
    // u (0 to 1) goes from +z to -z
    // v (0 to 1) goes from -y to +y
    maxAxis = absX;
    uc = -z;
    vc = y;
    *index = 0;
  }
  // NEGATIVE X
  if (!isXPositive && absX >= absY && absX >= absZ) {
    // u (0 to 1) goes from -z to +z
    // v (0 to 1) goes from -y to +y
    maxAxis = absX;
    uc = z;
    vc = y;
    *index = 1;
  }
  // POSITIVE Y
  if (isYPositive && absY >= absX && absY >= absZ) {
    // u (0 to 1) goes from -x to +x
    // v (0 to 1) goes from +z to -z
    maxAxis = absY;
    uc = x;
    vc = -z;
    *index = 2;
  }
  // NEGATIVE Y
  if (!isYPositive && absY >= absX && absY >= absZ) {
    // u (0 to 1) goes from -x to +x
    // v (0 to 1) goes from -z to +z
    maxAxis = absY;
    uc = x;
    vc = z;
    *index = 3;
  }
  // POSITIVE Z
  if (isZPositive && absZ >= absX && absZ >= absY) {
    // u (0 to 1) goes from -x to +x
    // v (0 to 1) goes from -y to +y
    maxAxis = absZ;
    uc = x;
    vc = y;
    *index = 4;
  }
  // NEGATIVE Z
  if (!isZPositive && absZ >= absX && absZ >= absY) {
    // u (0 to 1) goes from +x to -x
    // v (0 to 1) goes from -y to +y
    maxAxis = absZ;
    uc = -x;
    vc = y;
    *index = 5;
  }

  if (maxAxis <= 0.0f) {
    // Degenerate direction; sample the centre of the face.
    *u = 0.5f;
    *v = 0.5f;
    return;
  }

  // Convert range from -1 to 1 to 0 to 1
  *u = 0.5f * (uc / maxAxis + 1.0f);
  *v = 0.5f * (vc / maxAxis + 1.0f);
}

// The sky, floor and sprites are drawn as textured geometry by the GPU. They
// used to be rasterised into a streaming texture one pixel at a time, which
// does not fit in the Wii's frame budget. Vertices are batched per texture so
// that a whole scene element costs a single draw call.
#define MAX_RENDER_VERTICES 8192
SDL_Vertex renderVertices[MAX_RENDER_VERTICES];
int numRenderVertices = 0;
SDL_Texture* batchTexture = NULL;

void Renderer_Flush() {
	if (numRenderVertices > 0) {
		if (SDL_RenderGeometry(renderer, batchTexture, renderVertices, numRenderVertices, NULL, 0) < 0) {
			fprintf(stderr, "Unable to draw geometry: %s\n", SDL_GetError());
		}
		numRenderVertices = 0;
	}
	batchTexture = NULL;
}

// Claims room for n vertices in the batch, flushing first if the texture
// changed or the batch is full.
SDL_Vertex* Renderer_Reserve(SDL_Texture* texture, int n) {
	if (texture != batchTexture || numRenderVertices + n > MAX_RENDER_VERTICES) {
		Renderer_Flush();
		batchTexture = texture;
	}

	SDL_Vertex* v = &renderVertices[numRenderVertices];
	numRenderVertices += n;
	return v;
}

void Renderer_Vertex(SDL_Vertex* v, float x, float y, float u, float vTex) {
	v->position.x = x;
	v->position.y = y;
	v->color = (SDL_Color){0xff, 0xff, 0xff, 0xff};
	// The floor's rows reach past the edge of the track texture and clipping the
	// sky to the screen lands on face boundaries, which both leave coordinates a
	// hair outside the texture. The Wii's renderer rejects a whole draw call when
	// a vertex does that, so snap them to the edge instead.
	v->tex_coord.x = SDL_clamp(u, 0.0f, 1.0f);
	v->tex_coord.y = SDL_clamp(vTex, 0.0f, 1.0f);
}

void Renderer_Triangle(SDL_Texture* texture, const SDL_Vertex* a, const SDL_Vertex* b, const SDL_Vertex* c) {
	SDL_Vertex* v = Renderer_Reserve(texture, 3);
	v[0] = *a;
	v[1] = *b;
	v[2] = *c;
}

// corners: top left, top right, bottom right, bottom left.
void Renderer_Quad(SDL_Texture* texture, const SDL_Vertex* corners) {
	SDL_Vertex* v = Renderer_Reserve(texture, 6);
	v[0] = corners[0];
	v[1] = corners[1];
	v[2] = corners[2];
	v[3] = corners[0];
	v[4] = corners[2];
	v[5] = corners[3];
}

typedef struct Skybox {
	SDL_Texture* imgs[6];
} Skybox;

Skybox mainSkybox;

void LoadSkybox(Skybox* sb, const char** paths) {
	for (int i = 0; i < 6; i++) {
		SDL_Surface* surf = Asset_LoadSurface(paths[i]);
		if (surf == NULL) {
			fprintf(stderr, "Unable to load skybox image '%s': %s\n", paths[i], IMG_GetError());
			exit(EXIT_FAILURE);
		}

		SDL_Surface* converted = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGB24, 0);
		SDL_FreeSurface(surf);
		if (converted == NULL) {
			fprintf(stderr, "Unable to convert skybox image '%s': %s\n", paths[i], SDL_GetError());
			exit(EXIT_FAILURE);
		}

		sb->imgs[i] = SDL_CreateTextureFromSurface(renderer, converted);
		SDL_FreeSurface(converted);

		if (sb->imgs[i] == NULL) {
			fprintf(stderr, "Unable to upload skybox image '%s': %s\n", paths[i], SDL_GetError());
			exit(EXIT_FAILURE);
		}
		SDL_SetTextureBlendMode(sb->imgs[i], SDL_BLENDMODE_NONE);
		SDL_SetTextureScaleMode(sb->imgs[i], SDL_ScaleModeNearest);
	}
}

void UnloadSkybox(Skybox* sb) {
	for (int i = 0; i < 6; i++) {
		SDL_DestroyTexture(sb->imgs[i]);
		sb->imgs[i] = NULL;
	}
}

// A cube map face in the layout convert_xyz_to_cube_uv() describes. The
// direction of a point on the face is base + dirA*a + dirB*b with a and b in
// [-1, 1], and u/v (as fractions of the face image) are linear in a and b:
// index = u0 + uA*a + uB*b, likewise for v.
typedef struct SkyFace {
	vec3 base;
	vec3 dirA;
	vec3 dirB;
	float u0, uA, uB;
	float v0, vA, vB;
} SkyFace;

SkyFace skyFaces[6] = {
	// +X: u goes +z to -z, v goes -y to +y
	{{ 1,  0,  0}, {0, 0, 1}, {0, 1, 0}, 0.5f, -0.5f,  0.0f, 0.5f, 0.0f,  0.5f},
	// -X: u goes -z to +z, v goes -y to +y
	{{-1,  0,  0}, {0, 0, 1}, {0, 1, 0}, 0.5f,  0.5f,  0.0f, 0.5f, 0.0f,  0.5f},
	// +Y: u goes -x to +x, v goes +z to -z
	{{ 0,  1,  0}, {1, 0, 0}, {0, 0, 1}, 0.5f,  0.5f,  0.0f, 0.5f, 0.0f, -0.5f},
	// -Y: u goes -x to +x, v goes -z to +z
	{{ 0, -1,  0}, {1, 0, 0}, {0, 0, 1}, 0.5f,  0.5f,  0.0f, 0.5f, 0.0f,  0.5f},
	// +Z: u goes -x to +x, v goes -y to +y
	{{ 0,  0,  1}, {1, 0, 0}, {0, 1, 0}, 0.5f,  0.5f,  0.0f, 0.5f, 0.0f,  0.5f},
	// -Z: u goes +x to -x, v goes -y to +y
	{{ 0,  0, -1}, {1, 0, 0}, {0, 1, 0}, 0.5f, -0.5f,  0.0f, 0.5f, 0.0f,  0.5f},
};

// Directions closer to the plane of the screen than this would project to
// infinity, so they are clipped away.
#define SKY_NEAR 0.1f

// One corner of a skybox cell: its direction and the texel it samples.
typedef struct SkyVertex {
	vec3 dir;
	float u;
	float v;
} SkyVertex;

vec3 SkyFace_Dir(SkyFace* face, float a, float b) {
	return vec3_add(face->base, vec3_add(vec3_scale(face->dirA, a), vec3_scale(face->dirB, b)));
}

// A face is a flat convex quad, so if all of its corners are behind the
// camera the same is true of every point on it.
bool SkyFace_IsBehind(SkyFace* face) {
	for (int i = 0; i < 4; i++) {
		float a = (i == 1 || i == 2) ? 1.0f : -1.0f;
		float b = (i >= 2) ? 1.0f : -1.0f;
		if (vec3_dot(SkyFace_Dir(face, a, b), mainCamera.forward) >= SKY_NEAR) {
			return false;
		}
	}
	return true;
}

// Clips a convex polygon against the near plane. Both the direction and the
// texture coordinate are linear along an edge, and a clipped edge projects
// exactly (the projection is projective), so interpolating all three between
// the edge's endpoints is correct.
int SkyVertex_Clip(const SkyVertex* in, int n, SkyVertex* out) {
	int m = 0;
	for (int i = 0; i < n; i++) {
		const SkyVertex* a = &in[i];
		const SkyVertex* b = &in[(i + 1) % n];

		float da = vec3_dot(a->dir, mainCamera.forward) - SKY_NEAR;
		float db = vec3_dot(b->dir, mainCamera.forward) - SKY_NEAR;

		if (da >= 0) {
			out[m++] = *a;
		}
		if ((da >= 0) != (db >= 0)) {
			float t = da / (da - db);
			out[m].dir = vec3_add(a->dir, vec3_scale(vec3_sub(b->dir, a->dir), t));
			out[m].u = a->u + t * (b->u - a->u);
			out[m].v = a->v + t * (b->v - a->v);
			m++;
		}
	}
	return m;
}

void DrawSkyCell(Skybox* sb, int face, float a0, float a1, float b0, float b1) {
	SkyFace* f = &skyFaces[face];
	SDL_Texture* texture = sb->imgs[face];

	SkyVertex corners[4];
	int numInFront = 0;
	for (int i = 0; i < 4; i++) {
		float a = (i == 1 || i == 2) ? a1 : a0;
		float b = (i >= 2) ? b1 : b0;

		corners[i].dir = SkyFace_Dir(f, a, b);
		corners[i].u = f->u0 + f->uA * a + f->uB * b;
		corners[i].v = f->v0 + f->vA * a + f->vB * b;

		if (vec3_dot(corners[i].dir, mainCamera.forward) >= SKY_NEAR) {
			numInFront++;
		}
	}

	if (numInFront == 0) {
		return;
	}

	SkyVertex clipped[5];
	SkyVertex* poly = corners;
	int numCorners = 4;
	if (numInFront < 4) {
		numCorners = SkyVertex_Clip(corners, 4, clipped);
		poly = clipped;
		if (numCorners < 3) {
			return;
		}
	}

	SDL_Vertex v[5];
	float minX = 0, maxX = 0, minY = 0, maxY = 0;
	for (int i = 0; i < numCorners; i++) {
		float pf = vec3_dot(poly[i].dir, mainCamera.forward);
		float pr = vec3_dot(poly[i].dir, mainCamera.right);
		float pu = vec3_dot(poly[i].dir, mainCamera.up);

		float x = GAME_WIDTH / 2.0f + mainCamera.cam_dist * pr / pf;
		float y = GAME_HEIGHT / 2.0f - mainCamera.cam_dist * pu / pf;
		Renderer_Vertex(&v[i], x, y, poly[i].u, poly[i].v);

		if (i == 0 || x < minX) minX = x;
		if (i == 0 || x > maxX) maxX = x;
		if (i == 0 || y < minY) minY = y;
		if (i == 0 || y > maxY) maxY = y;
	}

	if (maxX < 0 || minX > GAME_WIDTH || maxY < 0 || minY > GAME_HEIGHT) {
		return;
	}

	for (int i = 1; i + 1 < numCorners; i++) {
		Renderer_Triangle(texture, &v[0], &v[i], &v[i + 1]);
	}
}

// Texture coordinates are interpolated across a triangle, so a whole face
// drawn as one quad would smear the image near its corners. Each face is
// therefore cut into a grid of small quads, which keeps the error well below
// a pixel and leaves the face boundaries exact.
#define SKY_FACE_CELLS 8

void DrawSky(Skybox* sb) {
	for (int face = 0; face < 6; face++) {
		if (SkyFace_IsBehind(&skyFaces[face])) {
			continue;
		}

		for (int i = 0; i < SKY_FACE_CELLS; i++) {
			float a0 = mapf(i, 0, SKY_FACE_CELLS, -1, 1);
			float a1 = mapf(i + 1, 0, SKY_FACE_CELLS, -1, 1);

			for (int j = 0; j < SKY_FACE_CELLS; j++) {
				float b0 = mapf(j, 0, SKY_FACE_CELLS, -1, 1);
				float b1 = mapf(j + 1, 0, SKY_FACE_CELLS, -1, 1);

				DrawSkyCell(sb, face, a0, a1, b0, b1);
			}
		}
	}

	Renderer_Flush();
}

// The ground point seen through pixel (j, y) is an affine function of j:
// value = valueAt0 + step * j. That holds because the camera basis is
// horizontal, so the distance to the floor only depends on the row.
bool Floor_Row(Camera* cam, float y, vec3* valueAt0, vec3* step) {
	float up = y * GAME_HEIGHT / 2;
	float dz = cam->forward.z * cam->cam_dist + cam->up.z * up;

	// At or above the horizon there is no floor. Note that dz only decreases
	// as y increases, so the rows below this one are fine.
	if (dz >= 0) {
		return false;
	}

	float t = -cam->position.z / dz;
	valueAt0->x = cam->position.x + t * (cam->forward.x * cam->cam_dist + up * cam->up.x - cam->right.x * GAME_WIDTH / 2);
	valueAt0->y = cam->position.y + t * (cam->forward.y * cam->cam_dist + up * cam->up.y - cam->right.y * GAME_WIDTH / 2);
	step->x = t * cam->right.x;
	step->y = t * cam->right.y;
	return true;
}

// Narrows [*j0, *j1] to the j where lo <= valueAt0 + step * j <= hi. Returns
// false if the range is empty.
bool Floor_ClipRange(float valueAt0, float step, float lo, float hi, float* j0, float* j1) {
	if (step == 0) {
		return valueAt0 >= lo && valueAt0 <= hi;
	}

	float a = (lo - valueAt0) / step;
	float b = (hi - valueAt0) / step;
	if (a > b) {
		float tmp = a;
		a = b;
		b = tmp;
	}

	if (a > *j0) *j0 = a;
	if (b < *j1) *j1 = b;
	return *j0 <= *j1;
}

void DrawFloor() {
	Camera* cam = &mainCamera;
	float size = 1 << track.size_log2;

	for (int i = 0; i < GAME_HEIGHT; i++) {
		vec3 top;
		vec3 topStep;
		if (!Floor_Row(cam, mapf(i, 0, GAME_HEIGHT, 1, -1), &top, &topStep)) {
			continue;
		}

		// Only draw the part of the row that lands on the track; the sky
		// stays visible everywhere else.
		float j0 = 0;
		float j1 = GAME_WIDTH;
		if (!Floor_ClipRange(top.x, topStep.x, 0, size, &j0, &j1)) {
			continue;
		}
		if (!Floor_ClipRange(top.y, topStep.y, 0, size, &j0, &j1)) {
			continue;
		}

		vec3 bottom;
		vec3 bottomStep;
		if (!Floor_Row(cam, mapf(i + 1, 0, GAME_HEIGHT, 1, -1), &bottom, &bottomStep)) {
			continue;
		}

		// One world unit is one texel, so the texture coordinates of a corner
		// are its world position on the track.
		SDL_Vertex corners[4];
		Renderer_Vertex(&corners[0], j0, i, (top.x + topStep.x * j0) / size, (top.y + topStep.y * j0) / size);
		Renderer_Vertex(&corners[1], j1, i, (top.x + topStep.x * j1) / size, (top.y + topStep.y * j1) / size);
		Renderer_Vertex(&corners[2], j1, i + 1, (bottom.x + bottomStep.x * j1) / size, (bottom.y + bottomStep.y * j1) / size);
		Renderer_Vertex(&corners[3], j0, i + 1, (bottom.x + bottomStep.x * j0) / size, (bottom.y + bottomStep.y * j0) / size);
		Renderer_Quad(track.trackImage, corners);
	}

	Renderer_Flush();
}

SpriteImage* Sprite_GetImage(const char* path) {
	for (int i = 0; i < numSpriteImages; i++) {
		if (strcmp(spriteImages[i].path, path) == 0) {
			return &spriteImages[i];
		}
	}

	if (numSpriteImages == MAX_SPRITE_IMAGES) {
		fprintf(stderr, "Ran out of sprite images\n");
		exit(EXIT_FAILURE);
	}

	SDL_Surface* surf = Asset_LoadSurface(path);
	if (surf == NULL) {
		fprintf(stderr, "Unable to load sprite: %s\n", IMG_GetError());
		exit(EXIT_FAILURE);
	}

	SDL_Surface* converted = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(surf);
	if (converted == NULL) {
		fprintf(stderr, "Unable to convert sprite: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
	}

	SpriteImage* image = &spriteImages[numSpriteImages++];
	snprintf(image->path, sizeof(image->path), "%s", path);
	image->w = converted->w;
	image->h = converted->h;
	image->numAngles = 1;
	image->texture = SDL_CreateTextureFromSurface(renderer, converted);
	SDL_FreeSurface(converted);

	if (image->texture == NULL) {
		fprintf(stderr, "Unable to upload sprite: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
	}
	SDL_SetTextureBlendMode(image->texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(image->texture, SDL_ScaleModeNearest);
	return image;
}

int AddSprite(const char* path) {
	if (numSprites == MAX_SPRITES) {
		fprintf(stderr, "Ran out of sprites\n");
		exit(EXIT_FAILURE);
	}

	sprites[numSprites].img = Sprite_GetImage(path);
	numSprites++;
	return numSprites - 1;
}

int AddSpriteRotations(const char* path) {
	if (numSprites == MAX_SPRITES) {
		fprintf(stderr, "Ran out of sprites\n");
		exit(EXIT_FAILURE);
	}

	SpriteImage* image = Sprite_GetImage(path);
	// The image is a strip of square frames, one per rotation.
	image->numAngles = image->w / image->h;
	image->w = image->h;

	sprites[numSprites].img = image;
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

	int ia = *(int*)a;
	int ib = *(int*)b;

	const Sprite* sa = &sprites[ia];
	const Sprite* sb = &sprites[ib];

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

	int drawOrder[MAX_SPRITES];
	for (int i = 0; i < numSprites; i++) {
		drawOrder[i] = i;
	}
	qsort(drawOrder, numSprites, sizeof(int), cmp_sprites);

	for (int i = 0; i < numSprites; i++) {
		Sprite* spr = &sprites[drawOrder[i]];
		SpriteImage* img = spr->img;

		int rotationIndex = 0;
		bool flipX = false;
		if (img->numAngles > 1) {
			// Select which sprite
			//rotationIndex = (frame / 10) % img->numAngles;

			vec2 diff = (vec2){spr->pos.x - cam->position.x, spr->pos.y - cam->position.y};
			vec2 right = (vec2){cam->right.x, cam->right.y};

			float theta = spr->angle - cam->yaw - atanf(GAME_WIDTH/(2*cam->cam_dist)* vec2_dot(diff, right) / vec2_dot(diff, cam->forward_2d));
			// printf("%f\n", theta);
			if (theta < 0) {
				flipX = true;
				theta = -theta;
			}
			rotationIndex = mapf(theta, 0, M_PI, 0, img->numAngles);
			rotationIndex %= img->numAngles * 2;
			// Must be ">=": rotationIndex == numAngles would otherwise select
			// a frame one past the end of the sprite sheet.
			if (rotationIndex >= img->numAngles) {
				rotationIndex = rotationIndex - img->numAngles;
				flipX = true;
			}
		}

		vec3 left3 = vec3_sub(spr->pos, vec3_scale(cam->right, img->w/2.0f));
		vec3 right3 = vec3_add(spr->pos, vec3_scale(cam->right, img->w/2.0f));

		vec3 top3 = vec3_add(spr->pos, vec3_scale(cam->up, img->h));

		vec2 left = ProjectPoint(left3);
		vec2 right = ProjectPoint(right3);
		vec2 top = ProjectPoint(top3);

		// A sprite is a flat quad facing the camera, so all of its corners are
		// the same distance away: behind the camera its projection is mirrored
		// and there is nothing sensible to draw.
		if (vec3_dot(vec3_sub(spr->pos, cam->position), cam->forward) <= 0.01f) {
			continue;
		}

		float x0 = left.x;
		float x1 = right.x;
		float y0 = top.y;
		float y1 = left.y;
		// (Also rejects NaN, which is what a degenerate projection produces.)
		if (!(x1 > x0) || !(y1 > y0)) {
			continue;
		}

		// Clip the sprite to the screen. The source rectangle is clipped by the
		// same fraction, so the sprite does not slide as it leaves the screen.
		float clipX0 = clampf(x0, 0, GAME_WIDTH);
		float clipX1 = clampf(x1, 0, GAME_WIDTH);
		float clipY0 = clampf(y0, 0, GAME_HEIGHT);
		float clipY1 = clampf(y1, 0, GAME_HEIGHT);
		if (clipX1 - clipX0 < 0.01f || clipY1 - clipY0 < 0.01f) {
			continue;
		}

		int tx0 = mapf(clipX0, x0, x1, 0, img->w);
		int ty0 = mapf(clipY0, y0, y1, 0, img->h);

		SDL_Rect src = {
			img->w * rotationIndex + tx0,
			ty0,
			(int)mapf(clipX1, x0, x1, 0, img->w) - tx0,
			(int)mapf(clipY1, y0, y1, 0, img->h) - ty0,
		};
		if (src.w <= 0 || src.h <= 0) {
			continue;
		}

		SDL_FRect dst = {clipX0, clipY0, clipX1 - clipX0, clipY1 - clipY0};
		SDL_RenderCopyExF(renderer, img->texture, &src, &dst, 0, NULL, flipX ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
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

int positions[3] = {0, 0, 0};

vec2 velocity = {0};
void UpdateFirstPersonCamera() {
	const float acceleration = 20;
	const float turnSpeed = deg2rad(90);
	
	Camera* cam = &mainCamera;
	PlayerState* ps = &mainPlayerState;

	// Must be initialised: the "green" branch below is commented out, so
	// drag would otherwise be read uninitialised.
	float drag = 15;
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

	// printf("Player velocity: %f\n", hypotf(velocity.x, velocity.y));

	cam->position.x += velocity.x;
	cam->position.y += velocity.y;

	// Check player markers

	for (int i = 0; i < ps->pathLen; i++) {
		vec2 pathPoint = ps->path[i];
		float dx = pathPoint.x - cam->position.x;
		float dy = pathPoint.y - cam->position.y;
		float d = hypotf(dx, dy);

		if (d < 100) {
			if (i == ps->markerNumber) {
				printf("Player passed marker %d\n", ps->markerNumber);
				ps->markerNumber++;

				if (ps->markerNumber == ps->pathLen) {
					ps->markerNumber = 0;
					printf("Lap\n");

					ps->lapNumber++;

					if (ps->lapNumber == 4) {
						int place = 1;
						for (int i = 0; i < NUM_ENEMIES; i++) {
							if (enemies[i].lap > 3) {
								place++;
							}
						}
						printf("Came %d\n", place);
						positions[trackNumber - 1] = place;
						Transition_init();
					}
				}
			}
		}
	}
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
	const uint8_t* kb = Platform_PollKeyboard(&numKeys);

	if (keyboardState == NULL) {
		keyboardState = malloc(numKeys);
		lastKeyboardState = malloc(numKeys);
	}

	memcpy(lastKeyboardState, keyboardState, numKeys);
	memcpy(keyboardState, kb, numKeys);
}

void Enemy_BeginPathSegment(Enemy* enemy, int n) {
	enemy->pathIndex = n;
	
	vec2 a = enemy->pos;
	vec2 b = enemy->path[n + 1 == enemy->pathLength ? 0 : n + 1];
	vec2 d = vec2_normalize(vec2_sub(b, a));
	enemy->dir = d;
}

void Enemy_Update(Enemy* enemy) {
	sprites[enemy->sprite].pos = (vec3){enemy->pos.x, enemy->pos.y, 0};

	if (enemy->pathIndex == -1) {
		Enemy_BeginPathSegment(enemy, 0);
	}

	vec2 old_pos = enemy->pos;
	enemy->pos.x += enemy->dir.x * enemy->speed * global_dt;
	enemy->pos.y += enemy->dir.y * enemy->speed * global_dt;

	float minx = fminf(old_pos.x, enemy->pos.x);
	float maxx = fmaxf(old_pos.x, enemy->pos.x);

	float miny = fminf(old_pos.y, enemy->pos.y);
	float maxy = fmaxf(old_pos.y, enemy->pos.y);

	minx -= 10;
	maxx += 10;

	miny -= 10;
	maxy += 10;

	int nextIndex = enemy->pathIndex + 1 == enemy->pathLength ? 0 : enemy->pathIndex + 1;
	vec2 pathPoint = enemy->path[nextIndex];

	if (pathPoint.x >= minx && pathPoint.x <= maxx && pathPoint.y >= miny && pathPoint.y <= maxy) {
		Enemy_BeginPathSegment(enemy, nextIndex);
		printf("Passed point %d\n", enemy->pathIndex);

		if (enemy->pathIndex == 0) {
			printf("Enemy lap\n");
			enemy->lap++;
		}
	}
}

typedef enum GameState {
	State_Menu,
	State_Transition,
	State_Game,
	State_Over,
} GameState;

GameState gameState;

SDL_Texture* logo;
SDL_Texture* logo_notext;

void Menu_init() {
	gameState = State_Menu;

	logo = Asset_LoadTexture(renderer, "logo.png");
	logo_notext = Asset_LoadTexture(renderer, "brummie.png");
	if (logo == NULL || logo_notext == NULL) {
		fprintf(stderr, "Unable to load menu textures: %s\n", IMG_GetError());
		exit(EXIT_FAILURE);
	}
}

void Menu_update() {
	if (keyboardState[SDL_SCANCODE_RETURN]) {
		Transition_init();
	}
}

void Menu_draw() {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	SDL_RenderCopy(renderer, logo, NULL, NULL);

	SDL_RenderPresent(renderer);
}

int transition_timer;

const char* trackNames[] = {
	"tracks/mario_circuit",
	"tracks/ghost",
	"tracks/rainbow"
};

void Transition_init() {
	gameState = State_Transition;

	// Release the previous track's surfaces, sprites and paths before loading
	// the next one. This used to leak everything on every track change.
	Track_Unload(&track);

	NextTrack();

	transition_timer = 60;
}

void NextTrack() {
	if (trackNumber == 3) {
		Over_init();
		return;
	}

	Track_Load(&track, trackNames[trackNumber]);
	trackNumber++;

}

void Transition_update() {
	if (transition_timer > 0) {
		transition_timer--;
	}
	if (transition_timer == 0) {
		Game_init();
	}
}

void Transition_draw() {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);
	
	SDL_RenderCopy(renderer, logo_notext, NULL, NULL);
	
	DrawStringInfo dsi = {
		.font = font,
		.colour = {0xff, 0xff, 0xff, 0xff},
		.x = GAME_WIDTH / 2,
		.y = GAME_HEIGHT / 2,
		.alignX = TEXT_ALIGN_CENTRE,
		.alignY = TEXT_ALIGN_CENTRE,
	};
	drawString(&dsi, track.trackName);

	SDL_RenderPresent(renderer);
}

void Game_init() {
	gameState = State_Game;
}

void Game_update() {
	UpdateCamera();

	for (int i = 0; i < NUM_ENEMIES; i++) {
		Enemy_Update(&enemies[i]);
	}
}

void Game_draw() {
	////////////////////
	// Prepare draw

	// Clear screen
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	DrawSky(&mainSkybox);
	DrawFloor();
	DrawSprites();

	DrawStringInfo dsi = {
		.font = font,
		.colour = {0xff, 0xff, 0xff, 0xff},
		.x = 0,
		.y = GAME_HEIGHT,
		.alignX = TEXT_ALIGN_LEFT,
		.alignY = TEXT_ALIGN_ABOVE,
	};
	drawStringf(&dsi, "Lap %d", mainPlayerState.lapNumber);


	SDL_RenderPresent(renderer);
}

void Over_init() {
	gameState = State_Over;
}

void Over_update() {

}

void Over_draw() {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);
	
	SDL_RenderCopy(renderer, logo_notext, NULL, NULL);
	
	DrawStringInfo dsi = {
		.font = font_small,
		.colour = {0xff, 0xff, 0xff, 0xff},
		.x = 0,
		.y = 0,
		.alignX = TEXT_ALIGN_LEFT,
		.alignY = TEXT_ALIGN_BELOW,
	};
	drawStringf(&dsi, "Thankyou for playing Super Brummie Kart. You came in places %d, %d, %d.", positions[0], positions[1], positions[2]);

	SDL_RenderPresent(renderer);
}

void update() {
	lastGlobalTime = globalTime;
	globalTime += global_dt;

	updateKeyboard();

	switch (gameState) {
	case State_Menu:
		Menu_update();
		break;
	case State_Transition:
		Transition_update();
		break;
	case State_Game:
		Game_update();
		break;
	case State_Over:
		Over_update();
		break;
	default:
		fprintf(stderr, "Invalid game state %d\n", gameState);
		exit(EXIT_FAILURE);
	}

	// printf("%f %f %f\n", mainCamera.position.x, mainCamera.position.y, mainCamera.position.z);
}

void draw() {
	switch (gameState) {
	case State_Menu:
		Menu_draw();
		break;
	case State_Transition:
		Transition_draw();
		break;
	case State_Game:
		Game_draw();
		break;
	case State_Over:
		Over_draw();
		break;
	default:
		fprintf(stderr, "Invalid game state %d\n", gameState);
		exit(EXIT_FAILURE);
	}
}

// One frame of the game: consume input, update the world and present it.
// This is split out of main() so the Emscripten build can drive it from the
// browser's requestAnimationFrame callback via emscripten_set_main_loop().
static void MainLoopIteration(void) {
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

	if (!gameRunning) {
#ifdef __EMSCRIPTEN__
		// The loop is owned by the browser, so stop it explicitly.
		emscripten_cancel_main_loop();
#endif
	}
}

int main() {
	Platform_Init();

	SDL_Init(Platform_GetSubsystemFlags());
	IMG_Init(IMG_INIT_PNG);
	TTF_Init();

    SDL_version sdlVersion;
    SDL_GetVersion(&sdlVersion);
    printf("SDL Version: %d.%d.%d\n", sdlVersion.major, sdlVersion.minor, sdlVersion.patch);

    // Console targets render to one fixed framebuffer, so the window size is up
    // to the platform rather than the game.
    int windowWidth;
    int windowHeight;
    Platform_GetWindowSize(GAME_WIDTH, GAME_HEIGHT, &windowWidth, &windowHeight);

    window = SDL_CreateWindow("Super Brummie Kart", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowWidth, windowHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "Unable to create window: %s\n", SDL_GetError());
		return EXIT_FAILURE;
	}

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	if (renderer == NULL) {
		fprintf(stderr, "Unable to create renderer: %s\n", SDL_GetError());
		return EXIT_FAILURE;
	}

	SDL_RenderSetLogicalSize(renderer, GAME_WIDTH, GAME_HEIGHT);
	SDL_RenderSetIntegerScale(renderer, true);

	SDL_SetRelativeMouseMode(true);

	const char* skyboxPaths[6] = {
		"skybox/+x.png",
		"skybox/-x.png",
		"skybox/+y.png",
		"skybox/-y.png",
		"skybox/+z.png",
		"skybox/-z.png",
	};
	LoadSkybox(&mainSkybox, skyboxPaths);

	font = Asset_OpenFont("Blinker-Regular.ttf", 72);
	font_small = Asset_OpenFont("Blinker-Regular.ttf", 20);
	if (font == NULL || font_small == NULL) {
		fprintf(stderr, "Unable to load font: %s\n", TTF_GetError());
		return EXIT_FAILURE;
	}
	
	Menu_init();
	// Game_init();

	gameRunning = true;
	frame = 0;

#ifdef __EMSCRIPTEN__
	// The browser can only present a frame from within an animation frame, so
	// hand the loop over to Emscripten instead of blocking here. This call does
	// not return (simulate_infinite_loop = 1).
	emscripten_set_main_loop(MainLoopIteration, 0, 1);
#else
	while (gameRunning) {
		MainLoopIteration();
	}
#endif

	// Cleanup
	Track_Unload(&track);
	UnloadSkybox(&mainSkybox);
	SDL_DestroyTexture(logo);
	SDL_DestroyTexture(logo_notext);
	TTF_CloseFont(font);
	TTF_CloseFont(font_small);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	TTF_Quit();
	IMG_Quit();
	SDL_Quit();

	// The Wii powers off inside this call, so it never returns there.
	Platform_Exit();

    return 0;
}
