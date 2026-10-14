# SuperBrummieKart

My project for HackTheMidlands 7.0

Winner of 2nd prize overall and Spooooookiest Hack

https://devpost.com/software/super-brummie-kart

## Building

The game is built with [CMake](https://cmake.org/) and can target either the
desktop (SDL2) or the web (Emscripten).

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

### Convenience wrapper

`./compile.sh` is a thin wrapper around the desktop CMake build.

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
