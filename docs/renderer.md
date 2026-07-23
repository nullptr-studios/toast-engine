# Vulkan Renderer

> Authors: Darío Rodríguez Martínez
>
> Created on: 5/7/2026
> Updated on: 5/7/2026

The Vulkan renderer is a threaded rendering backend that consumes frame snapshots built
from the current engine state on the main thread, then records/submits Vulkan command buffers
on a dedicated render thread

At a high level:

1. The main thread fills `VulkanRenderer::RenderFrame` (`frame_data` + mesh instance proxies)
2. `submitFrame()` pushes that frame index into a queue
3. The render thread consumes that snapshot, updates GPU frame resources, records dynamic
   rendering passes (`MeshPass`, etc.), submits to the graphics queue, and presents through
   the active output target

## Motivation

The renderer is split around two constraints:

- The engine/editor simulation code should not block on command buffer recording or queue
  submission
- The backend must support both on-screen rendering (`SDLOutputTarget` + swapchain) and
  off-screen editor rendering (`SharedTextureOutputTarget`) with the same pass pipeline

The renderer keeps two domains:

- **Main-thread domain**: builds frame snapshots (`RenderFrame`)
- **Render-thread domain**: owns Vulkan submission flow and synchronization


- `MeshNode` instances register/unregister themselves in the engine
- On frame build, the engine snapshots each visible mesh into
  `VulkanRenderer::MeshInstanceProxy` with:
  - `VulkanMesh*` handle
  - model matrix
- The render thread reads those copied proxies from `RenderFrame`, it does not traverse the
  scene graph or keep `MeshNode` pointers

This separates scene ownership from render execution and keeps render-thread inputs
deterministic per submitted frame

## Uses

### Creating the renderer

Create `VulkanCore`, create an `IOutputTarget` implementation, then create
`VulkanRenderer`, add passes, and start:

```cpp
auto output_target = std::make_unique<renderer::SDLOutputTarget>(...); // or SharedTextureOutputTarget
renderer = std::make_unique<renderer::VulkanRenderer>(*vulkan_core, std::move(output_target));
renderer->addRenderPass(std::make_unique<renderer::MeshPass>(...));
renderer->start();
```

### Submitting frames

Per engine tick:

```cpp
auto& sem = renderer->getFreeFramesSemaphore();
if (sem.try_acquire()) {
    auto& frame = renderer::beginFrameBuild();
    frame.mesh_instances.clear();
    // fill frame.frame_data + frame.mesh_instances
    renderer::submitFrame();
}
```

### Resource uploads

GPU uploads are queued via `VulkanRenderer::queueResourceUpload(...)` using
`PendingResourceUpload` jobs (`MeshUpload`, `TextureUpload`, etc.). Uploads are staged and
flushed from the render thread to the transfer queue

### Output targets

- **SDL/swapchain path**: uses acquire/present semaphores and transitions to
  `ePresentSrcKHR` in `recordFinalize`
- **Shared texture path (editor)**: no acquire/present semaphore dependency, transitions
  color attachment to transfer source, copies to host-visible staging, and publishes the
  latest completed frame for `toast_viewport_get_frame`

### Resizing

Resize requests are queued into `VulkanRenderer::applyResize(...)` and applied on the
render thread (`applyResizeInternal`)

## Performance

The current design minimizes stalls by:

- keeping frame build on the main thread lightweight (copying proxy data, not doing Vulkan API work)
- processing uploads asynchronously with batch fences
- reusing command pools and per-frame synchronization objects

## Expansion

Planned expansions:

- multi-pass frame graph (shadow pass, lighting/post passes) via additional `IRenderPass`
  implementations
- explicit culling and sorting stages before filling `RenderFrame`
- compute pass integration
- stronger resource lifetime tracking for hot-reload and streaming

## Shaders and materials

Shaders are assets loaded through the AssetManager. The `ShaderCache` compiles every
manifest shader at startup into `cache://shaders/` as `<uid>.spv` (SPIR-V) plus `<uid>.json`
(reflection), with `hash.json` as the staleness index. All SPIR-V stays in memory for the
lifetime of the program; a shader used by several materials shares a single cache entry.
Editing a `.slang`, `.tmat` or `.tmi` file on disk hot-reloads it, recompiles through the
cache and rebuilds the affected pipelines on the render thread

Materials (`.tmat`) declare a `shaders` array; every other key is a value for a shader
parameter discovered via reflection, and the trailing `[settings]` table carries
fixed-function state. Material instances (`.tmi`) reference a parent material and serialize
only the values that differ from it. Set 0 is engine-reserved frame data and a `model` push
constant member is engine-written; everything else is material data, edited in the GenericEditor
through a schema generated from the reflection json.

The renderer creates one `MaterialPass` per root material on the render thread; instances share
their parent's pass with their own descriptor sets. Meshes without a material fall back to
`core://material/default.tmat`. Every pass exposes a name and an enabled flag, toggled from the
editor viewport's eye popup (`render_events.proto`).

## Known issues
- Node3D transforms are not being propagated to children
- Only the first shader module of a material's `shaders` array is used for pipeline creation
