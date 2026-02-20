# Renderer API Contract

This document describes the strict API contract between the engine/framework
and the renderer in dhewm3, and the internal layering within the renderer
that enables multiple GPU backends (OpenGL, Vulkan).

## Contract version

The public renderer contract is versioned in:

- `neo/renderer/RenderSystem.h`
  - `DHEWM_RENDER_SYSTEM_API_VERSION`
  - `idRenderSystem::GetApiVersion()`

The engine validates this contract during startup in:

- `neo/framework/Common.cpp` (`idCommonLocal::InitGame()`)

If the renderer reports a different API version than the engine expects,
startup fails with a fatal error.

## Engine-facing API surface

Engine/framework code interacts with the renderer through public interfaces:

- `idRenderSystem` (`neo/renderer/RenderSystem.h`) — primary interface
- `idRenderImGuiBackend` (`neo/renderer/RenderImGui.h`) — ImGui rendering
- `renderBackendInfo_t` / `renderBackendState_t` — backend info queries
- `sys/sys_glimp.h` — platform input/window functions (GRAB_*, GLimp_GrabInput)

Engine/framework code **must not** include renderer-internal headers
(`tr_local.h`, `qgl.h`, `Image.h`) or call OpenGL/Vulkan functions directly.

## Renderer internal architecture

### Layer 1: Frontend (API-neutral)

The frontend handles scene management, visibility determination, and command
generation. It produces API-neutral data structures consumed by the backend:

- **`viewDef_t`** — complete per-frame view description
- **`drawSurf_t`** — sorted visible surface for rendering
- **`viewLight_t`** — per-frame light with interaction surface chains
- **`drawInteraction_t`** — decomposed light/surface interaction parameters
- **Command queue** — `RC_DRAW_VIEW`, `RC_SET_BUFFER`, `RC_SWAP_BUFFERS`, `RC_COPY_RENDER`

Key frontend files:
- `tr_main.cpp` — frame setup, view processing
- `tr_light.cpp`, `tr_lightrun.cpp` — light/shadow processing
- `RenderWorld_*.cpp` — world management
- `RenderSystem.cpp` — 2D drawing, frame bracketing, render-to-texture

### Layer 2: Backend interface

The backend interface defines how the frontend communicates with GPU-specific
code. The GLS_* state constants in `tr_local.h` provide an API-neutral
abstraction for blend modes, depth functions, and alpha testing.

Key abstractions:
- **`idRenderBackendPlatform`** (`RenderBackendPlatform.h`) — window/context
  lifecycle, swap, gamma, vsync
- **`idRenderBackendDraw`** (`RenderBackendDraw.h`) — the draw backend
  interface that each GPU API implements (draw views, fill depth, draw
  interactions, shader passes, fog, stencil shadows, debug tools, 2D drawing)
- **`idRenderImGuiBackend`** (`RenderImGui.h`) — ImGui rendering
- **`idRenderGpuCommandContext`** (`RenderBackendGPU.h`) — basic GPU state

### Layer 3: GPU backends

Each backend implements the Layer 2 interfaces for a specific GPU API.

#### OpenGL backend (current, complete)

Files: `draw_arb2.cpp`, `draw_common.cpp`, `tr_backend.cpp`, `tr_render.cpp`,
`RenderBackendPlatform.cpp`, `RenderImGui.cpp`, `VertexCache.cpp`,
`Image_load.cpp`

Uses ARB vertex/fragment programs (ARB assembly), OpenGL fixed-function
state, and `qgl*` function pointers loaded through SDL.

#### Vulkan backend (planned)

Will implement the same Layer 2 interfaces using:
- VkInstance / VkDevice / VkSurface / VkSwapchain (platform layer)
- VkBuffer / VkDeviceMemory (vertex/index/uniform buffers)
- VkImage / VkImageView / VkSampler (textures)
- VkPipeline / VkPipelineLayout (shader pipelines with SPIR-V)
- VkRenderPass / VkFramebuffer (render passes)
- VkCommandBuffer (draw command recording)

### Layer 4: Platform (SDL)

The platform layer creates windows and surfaces via SDL. For OpenGL this
creates an SDL_GL context; for Vulkan it creates an SDL_Vulkan surface.

Files: `sys/glimp.cpp`, `sys/sys_glimp.h`

## GL type isolation

GL-specific types (`GLuint`, `GLenum`, `GLExtension_t`) must remain within
the renderer directory. The frontend command queue, image handles, and buffer
handles should use API-neutral types (int, unsigned int, void*) so the
frontend remains backend-agnostic.

Current known GL type leakage that must be addressed for Vulkan:
- `glconfig_t` fields used by frontend code (should query through methods)
- `GLuint texnum` in `idImage` (should become opaque handle)
- `GLuint vbo` in `vertCache_t` (should become opaque handle)
- `GLenum buffer` in `setBufferCommand_t` (should use int)
- `GLExtension_t` in `idRenderBackendPlatform` (should use `void*` or
  equivalent)
