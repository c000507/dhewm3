# Renderer API Contract

This document describes the strict API contract between the engine/framework
and the renderer in dhewm3.

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

Engine/framework code should interact with the renderer through public
renderer interfaces, primarily:

- `idRenderSystem` (`neo/renderer/RenderSystem.h`)
- backend/runtime abstractions via renderSystem methods

Engine/framework code should not depend on renderer-internal headers or
OpenGL-specific runtime functions.

## Backend abstraction direction

Runtime/context operations are being routed behind backend contracts so
alternative backends can be integrated incrementally. Current contracts include:

- `RenderBackendPlatform` (`neo/renderer/RenderBackendPlatform.h`)
- `RenderBackendGPU` (`neo/renderer/RenderBackendGPU.h`)

Backend module stubs exist for future implementations (e.g. Vulkan/software/
raytrace/voxel), while OpenGL remains the active implementation today.
