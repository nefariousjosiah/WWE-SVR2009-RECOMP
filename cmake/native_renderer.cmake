# Native renderer (adapted from re:Blue, BSD-3-Clause; see src/reblue/LICENSE.reblue).
#
# Draws through plume (Vulkan here; D3D12/Metal later) by hooking the game's XDK D3D functions,
# instead of emulating the Xenos GPU. Game shaders come precompiled from generated/native/
# shader_cache.cpp (XenosRecomp over the shaders dumped with SVR_DUMP_SHADERS; see
# tools/native/build_shader_cache.ps1).
#
# The renderer's sources go straight into the executable: its REX_HOOK overrides are strong
# definitions nothing else references, so a static library would drop them at link time.

enable_language(C)  # zstd and xxHash are C

set(SVR_NATIVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(SVR_XENOS_DIR "${SVR_NATIVE_ROOT}/third_party/XenosRecomp")
set(SVR_NATIVE_GEN "${CMAKE_BINARY_DIR}/native_gen")

# Rendering hardware interface (Vulkan always; D3D12 on Windows, Metal on macOS).
# Vulkan first: plume's D3D12 backend needs newer Windows SDK headers than this toolchain has.
set(PLUME_D3D12_ENABLED OFF CACHE BOOL "" FORCE)
add_subdirectory("${SVR_NATIVE_ROOT}/third_party/plume" "${CMAKE_BINARY_DIR}/plume" EXCLUDE_FROM_ALL)

# Small third-party pieces the renderer uses.
add_library(svr_native_thirdparty STATIC
    "${SVR_NATIVE_ROOT}/third_party/reblue_thirdparty/zstd/zstddeclib.c"
    "${SVR_XENOS_DIR}/thirdparty/xxHash/xxhash.c"
    "${SVR_XENOS_DIR}/thirdparty/smol-v/source/smolv.cpp")
target_include_directories(svr_native_thirdparty PUBLIC
    "${SVR_NATIVE_ROOT}/third_party/reblue_thirdparty/zstd"
    "${SVR_XENOS_DIR}/thirdparty/xxHash"
    "${SVR_XENOS_DIR}/thirdparty/smol-v/source")

# Host shaders (copy, ImGui overlay, MSAA resolve, gamma) compiled to SPIR-V headers with DXC.
if(WIN32)
    set(SVR_DXC "${SVR_XENOS_DIR}/thirdparty/dxc-bin/bin/x64/dxc.exe")
elseif(APPLE)
    set(SVR_DXC "${SVR_XENOS_DIR}/thirdparty/dxc-bin/bin/x64/dxc-macos")
else()
    set(SVR_DXC "${SVR_XENOS_DIR}/thirdparty/dxc-bin/bin/x64/dxc-linux")
endif()
set(SVR_HLSL_DIR "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/hlsl")
set(SVR_HOST_SHADER_HEADERS "")
function(svr_host_shader STEM PROFILE)
    file(GLOB hlsl_includes "${SVR_HLSL_DIR}/*.hlsli")
    set(args -spirv -fvk-use-dx-layout)
    if(PROFILE MATCHES "^vs")
        list(APPEND args -fvk-invert-y)
    endif()
    set(out "${SVR_NATIVE_GEN}/src/gpu/shaders/hlsl/${STEM}.hlsl.spirv.h")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${SVR_DXC}" -T ${PROFILE} -HV 2021 -all-resources-bound -Wno-ignored-attributes
                ${args} -I "${SVR_NATIVE_ROOT}" -Fh "${out}" "${SVR_HLSL_DIR}/${STEM}.hlsl"
                -Vn g_${STEM}_spirv
        DEPENDS "${SVR_HLSL_DIR}/${STEM}.hlsl" ${hlsl_includes}
        COMMENT "Compiling ${STEM}.hlsl (${PROFILE}, SPIR-V)"
        VERBATIM)
    set(SVR_HOST_SHADER_HEADERS ${SVR_HOST_SHADER_HEADERS} "${out}" PARENT_SCOPE)
endfunction()
foreach(shader IN ITEMS copy_vs imgui_vs)
    svr_host_shader(${shader} vs_6_0)
endforeach()
foreach(shader IN ITEMS copy_color_ps copy_depth_ps gamma_correction_ps pfx_occlusion_count_ps
        imgui_ps resolve_msaa_color_2x resolve_msaa_color_4x resolve_msaa_color_8x
        resolve_msaa_depth_2x resolve_msaa_depth_4x resolve_msaa_depth_8x)
    svr_host_shader(${shader} ps_6_0)
endforeach()

file(GLOB_RECURSE SVR_NATIVE_SOURCES CONFIGURE_DEPENDS
    "${SVR_NATIVE_ROOT}/src/reblue/*.cpp" "${SVR_NATIVE_ROOT}/src/reblue/*.h"
    "${SVR_NATIVE_ROOT}/src/native/*.cpp" "${SVR_NATIVE_ROOT}/src/native/*.h")

function(svr_add_native_renderer target)
    target_sources(${target} PRIVATE
        ${SVR_NATIVE_SOURCES}
        ${SVR_HOST_SHADER_HEADERS}
        "${SVR_NATIVE_ROOT}/generated/native/shader_cache.cpp")
    target_include_directories(${target} PRIVATE
        "${SVR_NATIVE_ROOT}/src/reblue"
        "${SVR_NATIVE_ROOT}/src/native"
        "${SVR_NATIVE_GEN}"
        "${SVR_NATIVE_ROOT}/third_party/plume"
        "${SVR_XENOS_DIR}/XenosRecomp")
    target_compile_definitions(${target} PRIVATE SVR_NATIVE_RENDERER=1)
    # SvR's addresses for the D3D functions re:Blue's hooks name (D3DDevice_Clear -> sub_...).
    set_source_files_properties(${SVR_NATIVE_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-include;${SVR_NATIVE_ROOT}/src/native/svr_d3d_names.h")
    target_link_libraries(${target} PRIVATE plume svr_native_thirdparty rex::o1heap)
    # Linker map: names recompiled functions in crash stacks (tools/native/catch_crash.py).
    target_link_options(${target} PRIVATE "-Wl,/MAP:${CMAKE_BINARY_DIR}/svr2009.map")
endfunction()
