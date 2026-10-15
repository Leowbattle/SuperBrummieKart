# SuperBrummieKart

My project for HackTheMidlands 7.0

Winner of 2nd prize overall and Spooooookiest Hack

https://devpost.com/software/super-brummie-kart

## Playing

| Action | Keyboard | Touch |
| --- | --- | --- |
| Start / confirm | `Enter` | "Tap to start" |
| Accelerate | `W` | Accelerate pad |
| Brake / reverse | `S` | Brake pad |
| Steer left / right | `A` / `D` | Left / right pads |

The web build adds a footer bar with two buttons:

- **Fullscreen (F)** — takes the game fullscreen. The canvas is fitted to the
  window, so a phone in landscape fills the screen and a phone in portrait
  letterboxes.
- **Show or hide touch controls (H)** — shows the on-screen pads, which can be
  useful on a desktop too, since they can be driven with the mouse.

While racing, the browser is asked for the pointer lock so the mouse can look
around. Clicks then land on the canvas rather than the buttons, so click once
(or press `Esc`) to get the cursor back; the `F` and `H` shortcuts keep working
either way. On a touch screen the pads appear as soon as the race starts, and
"Tap to start" enters fullscreen at the same time.

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

The web build uses the custom Emscripten shell in
[`shell.html`](./shell.html) rather than the default one. The default shell's
fullscreen helper sizes the canvas from `widthNative`, which an SDL2 build never
sets, so entering fullscreen blanked the canvas to 0x0; it also asked for the
pointer lock without handling a refusal, which surfaced as "Exception thrown,
see JavaScript console". The custom shell fits the canvas to the window in CSS,
fullscreens the game element itself, reports failures in a banner rather than
the console, and provides the touch controls, which reach the game through
`Touch_SetInput`.

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
