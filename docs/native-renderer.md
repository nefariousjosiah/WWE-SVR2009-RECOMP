# Native renderer

Goal: draw SvR 2008 through a modern graphics API directly, like Unleashed Recompiled and re:Blue,
instead of emulating the Xbox 360 GPU (Xenos) by translating its command stream draw by draw.

## Why

The current path (ReXGlue's Xenia-derived `xenos` plugin) rebuilds every draw from the GPU command
stream on one thread at ~2.2-2.4 us per draw. Entrance crowd shots issue ~5,000 draws per frame,
which sits at the edge of the 16.7 ms budget on a Ryzen 7 5800X3D and is over it on slower CPUs.
A native renderer intercepts the game's own Direct3D calls, so there is no command stream to
decode, no EDRAM emulation, and state can be cached per draw. It also makes MSAA, ultrawide and
texture replacement straightforward.

## Approach: adapt re:Blue's renderer

[re:Blue](https://github.com/zolaware/reblue) (Blue Dragon, BSD-3-Clause, by the ReXGlue author)
ships a complete native renderer on the same SDK:

- `src/gpu/hooks/*`: replaces ~60 functions of the statically linked XDK D3D library
  (`Direct3D_CreateDevice`, `Swap`, `Clear`, the draw calls, `Resolve`, tiling, resource
  create/lock/unlock/release, shader and state setters) with host implementations;
- [plume](https://github.com/zolaware/plume) as the rendering layer: Direct3D 12, Vulkan and Metal;
- [XenosRecomp](https://github.com/zolaware/reblue-XenosRecomp) to turn Xenos shader microcode
  into HLSL, compiled with DXC to DXIL / SPIR-V (Metal through plume);
- runs on Windows (D3D12 or Vulkan), Linux/Steam Deck (Vulkan) and macOS (Metal, Apple Silicon).

Everything game-specific in it (`bd*`, `hcg*`, `engine/`) is dropped; the XDK D3D parts are kept and
pointed at SvR's addresses. Files taken from re:Blue keep their BSD-3 headers, and re:Blue is
credited in the README.

## XDK version and function map (in progress, milestone 1)

- SvR 2008 links **D3D9 2.0.5632** (all XDK libraries 2.0.5632, from the XEX static-library header).
- Blue Dragon links a nearby build: `tools/native/map_d3d_functions.py` aligns re:Blue's named D3D
  functions with SvR's by exact function size and order. **296 of 333 align, 183 with identical
  size** (`config/native/d3d_functions_aligned.txt`).
- Size matches are only hints (Blue Dragon's XDK build differs; same-size functions are common).
  Every name in `config/native/d3d_names.toml` is confirmed by behaviour: what it writes, what it
  calls, and live calls from the census below.
- Confirmed so far (2026-10-06): `Present` `sub_8223AEF8`, `Swap` `sub_8223A960` (only `VdSwap`
  caller), `SynchronizeToPresentationInterval`, `Resolve` `sub_82249340`, `SetRenderTarget`
  `sub_822407E0`, `SetScissorRect` `sub_8223FD58`, and the draws, which the linker placed outside
  the main D3D range: `BeginVertices` `sub_825AAF28`, `EndVertices` `sub_825AB3C8`, `DrawVertices`
  `sub_825AB3E8`, `DrawIndexedVertices` `sub_825AB7D0`. All four flush the device's dirty state and
  write a predicated `DRAW_INDX` (0xC0012201), i.e. they are the XDK draw functions, not engine
  code writing PM4 itself. The whole `SetRenderState_*` / `SetSamplerState_*` table aligned and is
  confirmed by the startup routine `sub_82240ED4` setting each to its default.
- `sub_82238910` is a "block until resource not busy" helper (reads resource->Fence at +0x08), not
  `D3DDevice_BlockOnFence`; it calls the fence wait `sub_822386B8`, which busy-waits through
  `sub_82239E68` (about 454,000 polls per frame on the title screen).
- The device pointer: kernel import slot `0x82000898` (VdGlobalDevice); the engine keeps its own
  copy at `*(0x82E6E49C) + 128`. SvR's device layout differs from Blue Dragon's in places (scissor
  rect at +0x317C here vs +0x3070), so offsets are re-derived from SvR code.

## Tools (this folder)

- `tools/native/map_d3d_functions.py`: size alignment against re:Blue's names (hints).
- `tools/native/recomp_index.py`: index of the generated code; `dis`, `callers`, `imports`, `grep`.
- `tools/native/gen_census.py` + `src/dev/d3d_census_runtime.h`: CMake option `SVR_D3D_CENSUS`;
  logs the most-called D3D functions per frame and, every 30 s, every function called with sample
  arguments and its caller.
- `SVR_DUMP_IMAGE=<file>` (`src/image_dump.h`): writes the loaded game image for data scans.

This folder is separate from the playable build in `..\WWE 2008`; it reads that folder's game files
and SDK, and has its own build, logs and a copy of the save.

## Milestones

1. **Complete the map** of the functions re:Blue hooks, and check the guest `D3DDevice` layout
   (re:Blue's `gpu/d3d.h` offsets are for Blue Dragon's XDK build).
2. **Build integration:** plume, XenosRecomp, DXC (vcpkg) and the adapted `src/gpu` as a second
   renderer, chosen with a launcher option. The current renderer stays the default and the fallback
   until the native one reaches parity.
3. **Title screen and menus** (2D: textured quads, few shaders).
4. **Matches:** 3D, render targets, resolves, predicated tiling, MSAA.
5. **Entrances and effects** parity (lights, particles, post effects), crowd-shot performance.
6. **Platforms:** Windows D3D12 + Vulkan, Linux, macOS (Metal).

Expect visual bugs while bringing it up; nothing changes for players until it's at parity.

## Status (2026-10-06, end of day)

- **Shaders: done for what's been seen.** `SVR_DUMP_SHADERS` (src/dev/shader_dump.cpp) saved 449
  shaders from boot to title. With three SvR changes to XenosRecomp (patches/xenosrecomp-svr.patch:
  SvR's 16 vertex-input locations incl. morph targets and skinning, sampler slots 16-31 for vertex
  texture fetch, SampleLevel in vertex shaders) **all 449 convert and compile to DXIL and SPIR-V**
  -> generated/native/shader_cache.cpp (re:Blue's cache format). Matches/entrances will add more:
  play with `Collect native data.bat` and re-run XenosRecomp.
- **Renderer code imported:** re:Blue's src/gpu + core helpers in src/reblue (BSD-3 headers kept,
  LICENSE.reblue), minus Blue Dragon-only parts (PSO caches, post effects, output/tweak hooks).
  cmake/native_renderer.cmake builds plume (Vulkan), host shaders (DXC -> SPIR-V headers) and the
  thirdparty bits; not yet wired into CMakeLists.txt or compiled.
- **API surface:** SvR's game code calls 162 D3D library functions directly
  (config/native/d3d_api_surface.txt); most are render/sampler-state setters that only write the
  device's shadow state. 22 lead to GPU command writes; those and the ring-buffer primitives they
  bottom out in (segment allocator sub_82238930, fence wait sub_822386B8, ...) must be hooked or
  stubbed, as re:Blue does.

## Next steps

1. Device takeover: hook Direct3D_CreateDevice (find it; candidate sub_82235770), point the ring
   at a guard buffer, stub the ring primitives, and log any remaining command writer.
2. Map SvR's D3DDevice layout for the fields re:Blue reads (fetch constants, float constants,
   RT/DS shadows, viewport, shaders, index buffer, render-state register shadows).
3. Resources: SvR creates many resources outside D3DDevice_Create* (XG headers from archives), so
   lean on re:Blue's fetch-constant mirroring (native_texture_mirror) for those.
4. Wire into the build, get the title screen drawing, then menus, matches, entrances.
