// SvR 2009 addresses for the XDK D3D functions that re:Blue's renderer hooks by name.
//
// Force-included into the native renderer sources (cmake/native_renderer.cmake), so
// REX_HOOK(D3DDevice_Clear, ...) overrides SvR's recompiled sub_8226EEA8 and
// __imp__D3DDevice_Clear calls its original body. Each entry was matched from SvR 2008's map
// (tools/native/match_functions.py) and confirmed by behaviour: the device fields it writes
// (tools/native/device_offsets.py) and the functions it calls (Present calls this Scissor,
// SetRenderTarget, SynchronizeToPresentationInterval, Swap and Resolve). Functions not listed
// here are either not used by SvR's engine or not identified yet; a hook that names one fails to
// compile on purpose.

#pragma once

// Device / frame
#define D3DDevice_Present                     sub_8225C350
#define D3DDevice_Swap                        sub_8225BD40
#define D3DDevice_SynchronizeToPresentationInterval sub_8225B850
#define D3DDevice_Resolve                     sub_8226B978
#define D3DDevice_Clear                       sub_8226EEA8
#define D3DDevice_SetRenderTarget             sub_8225AEF0
#define D3DDevice_SetDepthStencilSurface      sub_8225B238
#define D3DDevice_SetScissorRect              sub_8225A500

// Draws. 2009 links no D3DDevice_DrawVertices (non-indexed draws go through BeginVertices or
// DrawIndexedVertices), so its hook is compiled out (SVR_NO_DRAW_VERTICES).
#define SVR_NO_DRAW_VERTICES 1
#define D3DDevice_BeginVertices               sub_825A3E98
#define D3DDevice_EndVertices                 sub_825A4330
#define D3DDevice_DrawIndexedVertices         sub_825A4340

// Buffers: the game creates, locks and unlocks its own; the renderer only observes Unlock
// (svr_geometry.cpp). D3DDevice_CreateVertexBuffer sub_82252700, D3DDevice_CreateIndexBuffer
// sub_82252828 and D3DVertexBuffer_Lock sub_822527C8 are matched but deliberately NOT mapped:
// gpu/hooks/resource.cpp's REX_HOOKs would replace them.
#define D3DVertexBuffer_Unlock                sub_82252818
#define D3DIndexBuffer_Unlock                 sub_82253178

// Resource creation
#define D3DDevice_CreateVertexDeclaration     sub_82260EA8
#define D3DDevice_CreateVertexShader          sub_82261170
#define D3DDevice_CreatePixelShader           sub_82261060

// State
#define D3DDevice_SetTexture                  sub_82255270
#define D3DDevice_SetVertexShader             sub_82260B88
#define D3DDevice_SetPixelShader              sub_82260888
#define D3DDevice_SetVertexDeclaration        sub_82260DA0
#define D3DDevice_SetStreamSource             sub_8225A648
#define D3DDevice_SetIndices                  sub_8225A768

// Original bodies (__imp__ = the recompiled function, for hooks that call through).
#define __imp__D3DDevice_Present              __imp__sub_8225C350
#define __imp__D3DDevice_Swap                 __imp__sub_8225BD40
#define __imp__D3DDevice_Resolve              __imp__sub_8226B978
#define __imp__D3DDevice_Clear                __imp__sub_8226EEA8
#define __imp__D3DDevice_SetTexture           __imp__sub_82255270
#define __imp__D3DDevice_SetRenderTarget      __imp__sub_8225AEF0
#define __imp__D3DDevice_SetDepthStencilSurface __imp__sub_8225B238
#define __imp__D3DDevice_SetScissorRect       __imp__sub_8225A500
#define __imp__D3DDevice_BeginVertices        __imp__sub_825A3E98
#define __imp__D3DDevice_EndVertices          __imp__sub_825A4330
#define __imp__D3DDevice_DrawIndexedVertices  __imp__sub_825A4340
#define __imp__D3DDevice_CreateVertexDeclaration __imp__sub_82260EA8
#define __imp__D3DDevice_CreateVertexShader   __imp__sub_82261170
#define __imp__D3DDevice_CreatePixelShader    __imp__sub_82261060
#define __imp__D3DDevice_SetVertexShader      __imp__sub_82260B88
#define __imp__D3DDevice_SetPixelShader       __imp__sub_82260888
#define __imp__D3DDevice_SetVertexDeclaration __imp__sub_82260DA0
#define __imp__D3DDevice_SetStreamSource      __imp__sub_8225A648
#define __imp__D3DDevice_SetIndices           __imp__sub_8225A768
#define __imp__D3DVertexBuffer_Unlock         __imp__sub_82252818
#define __imp__D3DIndexBuffer_Unlock          __imp__sub_82253178

// Resource description getters. Their re:Blue hooks are not wired to the game under SvR (the
// names themselves are not mapped), so these only have to link: Surface GetDesc is an exact match
// (sub_82255148); the two LevelDesc getters are the closest matches, unverified.
#define __imp__D3DSurface_GetDesc             __imp__sub_82255148
#define __imp__D3DTexture_GetLevelDesc        __imp__sub_822533E8
#define __imp__D3DVolumeTexture_GetLevelDesc  __imp__sub_822545C0
