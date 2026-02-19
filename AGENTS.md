# Building dhewm3

## Prerequisites

Install the required development libraries:

```bash
sudo apt-get install -y libopenal-dev libsdl2-dev libcurl4-openssl-dev
```

- **libopenal-dev** - required (audio)
- **libsdl2-dev** - required (windowing, input, platform abstraction)
- **libcurl4-openssl-dev** - optional (HTTP downloads)

## Build

```bash
mkdir -p build
cd build
cmake ../neo
make -j$(nproc)
```

## Build Output

The build produces three artifacts in the `build/` directory:

- **dhewm3** - main executable
- **base.so** - base game shared library
- **d3xp.so** - Resurrection of Evil expansion game shared library
