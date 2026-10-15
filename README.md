# SuperBrummieKart

My project for HackTheMidlands 7.0

Winner of 2nd prize overall and Spooooookiest Hack

https://devpost.com/software/super-brummie-kart

## Building

The game is built with [CMake](https://cmake.org/) and can target the desktop
(SDL2), the web (Emscripten) or the Nintendo Wii (devkitPro/libogc).

### Desktop (Linux)

Requires a C compiler, CMake, pkg-config and the SDL2 development packages:

```sh
# Debian / Ubuntu
sudo apt-get install build-essential cmake pkg-config \
    libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run from the source directory so the relative asset paths resolve.
./build/superbrummiekart
```

To produce a self-contained, runnable folder (binary + all assets):

```sh
cmake --install build --prefix dist
./dist/superbrummiekart
```

### Web (Emscripten)

Install and activate the
[emsdk](https://emscripten.org/docs/getting_started/downloads.html), then:

```sh
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --parallel
```

This produces `build-web/index.html`, `index.js`, `index.wasm` and `index.data`.
The web build preloads all of the game's assets (images, font, skybox and
tracks) into the virtual filesystem, so no extra files are needed at runtime.

A `file://` URL will not work because the `.wasm`/`.data` files are fetched over
HTTP — serve the directory instead:

```sh
python3 -m http.server -d build-web 8000
# then open http://localhost:8000/index.html
```

### Nintendo Wii

The Wii build needs the [devkitPro](https://devkitpro.org/) toolchain for the
`wii` group - devkitPPC, libogc with its OpenGX and libfat libraries, and the
SDL2, SDL2_image and SDL2_ttf portlibs. The official installer always writes to
`/opt/devkitpro` and therefore needs root; `scripts/install-devkitpro-wii.py`
does the same job into a prefix you own, verifying the SHA256 of every package it
downloads:

```sh
scripts/install-devkitpro-wii.py        # installs into ~/devkitpro by default
```

Then build (this finds devkitPro in `$DEVKITPRO`, `/opt/devkitpro` or
`~/devkitpro`):

```sh
./compile-wii.sh
# creates build-wii/superbrummiekart.dol
```

The console build has no working directory to load assets from, so every asset is
linked into the executable and read back through memory-backed stdio;
`scripts/embed-assets.py` generates the blob during the build. There is also no
keyboard, so the GameCube pad, the Wii Remote and its nunchuk/classic controller
expansions are translated into the keyboard scancodes the game already reads:

| Game action   | GameCube pad          | Wii Remote                      |
| ------------- | --------------------- | ------------------------------- |
| Accelerate    | A                     | 2, nunchuk Z, classic A / R     |
| Brake/reverse | B                     | 1, nunchuk C, classic B / L     |
| Steer         | left stick, D-pad     | D-pad, nunchuk or classic stick |
| Fly up/down   | X / Y, stick up/down  | A / B, stick up/down            |
| Zoom camera   | C-stick left/right    | -                               |
| Start race    | START (or A)          | PLUS (or 2)                     |
| Quit          | L + R + START         | MINUS + PLUS, classic L + R + PLUS |

Runs in the [Dolphin](https://dolphin-emu.org/) emulator:

```sh
scripts/configure-dolphin-automation.py                 # once: lets a run finish by itself
scripts/run-in-dolphin.sh build-wii/superbrummiekart.dol
```

`run-in-dolphin.sh` runs the game headless by default - Dolphin's `headless`
window platform renders offscreen, so no window appears and no display server is
needed - and prints the game's log output when it is done. Pass `--window` to
watch it, `--seconds N` to change the timeout, or `--dump-frames` to save every
frame as a PNG. When the game exits it powers the console off, which shuts
Dolphin down as well.

`configure-dolphin-automation.py` is what makes an unattended run possible: it
disables the "are you sure you want to stop emulation?" and panic dialogs, stops
the emulator pausing when it loses focus, and turns on logging, without which a
failed run leaves no output behind.

### Rendering

The 3D scene - skybox, floor and sprites - is submitted to the renderer as
textured geometry (`SDL_RenderGeometry()` and `SDL_RenderCopyExF()`) rather than
being rasterised on the CPU a pixel at a time, so the GPU does the per-pixel
work. This needs SDL 2.0.18 or newer.

On the Wii those calls go through devkitPro's SDL2 to libogc's OGC renderer and
the GX hardware; drawing a frame then costs about a millisecond, and the game
runs at a vsync-locked 60 fps in Dolphin.

### Convenience wrappers

`./compile.sh` is a thin wrapper around the desktop CMake build, `compile-wii.sh`
around the Wii one.

## Continuous integration

`.github/workflows/build.yml` builds both targets on every push and pull
request and uploads them as workflow artifacts:

- **`SuperBrummieKart-linux-x86_64`** — a `.tar.gz` containing the binary and
  its assets.
- **`SuperBrummieKart-web`** — a `.zip` containing `index.html`, `index.js`,
  `index.wasm` and `index.data`.

On pushes to `master`, the web build is also published to GitHub Pages. Enable
this once under **Settings → Pages → Build and deployment → Source: GitHub
Actions**.
