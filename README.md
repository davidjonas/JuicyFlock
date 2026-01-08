# JuicyFlock

**JuicyFlock** is an open-source JUCE + OpenGL example of a **compute-shader optimized boids (flocking) simulation**. It uses GPU compute to update particle motion and renders the flock as sprites (points, squares, lines or "cubes"), designed to scale realtime to large particle counts (for a simulation of this kind).  

The flocking rules are based on Craig Reynolds’ boids model (separation, alignment, cohesion) (https://www.red3d.com/cwr/boids/).

## Why?

This was built because after some time away from JUCE I wanted to test it out. I've made it in a few hours as an exercise to quickly build something useful, documented and open-sourced that has (at least) a high educational value instead of throwing away all the code every time I build something just to test a framework. What makes this possible (as opposed to the past 15 years of throwing code away) is that now I can build these things much faster with AI and use the extra time to clean it up and get an AI agent to document and explain the concepts behind the project and the code.

So, for a deep dive into how the code and shaders are structured (SSBOs, ping-pong buffers, grid neighbor search, uniforms, hot reload), read [`docs/TECHNICAL.md`](docs/TECHNICAL.md).

## Demo video

[![JuicyFlock demo video thumbnail](https://i.ytimg.com/vi/jg4g5PGKCzM/maxresdefault.jpg)](https://www.youtube.com/watch?v=jg4g5PGKCzM)

- Watch on YouTube: [`https://www.youtube.com/watch?v=jg4g5PGKCzM`](https://www.youtube.com/watch?v=jg4g5PGKCzM)
- Local file (stored via Git LFS): [`docs/JuicyFlockRecording.mp4`](docs/JuicyFlockRecording.mp4)

## Features

- **OpenGL 4.3 compute shader simulation**
  - SSBO ping-pong for particle state
  - Spatial hashing via a **3D grid + linked-list cell heads** (avoids naive O(n²))
- **Real-time controls** (collapsible overlay UI)
  - particle count (1…100000)
  - neighbor/separation radii, rule weights, speed limits, max accel
  - bounds mode (soft bounds or wrap), point size, alpha
- **Mouse camera**
  - left-drag orbit, right-drag pan, mouse wheel zoom
- **Shader hot reload**
  - changes in `Shaders/` are recompiled while the app is running

## Requirements

- Windows 10/11 + a GPU/driver that supports **OpenGL 4.3+** (compute shaders).
- **Git** (to clone the repo + JUCE submodule): [`git-scm.com/download/win`](https://git-scm.com/download/win)
- **CMake 3.22+**: [`cmake.org/download`](https://cmake.org/download/)
- **Visual Studio 2022** (or Build Tools) with **Desktop development with C++**: [`visualstudio.microsoft.com/downloads`](https://visualstudio.microsoft.com/downloads/)
  - Make sure you install the MSVC toolchain (v143) and a Windows 10/11 SDK.

## Getting the source

JUCE is included as a **git submodule** at `JUCE/`.

Windows note: JUCE contains some long paths; if you hit path-length errors during clone/update, enable long paths in git:

```powershell
git config --global core.longpaths true
```

Clone (recommended):

```powershell
git clone --recurse-submodules <YOUR_REPO_URL>
```

If you already cloned without submodules:

```powershell
git submodule update --init --recursive
```

## Build (Windows / PowerShell)

From the repo root (configure/generate):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

Build Release:

```powershell
cmake --build build --config Release
```

The executable will be placed under the build artefacts directory (typically `build/JuicyFlock_artefacts/Release/`). After building, the `Shaders/` folder is copied next to the executable automatically.

## Run

Run the produced executable from the build artefacts folder (or from Visual Studio). The app will look for shaders in a `Shaders/` directory next to the executable (this is why the post-build copy step matters).

Example (path may differ slightly depending on generator/config):

```powershell
.\build\JuicyFlock_artefacts\Release\JuicyFlock.exe
```

## Controls

- **Left drag**: orbit
- **Right drag**: pan
- **Mouse wheel**: zoom
- **UI panel (top-left)**: toggle collapse and tweak simulation parameters

## Architecture

### Data (GPU)

Particles are stored in an SSBO array:

- `Particle { vec4 pos; vec4 vel; vec4 color; }`

The simulation uses **ping-pong buffers**:

- `ParticlesA` (read)
- `ParticlesB` (write)

For neighbor queries, the code builds a 3D grid each frame using:

- `CellHeads` SSBO: `head[cell]` indices (initialised to `-1`)
- `NextIndex` SSBO: `next[i]` linked list pointers for particles in each cell

### Compute passes (per frame)

1. **Clear grid** (`Shaders/boids_clear.comp`): set `CellHeads` to `-1`
2. **Build grid** (`Shaders/boids_build.comp`): insert each particle into a grid cell list
3. **Boids step** (`Shaders/boids_step.comp`): scan neighbor cells (27) and apply:
   - **Separation**: avoid crowding
   - **Alignment**: match heading
   - **Cohesion**: move toward neighbors

### Rendering

Rendering uses `glDrawArrays(GL_POINTS, ...)` and the vertex shader reads particles by `gl_VertexID` directly from the SSBO (`Shaders/particles.vert` / `Shaders/particles.frag`).

## Coloring

Color is computed **per-particle on the GPU** during the boids compute step and stored in the particle SSBO:

- `Shaders/boids_step.comp` writes `Particle.color = vec4(rgb, a)` for each particle.
- The render pass reads this color from the SSBO in `Shaders/particles.vert` and passes it through to `Shaders/particles.frag`.
- Final on-screen alpha is `Particle.color.a * u_alphaMul` (the UI “Alpha” slider).

### Color modes

The UI “Color mode” selector controls how `boids_step.comp` generates the hue:

- **Solid**: fixed hue (good for debugging motion)
- **Heading**: hue derived from velocity direction (yaw)
- **Speed**: hue derived from current speed between min/max
- **Density**: hue derived from local neighbour count (bounded and capped)

The sliders **Hue offset**, **Hue range**, **Saturation**, **Brightness**, and **Density curve** feed uniforms into `boids_step.comp` and are applied via an HSV→RGB mapping in the compute shader.

To add your own coloring (e.g. curvature, vorticity, signed speed, custom palettes), edit the “Coloring” section in `Shaders/boids_step.comp` where `colorT` and `hsv2rgb()` are used.

## Project layout

- `Source/` – JUCE app code
- `Shaders/` – compute + render shaders (hot-reloaded, copied next to the executable post-build)

## License

This project is licensed under **AGPL-3.0-or-later** (see `LICENSE.md`).

Note: JUCE itself is licensed separately (see `JUCE/LICENSE.md`). If you distribute a modified version of JUCE, you must comply with JUCE’s license terms.


