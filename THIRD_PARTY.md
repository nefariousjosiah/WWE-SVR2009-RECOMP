# Third-party components

This project's own code, including the native renderer for SmackDown vs. Raw, is by
nefariousjosiah and licensed under the GNU GPL v3.0 ([LICENSE](LICENSE)). It builds on the projects below, which keep
their own licences. The release download carries all of these licence texts in its `licenses`
folder.

## In this repository, or fetched by `tools/windows/setup.ps1`

| Component | How it is included | Licence |
|---|---|---|
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (recompiler + runtime; portions from [Xenia](https://xenia.jp)) | fetched at `c94f5eb` by `tools/windows/setup.ps1`, plus `patches/rexglue-sdk.patch` | BSD 3-Clause (`third_party/rexglue-sdk/LICENSE`) |
| [re:Blue](https://github.com/zolaware/reblue) renderer core | adapted for SvR in `src/reblue`; the SvR changes are by nefariousjosiah (GPL-3.0) | BSD 3-Clause, `src/reblue/LICENSE.reblue` and each file's header |
| [plume](https://github.com/zolaware/plume) | fetched at `e0c8871`, plus `patches/plume.patch` | MIT |
| [XenosRecomp](https://github.com/zolaware/reblue-XenosRecomp) (build tool, shader recompiler) | fetched at `339af41`, plus `patches/xenosrecomp.patch` | MIT |
| [zstd](https://github.com/facebook/zstd) (decompression, single-file decoder) | `third_party/reblue_thirdparty/zstd` | BSD, `third_party/reblue_thirdparty/zstd/LICENSE` |
| Press Start 2P font (FPS counter) | `res/fonts` | SIL Open Font License 1.1, `res/fonts/OFL.txt` |
| [SDL 3](https://github.com/libsdl-org/SDL) (launcher window, controller, file dialogs) | the SDK's copy, `third_party/rexglue-sdk/thirdparty/sdl3`, built by `launcher/CMakeLists.txt` | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.92.5 (launcher UI) | `launcher/third_party/imgui` | MIT, `launcher/third_party/imgui/LICENSE.txt` |
| [stb_image](https://github.com/nothings/stb) 2.29 (launcher box art) | `launcher/third_party/stb` | public domain or MIT (end of `stb_image.h`) |
| Roboto Medium font (launcher) | `launcher/res/fonts` | Apache 2.0, `launcher/res/fonts/LICENSE-Roboto.txt` |
| Box art (`launcher/res/cover.jpg`) | shown by the launcher | owned by THQ / WWE; used only to identify the game |

## Built into the game's DLLs

`rexruntime.dll` and `rexgpu-xenos.dll` are built from the ReXGlue SDK (commit `c94f5eb` plus
`patches/rexglue-sdk.patch`) and contain these libraries, as the SDK includes them:

| Library | Used for | Licence |
|---|---|---|
| [FFmpeg](https://github.com/wmarti/FFmpeg) (libavcodec, libavutil), commit `0604b464` | the game's audio (XMA) | LGPL 2.1 or later |
| [libmspack](https://github.com/kyz/libmspack) (LZX decoder), commit `30590772` | unpacking the game's executable | LGPL 2.1 |
| [SDL 3](https://github.com/libsdl-org/SDL) | window, controllers, audio output | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) | on-screen menus | MIT |
| [fmt](https://github.com/fmtlib/fmt), [spdlog](https://github.com/gabime/spdlog) | text formatting, logging | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) | settings files | MIT |
| [xxHash](https://github.com/Cyan4973/xxHash) | hashing | BSD 2-Clause |
| [utf8-cpp](https://github.com/nemtrif/utfcpp) | text encoding | Boost Software License 1.0 |
| [disruptorplus](https://github.com/lewissbaker/disruptorplus) | threading | MIT |
| [o1heap](https://github.com/pavel-kirienko/o1heap) | memory allocator | MIT |
| aes_128 | decrypting the game's executable | MIT |
| [SIMDe](https://github.com/simd-everywhere/simde) | SIMD helpers | MIT |
| [glslang](https://github.com/KhronosGroup/glslang) | shader compiler | BSD 3-Clause and others (its licence file) |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | GPU memory | MIT |
| Vulkan and SPIR-V headers (Khronos) | API definitions | Apache 2.0 / MIT |
| RenderDoc API header | graphics debugging hook | MIT |

**The LGPL libraries (FFmpeg, libmspack)** are statically linked into `rexruntime.dll`; this
project doesn't change them. You may replace them with your own versions: their source is at the
links and commits above, and `rexruntime.dll` builds from the ReXGlue SDK source (`setup.ps1` fetches
it and applies `patches/rexglue-sdk.patch`). Put the rebuilt `rexruntime.dll` next to `svr2009.exe`.
