# Real-Time Procedural Volumetric Cloud Renderer (D3D11 + HLSL)
C++17 + Direct3D 11 sample that ray-marches procedural clouds using Perlin + Worley noise (multi-scale),
single-scattering with Henyey–Greenstein phase (anisotropic), temporal reprojection/accumulation,
and a basic distance-based LOD for step size. Weather parameters drive coverage, density, wind and height.

> Intentionally a touch janky (but solid). No external assets required.

## Requirements
- Windows 10/11
- Visual Studio 2019/2022 with Desktop C++
- CMake 3.20+
- Windows SDK (DirectX 11, D3DCompiler)

## Build
```bash
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
# .\Release\VolumetricClouds.exe
```

## Controls
- **W/A/S/D**: move
- **Q/E**: move down/up
- **Arrow keys**: wind X/Z
- **Z/X**: cloud coverage ±
- **C/V**: density ±
- **O/P**: sun azimuth ±
- **K/L**: sun elevation ±
- **T**: toggle temporal accumulation
- **F**: toggle jitter (TAA-ish)
- **R**: reset weather
- **Esc**: exit

## Notes
- Temporal reprojection uses previous VP matrix + velocity to sample history buffer with clamped neighborhood.
- Step size increases with distance to reduce cost (kinda crude, works).
- Shaders compile at runtime via D3DCompile from `shaders/Clouds.hlsl`.
- If you see banding, enable jitter (`F`).
