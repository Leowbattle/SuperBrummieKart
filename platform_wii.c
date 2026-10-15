// Nintendo Wii implementation of the platform layer.
//
// Two things are fundamentally different from the desktop build: there is no
// filesystem, so the assets are linked into the executable and read back through
// memory-backed stdio, and there is no keyboard, so the GameCube pad and the Wii
// Remote are translated into the SDL scancodes the game already reads (which
// keeps the game logic itself free of console-specific code).

#include "platform.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reent.h>
#include <sys/types.h>

#include <SDL2/SDL_image.h>

#include <gccore.h>
#include <wiikeyboard/keyboard.h>
#include <wiiuse/wpad.h>

#include "embedded_assets.h"

// The GameCube stick reports -128..127 around its centre, the nunchuk and
// classic controller sticks report 0..255 around their calibrated centre and
// only reliably reach +/-96 of it.
#define GC_DEADZONE 30
#define EXPANSION_DEADZONE 45

static uint8_t keyboardState[SDL_NUM_SCANCODES];

static char logLine[256];
static size_t logLineLength;

// Newlib sends everything printf()/fprintf() writes to the console here. The
// debug report channel the emulator and the USB Gecko read only flushes a line
// when a newline arrives, so complete lines are buffered and reported one at a
// time; a trailing part-line is reported by Platform_Exit().
static void Log_FullLine(void) {
	logLine[logLineLength] = '\0';
	SYS_Report("%s\n", logLine);
	logLineLength = 0;
}

ssize_t _write_r(struct _reent* reent, int fd, const void* buf, size_t len) {
	(void)reent;
	(void)fd;

	const char* text = buf;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n') {
			Log_FullLine();
		} else if (logLineLength == sizeof(logLine) - 1) {
			Log_FullLine();
			logLine[logLineLength++] = text[i];
		} else {
			logLine[logLineLength++] = text[i];
		}
	}

	return (ssize_t)len;
}

// The port only renders through SDL and polls events through it: the input comes
// from the GameCube pad and the Wii Remote directly, and the game has no sound,
// so the subsystems that are unused on the console are left off.
uint32_t Platform_GetSubsystemFlags(void) {
	return SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS;
}

void Platform_Init(void) {
	// The game polls the pad and the remote itself, so both drivers have to be up
	// before the first poll. SDL's joystick subsystem used to do this as a side
	// effect, but it is not enabled here, and an uninitialised driver does not
	// merely report no input: PAD_Read() dispatches through a status converter
	// that only PAD_Init() installs, so the first scan of a connected controller
	// calls a NULL function pointer.
	if (PAD_Init() == 0) {
		fprintf(stderr, "Warning: could not initialise the GameCube pad driver\n");
	}
	if (WPAD_Init() != WPAD_ERR_NONE) {
		fprintf(stderr, "Warning: could not initialise the Wii Remote driver\n");
	}

	// SDL's Wii event pump reads the console's USB keyboard unconditionally, by
	// calling libogc's KEYBOARD_GetEvent(), but libogc only creates that driver's
	// event queue in KEYBOARD_Init(). Without this call every pumped event walks a
	// NULL queue, which trips the MMU in an emulator and crashes real hardware.
	// There is no callback to install, the driver is only being made usable.
	if (KEYBOARD_Init(NULL) != 0) {
		fprintf(stderr, "Warning: could not initialise the Wii keyboard driver\n");
	}

	// Without a terminal newlib would buffer the output until the process exits,
	// and the console is powered off rather than exited, so keep both streams
	// unbuffered.
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
}

void Platform_Exit(void) {
	// Report any message that was printed without a trailing newline, then power
	// the console off the way the System Menu does so the emulator shuts down
	// cleanly too.
	if (logLineLength > 0) {
		Log_FullLine();
	}
	SYS_ResetSystem(SYS_POWEROFF, 0, 0);
}

static const uint8_t* Asset_Data(const EmbeddedAsset* asset) {
	return embedded_assets_dat + asset->offset;
}

FILE* Asset_OpenFile(const char* path) {
	const EmbeddedAsset* asset = EmbeddedAssets_Find(path);
	if (asset == NULL) {
		return NULL;
	}
	// fmemopen() never writes to the buffer when opened for reading, so dropping
	// the const here is safe.
	return fmemopen((void*)Asset_Data(asset), asset->size, "r");
}

// Wraps an embedded asset in an SDL_RWops for the loaders that take a stream. The
// caller passes freeSrc = 1 to the SDL_*_RW function, which is safe because the
// asset data itself lives in read-only memory.
static SDL_RWops* Asset_OpenRW(const char* path) {
	const EmbeddedAsset* asset = EmbeddedAssets_Find(path);
	if (asset == NULL) {
		return NULL;
	}
	return SDL_RWFromConstMem(Asset_Data(asset), (int)asset->size);
}

SDL_Surface* Asset_LoadSurface(const char* path) {
	SDL_RWops* rw = Asset_OpenRW(path);
	return rw == NULL ? NULL : IMG_Load_RW(rw, 1);
}

SDL_Texture* Asset_LoadTexture(SDL_Renderer* renderer, const char* path) {
	SDL_RWops* rw = Asset_OpenRW(path);
	return rw == NULL ? NULL : IMG_LoadTexture_RW(renderer, rw, 1);
}

TTF_Font* Asset_OpenFont(const char* path, int pointSize) {
	SDL_RWops* rw = Asset_OpenRW(path);
	return rw == NULL ? NULL : TTF_OpenFontRW(rw, 1, pointSize);
}

void Platform_GetWindowSize(int gameWidth, int gameHeight, int* windowWidth, int* windowHeight) {
	(void)gameWidth;
	(void)gameHeight;

	// The Wii renders to one fixed framebuffer, so make the window fill it and
	// let SDL_RenderSetLogicalSize() letterbox the game's 640x360 inside it.
	SDL_DisplayMode displayMode;
	if (SDL_GetDesktopDisplayMode(0, &displayMode) == 0) {
		*windowWidth = displayMode.w;
		*windowHeight = displayMode.h;
		return;
	}

	GXRModeObj* videoMode = VIDEO_GetPreferredMode(NULL);
	*windowWidth = videoMode->fbWidth;
	*windowHeight = videoMode->efbHeight;
}

static void SetKey(int scancode, bool held) {
	if (held) {
		keyboardState[scancode] = SDL_PRESSED;
	}
}

// Stick reading relative to the stick's calibrated centre. The expansion sticks
// are reported as unsigned bytes that grow downwards, so their vertical axis is
// flipped to match the GameCube stick (positive is up).
static int StickOffset(int value, int centre, bool flip) {
	int offset = value - centre;
	return flip ? -offset : offset;
}

// Maps the GameCube pad and the Wii Remote (with or without its nunchuk or
// classic controller) onto the scancodes the game reads:
//
//   W  accelerate: GC A, remote 2, nunchuk Z, classic A or R
//   S  brake:      GC B, remote 1, nunchuk C, classic B or L
//   A/D steer:     left stick or D-pad left/right
//   SPACE/LSHIFT   fly up/down: left stick or D-pad up/down, GC X/Y, remote A/B
//   I/K            field of view: GC or classic right stick left/right
//   RETURN         start the menu: GC START, remote/classic PLUS, or accelerate
//
// Holding L+R+START on the GameCube pad, MINUS+PLUS on the remote or L+R+PLUS on
// the classic controller quits the game (HOME is deliberately left alone as the
// emulator and the console both use it for their own menu).
const uint8_t* Platform_PollKeyboard(int* numKeys) {
	PAD_ScanPads();
	WPAD_ScanPads();

	memset(keyboardState, 0, sizeof(keyboardState));
	*numKeys = SDL_NUM_SCANCODES;

	const u16 pad = PAD_ButtonsHeld(0);
	const s8 padStickX = PAD_StickX(0);
	const s8 padStickY = PAD_StickY(0);
	const s8 padSubStickX = PAD_SubStickX(0);
	const u32 remote = WPAD_ButtonsHeld(0);

	struct expansion_t expansion;
	WPAD_Expansion(0, &expansion);

	// Whichever stick is pushed further steers, so an idle controller sitting at
	// its centre never cancels the other one out.
	int stickX = padStickX;
	int stickY = padStickY;
	int stickXDeadzone = GC_DEADZONE;
	int stickYDeadzone = GC_DEADZONE;
	int expansionX = 0;
	int expansionY = 0;
	if (expansion.type == WPAD_EXP_NUNCHUK) {
		expansionX = StickOffset(expansion.nunchuk.js.pos.x, expansion.nunchuk.js.center.x, false);
		expansionY = StickOffset(expansion.nunchuk.js.pos.y, expansion.nunchuk.js.center.y, true);
	} else if (expansion.type == WPAD_EXP_CLASSIC) {
		expansionX = StickOffset(expansion.classic.ljs.pos.x, expansion.classic.ljs.center.x, false);
		expansionY = StickOffset(expansion.classic.ljs.pos.y, expansion.classic.ljs.center.y, true);
	}
	if (abs(expansionX) > abs(stickX)) {
		stickX = expansionX;
		stickXDeadzone = EXPANSION_DEADZONE;
	}
	if (abs(expansionY) > abs(stickY)) {
		stickY = expansionY;
		stickYDeadzone = EXPANSION_DEADZONE;
	}

	const bool accelerate = (pad & PAD_BUTTON_A) ||
	                        (remote & (WPAD_BUTTON_2 | WPAD_NUNCHUK_BUTTON_Z |
	                                   WPAD_CLASSIC_BUTTON_A | WPAD_CLASSIC_BUTTON_FULL_R));
	const bool brake = (pad & PAD_BUTTON_B) ||
	                   (remote & (WPAD_BUTTON_1 | WPAD_NUNCHUK_BUTTON_C |
	                              WPAD_CLASSIC_BUTTON_B | WPAD_CLASSIC_BUTTON_FULL_L));
	SetKey(SDL_SCANCODE_W, accelerate);
	SetKey(SDL_SCANCODE_S, brake);

	const bool steerLeft = (pad & PAD_BUTTON_LEFT) ||
	                       (remote & (WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT)) ||
	                       stickX < -stickXDeadzone;
	const bool steerRight = (pad & PAD_BUTTON_RIGHT) ||
	                        (remote & (WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT)) ||
	                        stickX > stickXDeadzone;
	SetKey(SDL_SCANCODE_A, steerLeft);
	SetKey(SDL_SCANCODE_D, steerRight);

	const bool flyUp = (pad & (PAD_BUTTON_UP | PAD_BUTTON_X)) ||
	                   (remote & (WPAD_BUTTON_UP | WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_UP |
	                              WPAD_CLASSIC_BUTTON_X)) ||
	                   stickY > stickYDeadzone;
	const bool flyDown = (pad & (PAD_BUTTON_DOWN | PAD_BUTTON_Y)) ||
	                     (remote & (WPAD_BUTTON_DOWN | WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_DOWN |
	                                WPAD_CLASSIC_BUTTON_Y)) ||
	                     stickY < -stickYDeadzone;
	SetKey(SDL_SCANCODE_SPACE, flyUp);
	SetKey(SDL_SCANCODE_LSHIFT, flyDown);

	SetKey(SDL_SCANCODE_I, padSubStickX < -GC_DEADZONE);
	SetKey(SDL_SCANCODE_K, padSubStickX > GC_DEADZONE);

	// The menu only reacts to Return, so on the console accelerating starts the
	// game too.
	const bool confirm = (pad & PAD_BUTTON_START) ||
	                     (remote & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS)) || accelerate;
	SetKey(SDL_SCANCODE_RETURN, confirm);

	const u16 padQuit = PAD_TRIGGER_L | PAD_TRIGGER_R | PAD_BUTTON_START;
	const u32 remoteQuit = WPAD_BUTTON_MINUS | WPAD_BUTTON_PLUS;
	const u32 classicQuit = WPAD_CLASSIC_BUTTON_FULL_L | WPAD_CLASSIC_BUTTON_FULL_R |
	                        WPAD_CLASSIC_BUTTON_PLUS;
	const bool quit = (pad & padQuit) == padQuit || (remote & remoteQuit) == remoteQuit ||
	                  (remote & classicQuit) == classicQuit;
	if (quit) {
		// The main loop already knows how to stop for a window close, so quit by
		// asking for one instead of duplicating the exit path here.
		SDL_Event event;
		event.type = SDL_QUIT;
		SDL_PushEvent(&event);
	}

	return keyboardState;
}
