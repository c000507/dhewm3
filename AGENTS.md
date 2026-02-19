# AGENTS.md

## Project Overview

dhewm3 is a source port of the Doom 3 GPL source code. It uses CMake as its build system. The main CMakeLists.txt is located at `neo/CMakeLists.txt`.

## Build Instructions

### System Dependencies (Debian/Ubuntu)

```sh
sudo apt-get install -y libgl1-mesa-dev libsdl2-dev libopenal-dev libcurl4-openssl-dev ninja-build cmake
```

### Compiling

```sh
cmake -G Ninja -B /tmp/dhewm3-build -S neo
cmake --build /tmp/dhewm3-build
```

### Build Artifacts

A successful build produces the following in the build directory:

- `dhewm3` — main game executable
- `dhewm3ded` — dedicated server executable
- `base.so` — base game shared library
- `d3xp.so` — Resurrection of Evil expansion shared library
