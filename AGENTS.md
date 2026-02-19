# AGENTS.md — dhewm3

## Project Overview

dhewm3 is a Doom 3 GPL source port using SDL, OpenAL, and CMake. The engine
source lives in `neo/`, with game code in `neo/game/` (base) and `neo/d3xp/`
(Resurrection of Evil expansion). The build produces four artifacts: the main
executable (`dhewm3`), a dedicated server (`dhewm3ded`), and two game shared
libraries (`base.so`, `d3xp.so`).

## Repository Layout

```
neo/                  # All engine and game source code
  CMakeLists.txt      # Main build configuration
  idlib/              # Core library (math, containers, SIMD)
  framework/          # Engine framework (cvars, filesystem, console, networking)
  renderer/           # OpenGL rendering system
  sound/              # OpenAL audio system
  ui/                 # UI system
  cm/                 # Collision model
  game/               # Base game code (compiles to base.so)
  d3xp/               # Expansion game code (compiles to d3xp.so)
  sys/                # Platform-specific code (linux, win32, osx, posix)
    cmake/            # CMake find-modules (e.g. FindSDL2.cmake)
  tools/              # Editor tools (map compiler, radiant, etc.)
  libs/               # Bundled third-party code (imgui)
  TypeInfo/           # Type information generator
base/                 # Runtime config files (gamepad.cfg, default.cfg)
docs/                 # Developer documentation
.github/workflows/    # CI workflows (linux, macos, win_msvc)
```

## System Dependencies (Ubuntu/Debian)

```sh
sudo apt update
sudo apt install \
  git cmake build-essential ninja-build \
  libgl1-mesa-dev libsdl2-dev libopenal-dev libcurl4-openssl-dev
```

- `build-essential` — GCC, g++, GNU Make, glibc headers
- `cmake` — Build system generator (minimum version 2.6)
- `ninja-build` — Fast build executor (optional, can use Make instead)
- `libgl1-mesa-dev` — OpenGL headers
- `libsdl2-dev` — SDL2 for windowing, input, and platform abstraction
- `libopenal-dev` — OpenAL Soft for audio
- `libcurl4-openssl-dev` — libcurl for HTTP downloads (optional)

## Build Instructions

Build out-of-source. CMake is pointed at the `neo/` directory:

```sh
mkdir -p /tmp/dhewm3-build && cd /tmp/dhewm3-build
cmake -G Ninja -DDEDICATED=ON /home/user/dhewm3/neo/
ninja
```

Or using Make instead of Ninja:

```sh
mkdir -p /tmp/dhewm3-build && cd /tmp/dhewm3-build
cmake -DDEDICATED=ON /home/user/dhewm3/neo/
make -j$(nproc)
```

### Build Artifacts

After a successful build, the output directory contains:

| File         | Description                              |
|--------------|------------------------------------------|
| `dhewm3`    | Main game executable                     |
| `dhewm3ded` | Dedicated server (requires `-DDEDICATED=ON`) |
| `base.so`   | Base game shared library                 |
| `d3xp.so`   | Resurrection of Evil expansion library   |

### CMake Options

Key options passed via `-D<OPTION>=ON/OFF`:

| Option               | Default | Description                                      |
|----------------------|---------|--------------------------------------------------|
| `CORE`               | ON      | Build the core engine                            |
| `BASE`               | ON      | Build base game code (`base.so`)                 |
| `D3XP`               | ON      | Build expansion game code (`d3xp.so`)            |
| `DEDICATED`          | OFF     | Build dedicated server executable                |
| `SDL2`               | ON      | Use SDL2 (recommended over SDL1.2)               |
| `SDL3`               | OFF     | Use SDL3 instead of SDL2                         |
| `IMGUI`              | ON      | Dear ImGui settings menu (requires SDL2+ and C++11) |
| `HARDLINK_GAME`      | OFF     | Compile game code into executable (no DLLs)      |
| `ONATIVE`            | OFF     | Optimize for host CPU                            |
| `ASAN`               | OFF     | Enable Address Sanitizer (GCC/Clang)             |
| `UBSAN`              | OFF     | Enable Undefined Behavior Sanitizer (implies `HARDLINK_GAME`) |
| `TRACY`              | OFF     | Integrate Tracy profiler                         |
| `REPRODUCIBLE_BUILD` | OFF     | Deterministic `__DATE__`/`__TIME__` values       |

List all options interactively: `cmake -LH /path/to/neo/`

## Verifying the Build

A successful build produces zero errors and generates all four artifacts:

```sh
ls /tmp/dhewm3-build/dhewm3 /tmp/dhewm3-build/dhewm3ded \
   /tmp/dhewm3-build/base.so /tmp/dhewm3-build/d3xp.so
```

## CI

GitHub Actions workflows live in `.github/workflows/`:

- `linux.yml` — Ubuntu 22.04 x86_64, Ninja, `-DDEDICATED=ON`
- `macos.yml` — macOS build
- `win_msvc.yml` — Windows MSVC build

The Linux CI mirrors the manual build steps above: install the same apt
packages, run cmake with Ninja, then run ninja.

## Running

dhewm3 requires the original Doom 3 game data (patched to v1.3.1). Point the
engine at the directory containing `base/pak000.pk4` through `pak008.pk4`:

```sh
./dhewm3 +set fs_basepath /path/to/doom3/
```

## Key Files for Development

| Path                            | Purpose                                 |
|---------------------------------|-----------------------------------------|
| `neo/CMakeLists.txt`            | Build system — start here for build changes |
| `neo/framework/`                | Engine core (cvars, commands, filesystem) |
| `neo/renderer/`                 | Renderer — shaders, models, materials   |
| `neo/sound/`                    | Audio via OpenAL                        |
| `neo/game/Game_local.h`         | Base game main header                   |
| `neo/d3xp/Game_local.h`        | Expansion game main header              |
| `neo/sys/linux/main.cpp`        | Linux entry point                       |
| `neo/sys/posix/posix_main.cpp`  | POSIX shared code                       |
| `neo/idlib/`                    | Core math/container/string library      |
