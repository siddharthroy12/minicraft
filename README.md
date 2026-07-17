# Minicraft

A minimal Minecraft-style voxel sandbox built with C++ and [raylib](https://www.raylib.com/). Procedurally generated terrain, trees, block breaking/placing, and a pixel-art texture atlas generated at runtime (no external image assets).

## Requirements

- CMake 3.15+
- A C++17 compiler (Clang/AppleClang, GCC, or MSVC)
- Git (for the raylib submodule)
- Platform build tools:
  - **macOS**: Xcode Command Line Tools (`xcode-select --install`)
  - **Linux**: build-essential + X11/Wayland dev packages (raylib's own README lists these if you hit missing-library errors)
  - **Windows**: Visual Studio (Desktop C++ workload) or MinGW

## Setup

Clone the repo and pull in the raylib submodule:

```sh
git clone https://github.com/siddharthroy12/minicraft.git
cd minicraft
git submodule update --init --recursive
```

If you already have the repo cloned without the submodule initialized, just run the `git submodule update` line from inside it.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

(`-j` parallelizes the build; on macOS you can use `-j$(sysctl -n hw.ncpu)`, on Linux `-j$(nproc)`.)

The first build also compiles raylib itself from source, so it will take a little longer than subsequent builds.

## Run

```sh
./build/minicraft
```

On Windows the executable will be at `build\Debug\minicraft.exe` or `build\Release\minicraft.exe` depending on generator/config.

After editing source files, just re-run the build command — you don't need to redo the `cmake -S . -B build` configure step unless `CMakeLists.txt` changes.
