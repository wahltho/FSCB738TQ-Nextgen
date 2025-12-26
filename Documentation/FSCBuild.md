# FSCB738TQ-Nextgen Build (macOS / Linux / Windows)

Cross-platform build notes for the FSCB738TQ-Nextgen plugin. This repo is a CMake project; X-Plane SDK headers are expected under `../SDKs/XPlane_SDK`.

## Artifacts & Layout
- Output names (set by CMake): `mac.xpl`, `lin.xpl`, `win.xpl`.
- Typical output locations: `build-<plat>/mac.xpl`, `build-<plat>/lin.xpl`, `build-<plat>/win.xpl`.
- X-Plane loads the platform-specific file from `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/64/`.

## Common Requirements
- X-Plane SDK path: `-DXPLANE_SDK_ROOT="../SDKs/XPlane_SDK"` (absolute paths are fine; in containers use a container path).
- CMake >= 3.15.
- Toolchains: Xcode (macOS), GCC/Clang + build-essential (Linux), Visual Studio 2022 with C++ (Windows).

## macOS (universal recommended)
```bash
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DXPLANE_SDK_ROOT="../SDKs/XPlane_SDK"
cmake --build build-mac --config Release

# Universal binary (recommended):
cmake -S . -B build-mac-universal -DCMAKE_BUILD_TYPE=Release -DXPLANE_SDK_ROOT="../SDKs/XPlane_SDK" -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"
cmake --build build-mac-universal --config Release
```
Result: `build-mac/mac.xpl` or `build-mac-universal/mac.xpl`.

## Linux (container recommended)
- Podman or Docker with an Ubuntu image.
- If the SDK lives next to the repo (`../SDKs/XPlane_SDK`), mount it into the container.
- On Apple Silicon, use `--platform=linux/amd64` (X-Plane Linux is x86_64).
```bash
podman machine start   # once (if using Podman)
podman run --rm -it --platform=linux/amd64 -v "$(pwd)":/work -v "$(pwd)/../SDKs":/SDKs -w /work ubuntu:22.04 bash -lc "\
  apt-get update && apt-get install -y build-essential cmake ninja-build && \
  cmake -S . -B build-lin -G Ninja -DCMAKE_BUILD_TYPE=Release -DXPLANE_SDK_ROOT=/SDKs/XPlane_SDK && \
  cmake --build build-lin"
```
Result: `build-lin/lin.xpl`.

## Windows (Visual Studio 2022)
- VS 2022 Desktop C++ Workload.
```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 -DXPLANE_SDK_ROOT=../SDKs/XPlane_SDK
cmake --build build-win --config Release
```
Result: `build-win/Release/win.xpl`.

## Windows (Cross-Compile via Container, optional)
- For CI or macOS/Linux hosts: use `mingw-w64` in an Ubuntu container.
- Important: use `x86_64-w64-mingw32-g++-posix` (avoids missing `std::thread`/`std::mutex`).
- Note: for MinGW builds, this repo links statically to avoid extra runtime DLLs (prevents X-Plane Error 126/127).
```bash
podman run --rm -it --platform=linux/amd64 -v "$(pwd)":/work -v "$(pwd)/../SDKs":/SDKs -w /work ubuntu:22.04 bash -lc "\
  apt-get update && apt-get install -y cmake ninja-build mingw-w64 && \
  cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix \
    -DXPLANE_SDK_ROOT=/SDKs/XPlane_SDK && \
  cmake --build build-win"
```
Result: `build-win/win.xpl` (Ninja) or `build-win/Release/win.xpl` (VS).

## Staging / Packaging
- Staging folder: `deploy/FSCB738TQ-Nextgen/64/`.
```bash
mkdir -p deploy/FSCB738TQ-Nextgen/64

# macOS (universal recommended):
cp -f build-mac-universal/mac.xpl deploy/FSCB738TQ-Nextgen/64/mac.xpl

# Linux:
cp -f build-lin/lin.xpl deploy/FSCB738TQ-Nextgen/64/lin.xpl

# Windows (Ninja/Container):
cp -f build-win/win.xpl deploy/FSCB738TQ-Nextgen/64/win.xpl
# Windows (Visual Studio Generator):
# cp -f build-win/Release/win.xpl deploy/FSCB738TQ-Nextgen/64/win.xpl
```

## Copy to X-Plane
- Copy the entire folder `deploy/FSCB738TQ-Nextgen` to `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen`.
- The plugin is then located at `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/64/<mac|lin|win>.xpl`.
