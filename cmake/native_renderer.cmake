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

# Rendering hardware interface: Vulkan, or Direct3D 12 with SVR_D3D12 (Windows only). re:Blue's
# renderer picks its backend at compile time (REBLUE_D3D12), so the D3D12 build is its own exe.
option(SVR_D3D12 "Native renderer on Direct3D 12 instead of Vulkan (Windows)" OFF)
if(SVR_D3D12 AND NOT WIN32)
    message(FATAL_ERROR "SVR_D3D12 is Windows only")
endif()
set(PLUME_D3D12_ENABLED ${SVR_D3D12} CACHE BOOL "" FORCE)
add_subdirectory("${SVR_NATIVE_ROOT}/third_party/plume" "${CMAKE_BINARY_DIR}/plume" EXCLUDE_FROM_ALL)

# Small third-party pieces the renderer uses.
add_library(svr_native_thirdparty STATIC
    "${SVR_NATIVE_ROOT}/third_party/reblue_thirdparty/zstd/zstddeclib.c"
    "${SVR_XENOS_DIR}/thirdparty/xxHash/xxhash.c"
    "${SVR_XENOS_DIR}/thirdparty/smol-v/source/smolv.cpp"
    "${SVR_NATIVE_ROOT}/third_party/reblue_thirdparty/miniz/miniz.cpp")
target_include_directories(svr_native_thirdparty PUBLIC
    "${SVR_NATIVE_ROOT}/third_party/reblue_thirdparty/zstd"
    "${SVR_XENOS_DIR}/thirdparty/xxHash"
    "${SVR_XENOS_DIR}/thirdparty/smol-v/source"
    "${SVR_NATIVE_ROOT}/third_party/reblue_thirdparty/miniz")

# Host shaders (copy, ImGui overlay, MSAA resolve, gamma) compiled with DXC: SPIR-V headers, or
# DXIL ones for the D3D12 build.
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
    if(SVR_D3D12)
        set(ext dxil)
        set(args "")
    else()
        set(ext spirv)
        # Keep cbuffer byte offsets identical to D3D, and match D3D clip space.
        set(args -spirv -fvk-use-dx-layout)
        if(PROFILE MATCHES "^vs")
            list(APPEND args -fvk-invert-y)
        endif()
    endif()
    set(out "${SVR_NATIVE_GEN}/src/gpu/shaders/hlsl/${STEM}.hlsl.${ext}.h")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${SVR_DXC}" -T ${PROFILE} -HV 2021 -all-resources-bound -Wno-ignored-attributes
                ${args} -I "${SVR_NATIVE_ROOT}" -Fh "${out}" "${SVR_HLSL_DIR}/${STEM}.hlsl"
                -Vn g_${STEM}_${ext}
        DEPENDS "${SVR_HLSL_DIR}/${STEM}.hlsl" ${hlsl_includes}
        COMMENT "Compiling ${STEM}.hlsl (${PROFILE}, ${ext})"
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
# The runtime DXC shader linker (D3D12 has no specialization constants) has no Vulkan counterpart.
set(SVR_D3D12_ONLY_SOURCES
    "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/dxc_link.cpp"
    "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/dxc_link.h"
    "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/shader_linker.cpp"
    "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/shader_linker.h")
if(NOT SVR_D3D12)
    list(REMOVE_ITEM SVR_NATIVE_SOURCES ${SVR_D3D12_ONLY_SOURCES})
endif()

# D3D12: shaders that read specialization constants ship as DXIL libraries. svr_prelink (re:Blue's
# reblue_prelink) DXC-links every variant at build time into linked_shader_cache.cpp, so the game
# never links one while drawing (each runtime link stalls a frame).
if(SVR_D3D12)
    set(SVR_DXC_DIR "${SVR_XENOS_DIR}/thirdparty/dxc-bin/bin/x64")
    add_executable(svr_prelink
        "${SVR_NATIVE_ROOT}/tools/native/prelink_shader_cache.cpp"
        "${SVR_NATIVE_ROOT}/src/reblue/gpu/shaders/dxc_link.cpp"
        "${SVR_NATIVE_ROOT}/generated/native/shader_cache.cpp")
    target_include_directories(svr_prelink PRIVATE "${SVR_NATIVE_ROOT}/src/reblue")
    target_link_libraries(svr_prelink PRIVATE svr_native_thirdparty dxcompiler)
    add_custom_command(TARGET svr_prelink POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SVR_DXC_DIR}/dxcompiler.dll" "${SVR_DXC_DIR}/dxil.dll" "$<TARGET_FILE_DIR:svr_prelink>"
        VERBATIM)
    add_custom_command(
        OUTPUT "${SVR_NATIVE_GEN}/linked_shader_cache.cpp"
        COMMAND svr_prelink "${SVR_NATIVE_GEN}/linked_shader_cache.cpp"
        DEPENDS svr_prelink
        COMMENT "Pre-linking spec-constant shader variants (D3D12)"
        VERBATIM)
endif()

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
    if(SVR_D3D12)
        # Prelinked variants (svr_prelink above); dxcompiler.dll + dxil.dll beside the exe still
        # link any variant the prelink missed.
        target_sources(${target} PRIVATE "${SVR_NATIVE_GEN}/linked_shader_cache.cpp")
        target_compile_definitions(${target} PRIVATE REBLUE_D3D12=1)
        target_link_libraries(${target} PRIVATE d3d12 dxgi dxcompiler)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SVR_DXC_DIR}/dxcompiler.dll" "${SVR_DXC_DIR}/dxil.dll" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
    # SvR's addresses for the D3D functions re:Blue's hooks name (D3DDevice_Clear -> sub_...).
    set_source_files_properties(${SVR_NATIVE_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-include;${SVR_NATIVE_ROOT}/src/native/svr_d3d_names.h")
    target_link_libraries(${target} PRIVATE plume svr_native_thirdparty rex::o1heap)
    # Linker map: names recompiled functions in crash stacks.
    target_link_options(${target} PRIVATE "-Wl,/MAP:${CMAKE_BINARY_DIR}/svr2009.map")
endfunction()
