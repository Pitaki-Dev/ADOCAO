# ADOCAO - A Dance of C++ and OpenGL

A native high-performance viewer for [A Dance of Fire and Ice](https://store.steampowered.com/app/977950/A_Dance_of_Fire_and_Ice/) custom levels (`.adofai` format), written in C++20 and OpenGL 4.3.

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![OpenGL](https://img.shields.io/badge/OpenGL-4.3-red)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)

A [Vulkan 1.2 port](https://github.com/CHT-1192/ADOCAV) is also available.

## Features

- Full ADOFAI playback engine with relative angle computation (matches [ADOFAI-JS](https://github.com/adofaiex/ADOFAI-JS))
- Music playback via miniaudio (WASAPI/PulseAudio): AIFF, OGG, WAV, FLAC
- Pre-synthesized hitsound tracks (27 hit types) with 16-bit hard-clip mixing
- Instanced GPU rendering with frustum culling and visibility cache
- Per-instance fill and stroke colors via vertex attributes
- Planet movement with trail rendering (Catmull-Rom spline)
- Event icons: Twirl (purple), SetSpeed (red/blue)
- Z-depth rendering (far-to-near tile ordering, supports 7M-tile levels)
- Bookmark navigation (Ctrl+Left/Right) + track selection (click to play from tile)
- DPI-aware launcher with auto-stroke color, background color, and resolution selection
- `SetSpeed`, `Twirl`, `Pause`, `Midspin` event support
- `--auto-play` for automatic playback
- `--force-hitsound` to override level hitsound type
- `--legacy-culling` for legacy per-instance culling (vs SoA SIMD culling)

## Quick Start

### Windows
```bash
build.bat                     # default: Ultra zoom (min 1x), dynamic link
build.bat -Hyper              # Hyper zoom (min 0.5x)
build.bat -Portable           # static-linked portable build
build.bat -P -H               # short form: portable + Hyper
```
All arguments skip interactive mode. Run without arguments for prompts.

### Linux / macOS
```bash
chmod +x build.sh
./build.sh                    # default: Ultra zoom (min 1x)
./build.sh -Hyper             # Hyper zoom (min 0.5x)
./build.sh -Extreme -Portable # Extreme zoom + portable
```
Re-running `./build.sh` without arguments when `build/` already has a cache: rebuilds **incrementally** — no prompts, no re-configure. Pass options (e.g. `-U -Portable`) to reconfigure.

### Zoom Levels

Zoom controls how far out you can scroll. Smaller = further out, good for huge levels.

| Level | Min Zoom | Long Arg | Short | Description |
|-------|----------|----------|-------|-------------|
| Normal | 10× | `-Normal` | `-N` | Standard zoom range |
| Extra | 5× | `-Extra` | `-T` | Slightly wider view |
| Super | 2.5× | `-Super` | `-S` | Good for medium-large levels |
| **Ultra** | **1×** | **`-Ultra`** | **`-U`** | **Default. Covers most extreme levels** |
| Hyper | 0.5× | `-Hyper` | `-H` | Very wide, 2× further than Ultra |
| Extreme | 0.25× | `-Extreme` | `-X` | 4× further than Ultra |
| Unimaginable | 0.1× | `-Unimaginable` | `-I` | Maximum zoom-out, 10× further than Ultra |

### Build Script Arguments

| Argument | Short | Description |
|----------|-------|-------------|
| `-Portable` | `-P` | Static-link MinGW DLLs (no runtime dependency) |
| `-Normal` | `-N` | Min zoom 10× |
| `-Extra` | `-T` | Min zoom 5× |
| `-Super` | `-S` | Min zoom 2.5× |
| `-Ultra` | `-U` | Min zoom 1× (default) |
| `-Hyper` | `-H` | Min zoom 0.5× |
| `-Extreme` | `-X` | Min zoom 0.25× |
| `-Unimaginable` | `-I` | Min zoom 0.1× |

Arguments can be combined and are case-insensitive. Any argument skips interactive prompts.

### CMake Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `ADOCAO_PORTABLE` | BOOL | OFF | Static-link portable build |
| `ADOCAO_ZOOM_LEVEL` | STRING | Ultra | Min zoom level. One of: `Normal`, `Extra`, `Super`, `Ultra`, `Hyper`, `Extreme`, `Unimaginable` |

Manual CMake example:
```bash
cmake .. -DADOCAO_PORTABLE=ON -DADOCAO_ZOOM_LEVEL=Hyper
```

## Build Dependencies

All dependencies are fetched automatically via CMake `FetchContent`:

| Library | Version | Purpose |
|---------|---------|---------|
| GLFW | 3.4 | Window + input |
| glm | 1.0.1 | Math |
| RapidJSON | master | .adofai parsing |
| Dear ImGui | 1.91.9 | Launcher UI |
| miniaudio | 0.11.22 | Audio playback |
| stb_vorbis | latest | OGG decoding |
| miniz | 3.0.2 | Zip archive reading (DataFile) |
| tinyfiledialogs | 2.9.3 | File open dialogs |

## CLI Usage

```
adocao.exe --level <file> --music <file> [--width N] [--height N]
           [--fullscreen] [--fill HEX] [--stroke HEX] [--bg HEX]
           [--no-auto-stroke] [--no-hitsound] [--no-trail] [--debug]
           [--force-hitsound [TYPE]] [--auto-play] [--export] [--legacy-culling]
           [--msaa N] [--exclusive | --no-exclusive]
           [--trail-duration SEC] [--trail-sample-rate N]
           [--render FILE] [--fps N] [--crf N] [--tail SEC]
```

Without `--level`, falls through to the ImGui launcher.

## Video export

`--render <file.mp4>` writes the level straight to a video file instead of
opening a window. The level is played on a **deterministic clock** — frame *N*
is always at `t = N / fps` — so the output does not depend on how fast the
machine can draw, and there are no dropped or duplicated frames:

```bash
./build/ADOCAO --level level.adofai --music song.ogg \
               --render out.mp4 --width 1920 --height 1080 --fps 60
```

Rendering happens into an offscreen framebuffer and every frame is piped to
`ffmpeg`; afterwards the music and the synthesized hitsounds are muxed in as
audio tracks. `ffmpeg` must be on `PATH` — there is no libav dependency.
A GL context is still required, so a display (real, or `xvfb-run`) must exist:

```bash
xvfb-run -a -s "-screen 0 1280x720x24" ./build/ADOCAO ... --render out.mp4
```

Encoding is **on the GPU by default**. `--encoder auto` (the default) uses
`h264_nvenc` when the OpenGL renderer reports an NVIDIA GPU and the local
ffmpeg has that encoder; otherwise it falls back to `libx264` on the CPU.
A software rasteriser (`llvmpipe` / `softpipe`, i.e. CI runners) is detected
and reported in `ADOCAO.log` along with the chosen encoder.

```
--encoder auto | libx264 | h264_nvenc | h264_qsv | h264_amf | ...
```

`--crf` is interpreted per encoder family: `-crf` for x264/x265,
`-cq` (+`-b:v 0`) for NVENC, `-global_quality` for QSV, `-qp_i/-qp_p` for AMF.

Anti-aliasing comes from **supersampling**: `--ssaa N` draws into an `N×`
larger framebuffer and ffmpeg downscales with lanczos. The offscreen FBO has no
MSAA, so this is the only AA available — and it also removes the sub-pixel
speckle you get from events (e.g. a red SetSpeed-up icon is 0.22 world units,
i.e. half a pixel at `--zoom 2.5` on 1080p).

```
--ssaa 1 | 2 | 3 | 4      (1 = off; 2 costs 4× the fill rate)
```

`.github/workflows/render-video.yml` wraps all of this so a level can be
rendered to an MP4 on a CI runner and downloaded as an artifact.


## Project Structure

```
app/         Application layer (windows, launcher, game loop)
audio/       Music playback + hitsound synthesis (miniaudio)
camera/      Orthographic camera with frustum culling
game/        Planet rendering + playback engine
glad/        OpenGL 4.3 Core loader (custom minimal loader)
level/       .adofai parser + JSON cleaner
render/      Shader programs + planet trail rendering
shaders/     GLSL shader source files (.vert / .frag)
track/       Tile mesh generation + instanced rendering
util/        Logger + easing functions
hitsounds/   27 hit sound .wav files
```

## Controls

| Key | Action |
|-----|--------|
| Space | Start/stop playback |
| Esc | Close game window |
| Mouse drag | Pan camera (when stopped) |
| Scroll | Zoom in/out (5–1000) |

## Acknowledgements

This project draws heavily from:
- [Re_ADOJAS](https://github.com/adofaiex/Re_ADOJAS) — Three.js web player
- [ADOFAN_PIXI](https://github.com/AnStartist/ADOFAN_PIXI) — PixiJS web player

Thank you to their authors for the reference implementations.

## License

Apache 2.0
