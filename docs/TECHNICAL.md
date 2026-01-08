# TECHNICAL.md — JuicyFlock technical architecture

This document explains the implementation details of the JUCE + OpenGL boids simulation: GPU data layout, compute pipeline, grid neighbor search, and shader-based coloring.

## Core concepts and definitions

### Compute shader

A GPU program that runs many threads in parallel and can read/write buffers (SSBOs). The compute shader updates all particles each frame.

### SSBO (Shader Storage Buffer Object)

A GPU buffer that shaders can treat like an array of structs and **read/write**. Used here for:

- particle state (`pos/vel/color`)
- grid acceleration structure (`head[]` and `next[]`)

### `std430`

The memory layout rule used for SSBO structs/arrays. It makes CPU↔GPU struct layouts predictable *as long as the fields match* (see Particle layout below).

### Binding index (buffer “slot”)

In shaders: `layout(std430, binding = N) buffer ...`.

In C++: `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, N, bufferHandle)`.

If `N` doesn’t match, the shader reads/writes the wrong buffer.

### Ping-pong buffers

Two buffers A/B used to avoid “read while writing” hazards:

- read previous state from A
- write next state into B
- swap A/B

### Workgroup / dispatch size

Compute shaders run in groups. With `local_size_x = 256`, one workgroup runs 256 threads.

To launch `count` threads: `groups = (count + 255) / 256`, then `glDispatchCompute(groups, 1, 1)`.

### Memory barrier

`glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)` ensures SSBO writes from one dispatch are visible to later dispatches (and to rendering). Missing it causes intermittent “stale data” bugs.

### Atomics (why the grid build is correct)

When many GPU threads insert into the same cell list, they must coordinate. `atomicExchange(head[cell], i)` is a lock-free “push to head” for a linked list.

### `gl_VertexID` (drawing without a vertex buffer)

When you call `glDrawArrays(GL_POINTS, 0, N)`, the GPU runs the vertex shader N times. `gl_VertexID` is the current index \([0..N-1]\). This project uses it to index directly into the particle SSBO: `p[gl_VertexID]`.

### Uniforms

Uniforms are small per-draw/per-dispatch values (scalars/matrices) set from C++ (e.g. radius, weight, `dt`, `viewProj`). They are not per-particle; they are shared by all shader invocations for that dispatch/draw.

### Uniform grid (why neighbor search is fast)

Particles are binned into a 3D grid each frame. Each particle only scans the 27 surrounding cells instead of scanning all particles.

## File map

- **App entry**
  - `Source/Main.cpp`: JUCE application + window. Creates `MainComponent`.
- **All OpenGL + simulation**
  - `Source/MainComponent.h`: parameters, GL object handles, UI panel.
  - `Source/MainComponent.cpp`: shader compile/hot reload, SSBO creation, per-frame compute + draw.
- **Shaders (GPU behavior)**
  - `Shaders/boids_clear.comp`: set all grid heads to `-1`.
  - `Shaders/boids_build.comp`: insert each particle index into its cell’s linked list.
  - `Shaders/boids_step.comp`: neighbor query + boids rules + integration + write color.
  - `Shaders/particles.vert`: fetch particle by `gl_VertexID`, compute clip-space position, pass color.
  - `Shaders/particles.frag`: disc shaping + alpha multiply.
- **Build/runtime**
  - `CMakeLists.txt`: copies `Shaders/` next to the executable (so runtime shader loading/hot reload works).

## Runtime model (threads and “who calls what”)

JUCE owns two relevant threads:

- **Message thread**: normal UI events (sliders, buttons).
- **OpenGL thread**: JUCE calls `MainComponent::initialise()`, `MainComponent::render()`, `MainComponent::shutdown()` on the OpenGL context thread.

Rule: **all OpenGL calls must happen on the OpenGL thread**.

How this code enforces that:

- UI changes are debounced in the control panel, then forwarded using `openGLContext.executeOnGLThread(...)` so parameter updates + buffer rebuilds happen safely on the GL thread.
- Shader hot reload is triggered by a timer and also uses `executeOnGLThread(...)` to recompile programs on the GL thread.

## Per-frame execution (exact order in code)

Called from `MainComponent::render()` on the OpenGL thread:

1. **Compute `dt`**
   - measured wall time, then clamped to `0..0.05` for stability.
2. **Dispatch compute passes** (`dispatchComputePasses(dt)`)
   - **Clear grid** (`boids_clear.comp`)
     - bind: `CellHeads` → binding **2**
     - set uniform: `u_cellCount`
     - dispatch: groups = `(cellCount + 255) / 256`
     - barrier: `GL_SHADER_STORAGE_BARRIER_BIT`
   - **Build grid** (`boids_build.comp`)
     - bind: particles (read) → **0**, `CellHeads` → **2**, `NextIndex` → **3**
     - set uniforms: `u_particleCount`, `u_gridDims`, `u_worldMin`, `u_cellSize`
     - dispatch: groups = `(particleCount + 255) / 256`
     - barrier
   - **Boids step** (`boids_step.comp`)
     - bind: particles in → **0**, particles out → **1**, `CellHeads` → **2**, `NextIndex` → **3**
     - set uniforms: all sim + color parameters (`u_dt`, radii, weights, speed/accel limits, bounds mode, HSV params)
     - dispatch: groups = `(particleCount + 255) / 256`
     - barrier
   - **Ping-pong swap**
     - swap the two particle SSBO handles so “latest” is always `particlesSSBO[0]`.
3. **Draw points**
   - bind particles SSBO (latest) → binding **0**
   - set uniforms: `u_viewProj`, `u_pointSize`, `u_shape`, `u_alphaMul`
   - draw: `glDrawArrays(GL_POINTS, 0, particleCount)`
   - note: blending is explicitly enabled before draw because JUCE overlay painting may change GL state.

## Core GPU data structures

### Particle layout (CPU and GPU must match)

**CPU struct** (in `MainComponent.cpp`):

- `ParticleCPU { float pos[4]; float vel[4]; float color[4]; }`

**GPU struct** (in shaders):

- `Particle { vec4 pos; vec4 vel; vec4 color; }`

This matches 1:1: 3 × `vec4` = 12 floats = 48 bytes per particle.

Notes:

- `pos.w` is set to `1.0`, `vel.w` to `0.0` (padding / convenience).
- `color` is written each frame by the compute shader and then consumed by the render shaders.

### std430 layout and binding points

All buffers use:

- `layout(std430, binding = N) buffer ...`

Meaning:

- **std430**: packing rules for structs/arrays in the buffer (predictable layout).
- **binding = N**: a numeric “slot” used to connect a C++ buffer object to a shader buffer declaration.

In C++ the connection is done with:

- `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingIndex, bufferHandle)`

This code uses these bindings consistently:

- **0**: particles input (`ParticlesIn` / `Particles` in vertex shader)
- **1**: particles output (`ParticlesOut`)
- **2**: grid cell heads (`CellHeads`)
- **3**: per-particle next pointers (`NextIndex`)

## Ping-pong buffers (why and how)

### Why ping-pong?

If every particle reads and writes the same buffer in one compute dispatch, two bad things happen:

- threads can overwrite data that other threads still need to read (race conditions),
- ordering becomes undefined without heavy synchronization.

So each frame:

- read from buffer **A**
- write results into buffer **B**
- swap A/B for the next frame

### How it’s implemented here

In C++ there are **two particle SSBOs**:

- `particlesSSBO[0]` = current input
- `particlesSSBO[1]` = output

Per frame in `dispatchComputePasses()`:

- bind `particlesSSBO[0]` to binding 0
- bind `particlesSSBO[1]` to binding 1
- dispatch `boids_step.comp`
- then `std::swap(particlesSSBO[0], particlesSSBO[1])`

The render pass always binds `particlesSSBO[0]` (the “latest” buffer after swap).

## Spatial grid neighbor search (the critical performance feature)

Naive boids: for each particle, scan all other particles → \(O(n^2)\).

This project: build a uniform 3D grid each frame and only check nearby cells → roughly \(O(n \cdot k)\) where \(k\) is bounded by local density.

### Grid definition

The world is an axis-aligned box:

- `worldMin = (-5,-5,-5)`
- `worldMax = ( 5, 5, 5)`

Cell size is derived from neighbor radius:

- `cellSize = max(0.05, neighborRadius)`

Grid dimensions:

- `gridDims = ceil((worldMax - worldMin) / cellSize)` per axis
- `cellCount = gridDims.x * gridDims.y * gridDims.z`

Rebuilding: whenever particle count changes or neighbor radius changes, buffers are rebuilt (because `cellSize`, `gridDims`, and `cellCount` depend on neighbor radius).

### Grid storage: “cell heads + linked list”

Two SSBOs implement a per-cell linked list of particle indices:

1. **CellHeads SSBO** (`int head[cellCount]`)
   - `head[c]` stores the index of the first particle in cell `c`, or `-1` if empty.
2. **NextIndex SSBO** (`int next[particleCount]`)
   - `next[i]` stores the index of the next particle in the *same cell list*, or `-1`.

This forms a singly-linked list per grid cell:

- `i = head[cell]`
- while `i != -1`: visit particle `i`, then `i = next[i]`

### Pass 1: clear cell heads (`boids_clear.comp`)

Goal: set every `head[c] = -1`.

- One invocation per cell.
- Workgroup size is 256 threads (`local_size_x=256`).
- C++ dispatch groups: `(cellCount + 255) / 256`.

### Pass 2: build cell linked lists (`boids_build.comp`)

Goal: insert every particle index into its cell list.

Steps per particle `i`:

1. Load position `pos`.
2. Compute integer cell coordinates:
   - `cell = floor((pos - worldMin) / cellSize)`
   - clamp to `[0, gridDims-1]` to be safe.
3. Flatten cell `(x,y,z)` to 1D cell index:
   - `cellIndex = x + gridDims.x * (y + gridDims.y * z)`
4. Atomically push onto the cell’s linked list:
   - `prev = atomicExchange(head[cellIndex], i)`
   - `next[i] = prev`

Key concept: **atomicExchange**

- multiple threads might insert into the same cell at the same time,
- `atomicExchange` makes “push to head” safe without locks:
  - it replaces `head[cell]` with `i`,
  - returns the previous head value so we can link `next[i]` to it.

Result: each cell contains an unordered list of particle indices inserted during this frame.

### Neighbor query in the boids step (`boids_step.comp`)

Each particle only checks particles from the **27 cells** surrounding its own cell:

- `(dx,dy,dz)` in `[-1,0,1]³`
- skip out-of-bounds neighbor cells

Inside each neighbor cell:

- iterate `for (j = head[cell]; j != -1; j = next[j])`
- compute `d = pos[j] - pos[i]`, `dist2 = dot(d,d)`
- keep only neighbors with:
  - `dist2 <= neighborRadius^2`
  - not `j == i`

There is an explicit cap:

- `kMaxNeighbours = 64`

This prevents worst-case slowdown if a lot of particles land in the same cell(s).

## Boids behavior (exact rules used here)

All of this happens per particle in `boids_step.comp`.

Inputs:

- current particle `pos`, `vel`
- nearby neighbors from the grid
- UI-controlled parameters (weights, radii, speed/accel limits, bounds mode)

Outputs:

- updated `pos`, `vel`
- updated `color` (stored with the particle)

### 1) Neighbor accumulation

For neighbors within `neighborRadius`:

- **cohesion accumulator**: sum of neighbor positions
- **alignment accumulator**: sum of neighbor velocities
- `neighbourCount++`

For neighbors also within `separationRadius`:

- **separation accumulator**: repulsive push
  - `separation -= d / (dist2 + eps)`
  - inverse-square-ish: close neighbors push much harder
- `separationCount++`

### 2) Convert accumulators into steering vectors

If there are neighbors:

- `avgPos = cohesion / neighbourCount`
- `avgVel = alignment / neighbourCount`
- **cohesion steer**: `steerCoh = (avgPos - pos)` (move toward average position)
- **alignment steer**: `steerAli = (avgVel - vel)` (match neighbor velocity)

If there are separation neighbors:

- `steerSep = separation / separationCount`

### 3) Weighted sum → acceleration

Acceleration starts as:

- `accel = wSep*steerSep + wAli*steerAli + wCoh*steerCoh`

Then adds a global center pull:

- `center = 0.5*(worldMin+worldMax)`
- `accel += (center - pos) * centerAttraction`

### 4) Bounds behavior

Two modes, controlled by `u_wrapBounds`:

- **Soft bounds (wrap disabled)**:
  - before integrating velocity, the shader adds steering acceleration when a particle is within `boundaryMargin` of a face.
  - this is proportional to “how deep into the margin” the particle is, scaled by `boundaryStrength`.
  - effect: particles “turn back” instead of teleporting.
- **Wrap bounds (wrap enabled)**:
  - no soft steering is added.
  - after position update, if `pos < worldMin` then `pos += (worldMax-worldMin)` (and similarly for `pos > worldMax`).
  - effect: particles teleport to the opposite side (toroidal space).

Even in soft-bounds mode, the shader still clamps position after integration to guarantee `pos` stays inside the box.

### 5) Acceleration clamp

To prevent jitter/explosions:

- `if length(accel) > maxAccel: accel = normalize(accel) * maxAccel`

### 6) Integrate velocity and position

Time integration is simple explicit Euler:

- `vel += accel * dt`
- `pos += vel * dt`

Speed is clamped into `[minSpeed, maxSpeed]`:

- if `speed` is almost zero, it forces a fallback direction (to avoid NaNs / stuck particles),
- otherwise normalize `vel` and scale by clamped speed.

## Coloring (done in the compute shader, stored per particle)

Color is computed in `boids_step.comp` and written into `Particle.color` in the SSBO.

### HSV mapping

The compute shader uses an HSV→RGB helper:

- `hsv2rgb(vec3(h, s, v))`

It then computes a hue:

- `hue = fract(hueOffset + hueRange * colorT)`

And produces RGB:

- `rgb = hsv2rgb(vec3(hue, saturation, value))`

### Color modes (how `colorT` is chosen)

The UI drives `u_colorMode`:

- **0: Solid**
  - `colorT = 0` → hue is constant (only `hueOffset` matters).
- **1: Heading**
  - Uses velocity direction projected into XZ (“yaw”):
    - `yaw = atan(heading.z, heading.x)` in `[-pi, +pi]`
    - `colorT = fract(0.5 + yaw / (2*pi))`
  - Effect: heading direction maps to hue around a color wheel.
- **2: Speed**
  - `t = clamp((|vel| - minSpeed) / (maxSpeed - minSpeed), 0..1)`
  - `colorT = t`
  - Effect: slow→fast maps along hue range.
- **3: Density**
  - Uses neighbor count from the grid query:
    - `d = clamp(neighbourCount / kMaxNeighbours, 0..1)`
    - `colorT = pow(d, densityCurve)`
  - Effect: denser areas shift hue (with curve shaping).

### Alpha behavior

Alpha stored in the particle is speed-derived:

- `alpha = 0.35 + 0.65 * t` where `t` is the normalized speed described above.

Final on-screen alpha is:

- `finalAlpha = particleAlpha * u_alphaMul` (slider “Alpha”).

## Rendering path (no vertex buffer; `gl_VertexID` indexes particles)

Render happens in `MainComponent::render()` after compute.

### Vertex shader (`particles.vert`)

- Binds particles SSBO at binding **0**.
- For each point, uses:
  - `Particle particle = p[gl_VertexID];`
- Computes:
  - `gl_Position = u_viewProj * vec4(particle.pos.xyz, 1)`
  - `gl_PointSize = u_pointSize`
- Passes `particle.color` to fragment shader.

No VBO attributes are used. A VAO is still created because OpenGL core profile requires a VAO bound for drawing.

### Fragment shader (`particles.frag`)

- Optionally shapes points as a disc:
  - uses `gl_PointCoord` to discard pixels outside radius 1.
- Outputs:
  - `FragColor = vec4(vColor.rgb, vColor.a * u_alphaMul)`

### Blending

The renderer enables alpha blending each frame:

- `glEnable(GL_BLEND)`
- `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`

Depth test is disabled (points are drawn in submission order).

## Compute dispatch details (thread group math + barriers)
All compute shaders use `local_size_x = 256`, so group counts are:

- particles: `(particleCount + 255) / 256`
- cells: `(cellCount + 255) / 256`

After each compute pass the code calls `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)` so:

- clear → build sees cleared `head[]`
- build → step sees completed `head[]` and `next[]`
- step → draw sees updated particle data

## C++ side: buffer creation and lifetime

### Creating buffers (`rebuildBuffersOnGLThread`)

Called on the GL thread when:

- the app starts (after shaders compile), and
- particle count changes, or
- neighbor radius changes (because it changes cell size and grid dimensions).

It creates and fills:

- **2 particle SSBOs**
  - buffer 0: initialized with random particles
  - buffer 1: allocated (same size) but left empty (compute writes it)
- **CellHeads SSBO**
  - initialized to `-1` for all cells (also cleared every frame by compute)
- **NextIndex SSBO**
  - allocated to `particleCount` ints (written every frame in build pass)

Initial particle generation:

- random position uniformly in `[worldMin, worldMax]`
- random direction (normalized) and random speed in `[minSpeed, maxSpeed]`
- initial color is a simple CPU-side debug mapping (compute overwrites it once running).

### Deletion

On shutdown or rebuild:

- shader programs deleted with `glDeleteProgram`
- SSBOs deleted with `glDeleteBuffers`

## Shader compilation + hot reload

### Where shader files are loaded from

The app looks for a runtime `Shaders/` directory relative to the executable (and a few parent directories), with a final fallback to the current working directory.

Build system copies `Shaders/` next to the executable on each build (`CMakeLists.txt` post-build step). That is why runtime loading works.

### Compilation flow

On OpenGL initialise:

- checks OpenGL version is 4.3+ (compute required),
- compiles and links:
  - `boids_clear.comp`
  - `boids_build.comp`
  - `boids_step.comp`
  - `particles.vert` + `particles.frag`

If compilation/link fails:

- an error string is stored and drawn by `MainComponent::paint()` as an overlay (so you see shader errors inside the app).

### Hot reload

A JUCE timer runs every ~500ms:

- compares file modification times for shader files,
- if any changed, it recompiles all programs on the GL thread,
- updates the stored modification times.

## UI parameter plumbing (CPU → uniforms)

The control panel emits a `Params` struct (particle count, radii, weights, speed limits, bounds mode, point size, alpha, color parameters).

Important details:

- UI changes are debounced (panel timer) to avoid spamming updates while dragging sliders.
- Parameter application happens on the GL thread via `executeOnGLThread`.
- Particle count changes or neighbor radius changes trigger a buffer rebuild because they change:
  - SSBO sizes (particle count),
  - grid cell size/dims (neighbor radius).
- All other values are passed as uniforms each frame during the boids step and draw.

## “If you reimplement this” checklist (most common pitfalls)

- **Match struct layout exactly**
  - `Particle` must be 3×`vec4` in both CPU and GPU view; keep `std430` and alignment in mind.
- **Bind SSBOs to the correct binding indices**
  - 0/1/2/3 must match shader `binding = N` declarations.
- **Use ping-pong for particles**
  - read-only input buffer, write-only output buffer, then swap handles.
- **Build the grid every frame**
  - clear heads → build linked lists → query 27 neighbor cells.
- **Use atomics for grid insertion**
  - without `atomicExchange`, you will corrupt cell lists under parallel writes.
- **Insert memory barriers**
  - missing `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)` causes intermittent “stale data” bugs.
- **Clamp dt**
  - large `dt` destabilizes Euler integration quickly.
- **Cap neighbor count**
  - without a cap, dense cells can stall the GPU (pathological worst-case).

