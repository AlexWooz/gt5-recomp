// upscale_dlss.cpp - gt5_upscale_dlss.dll: NVIDIA DLSS behind gt5_upscale.h: Super Resolution and DLAA
// (the upscaler's calls) and DLSS Frame Generation (the fg_ calls, interface version 2).
//
// This file is the project's own; what it includes and links (nvsdk_ngx*.h, nvsdk_ngx_d.lib) is NVIDIA's
// DLSS SDK, which is NOT part of the project: scripts\add_dlss.ps1 fetches it from NVIDIA's repository
// on the player's PC, after showing NVIDIA's licence, and builds this DLL there. nvngx_dlss.dll and
// nvngx_dlssg.dll (NVIDIA's, signed) sit next to it. The game loads gt5_upscale_dlss.dll if it is there
// and never needs it.
//
// NGX rules kept here: one thread (the renderer's), LDR colour by leaving IsHDR off, vectors at render
// size in pixels, no sharpening (deprecated in DLSS), features released by destroy() / fg_destroy() only.
//
// Frame generation is asked from NGX directly (NVSDK_NGX_Feature_FrameGeneration), without Streamline:
// the made frame is written into a texture of the renderer's and the renderer presents it. What a
// stand-alone test (dlssgtest) found and this file relies on:
//   - at this level DLSSG.MvecScale takes the vectors to PIXELS of the render size (1 for ours), not to
//     the -1..1 range Streamline's guide speaks of;
//   - with sub-rectangles the feature is made for the 3D rectangle's size, not the texture's;
//   - matrices are row-major for row vectors; identity clip-to-previous-clip also works, since all
//     motion is in the vectors.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include <d3d12.h>
#ifndef _NTDEF_
typedef LONG NTSTATUS;
#endif
#include <d3dkmthk.h>              // the graphics kernel's own word on GPU scheduling (gdi32)

#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"
#include "nvsdk_ngx_helpers_dlssg_d3d.h"

#include "gt5_upscale.h"

namespace {

// The project's own identifier for NGX (Init_with_ProjectID wants a GUID-like string; made once, fixed).
const char kProjectId[] = "6f2d9c41-3b7e-4c58-9a10-5e8f27d4b3a6";
const char kEngineVersion[] = "1.0";

NVSDK_NGX_Parameter* s_caps;        // capability parameters (NGX owns their lifetime until destroyed)
NVSDK_NGX_Parameter* s_params;      // the upscaler's own parameters
NVSDK_NGX_Handle* s_feature;
bool s_inited;
char s_error[240];
char s_init_message[200];           // why NGX did not start, for fg_caps

// frame generation
NVSDK_NGX_Parameter* s_fg_params;
NVSDK_NGX_Handle* s_fg_feature;
gt5_fg_state s_fg;
unsigned s_fg_flags;
ID3D12Device* s_device;
wchar_t s_dirs[2][MAX_PATH];        // where NGX may look for NVIDIA's DLLs besides the game's folder
const wchar_t* s_dir_list[2];
unsigned s_n_dirs;

void set_error(const char* what, NVSDK_NGX_Result r)
{
    char wide[160] = "";
    const wchar_t* w = GetNGXResultAsString(r);
    if (w) WideCharToMultiByte(CP_UTF8, 0, w, -1, wide, (int)sizeof wide, nullptr, nullptr);
    snprintf(s_error, sizeof s_error, "%s: 0x%08X %s", what, (unsigned)r, wide);
}

NVSDK_NGX_PerfQuality_Value quality_of(gt5_upscale_mode m)
{
    switch (m) {
    case GT5_UPSCALE_NATIVE:            return NVSDK_NGX_PerfQuality_Value_DLAA;
    case GT5_UPSCALE_QUALITY:           return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case GT5_UPSCALE_BALANCED:          return NVSDK_NGX_PerfQuality_Value_Balanced;
    case GT5_UPSCALE_PERFORMANCE:       return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case GT5_UPSCALE_ULTRA_PERFORMANCE: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    }
    return NVSDK_NGX_PerfQuality_Value_MaxQuality;
}

// The folder this DLL is in (NVIDIA's DLLs sit beside it), and GT5_NGX_DIR if set (tests): both are
// given to NGX as places to look, after the game's own folder.
void find_dirs()
{
    s_n_dirs = 0;
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void*)&find_dirs, &self) && self) {
        const DWORD n = GetModuleFileNameW(self, s_dirs[0], MAX_PATH);
        wchar_t* cut = n && n < MAX_PATH ? wcsrchr(s_dirs[0], L'\\') : nullptr;
        if (cut) { *cut = 0; s_dir_list[s_n_dirs++] = s_dirs[0]; }
    }
    const DWORD e = GetEnvironmentVariableW(L"GT5_NGX_DIR", s_dirs[1], MAX_PATH);
    if (e && e < MAX_PATH) s_dir_list[s_n_dirs++] = s_dirs[1];
}

// Is NVIDIA's file there (in one of those folders or in the game's)? Its version, when it is.
bool find_ngx_file(const wchar_t* name, char* version, size_t cap)
{
    if (version && cap) version[0] = 0;
    wchar_t exe[MAX_PATH] = L"";
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* cut = n && n < MAX_PATH ? wcsrchr(exe, L'\\') : nullptr;
    if (cut) *cut = 0;
    const wchar_t* dirs[3] = { s_n_dirs > 0 ? s_dir_list[0] : nullptr, s_n_dirs > 1 ? s_dir_list[1] : nullptr, cut ? exe : nullptr };
    for (const wchar_t* d : dirs) {
        if (!d) continue;
        wchar_t path[MAX_PATH + 40];
        _snwprintf(path, MAX_PATH + 40, L"%ls\\%ls", d, name);
        path[MAX_PATH + 39] = 0;
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) continue;
        DWORD dummy = 0;
        const DWORD sz = GetFileVersionInfoSizeW(path, &dummy);
        if (sz && version && cap) {
            void* buf = malloc(sz);
            VS_FIXEDFILEINFO* fi = nullptr; UINT fl = 0;
            if (buf && GetFileVersionInfoW(path, 0, sz, buf) && VerQueryValueW(buf, L"\\", (void**)&fi, &fl) && fi)
                snprintf(version, cap, "%u.%u.%u", (unsigned)HIWORD(fi->dwFileVersionMS), (unsigned)LOWORD(fi->dwFileVersionMS),
                         (unsigned)HIWORD(fi->dwFileVersionLS));
            free(buf);
        }
        return true;
    }
    return false;
}

int dlss_init(ID3D12Device* device, const wchar_t* dir, gt5_upscale_caps* caps)
{
    memset(caps, 0, sizeof *caps);
    snprintf(caps->name, sizeof caps->name, "NVIDIA DLSS");
    s_error[0] = 0;
    if (!device) { snprintf(caps->message, sizeof caps->message, "no Direct3D 12 device"); return 0; }
    if (!s_inited) {
        find_dirs();
        static NVSDK_NGX_FeatureCommonInfo info;
        memset(&info, 0, sizeof info);
        info.PathListInfo.Path = s_dir_list; info.PathListInfo.Length = s_n_dirs;
        const NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, dir && *dir ? dir : L".", device, &info,
            NVSDK_NGX_Version_API);
        caps->init_result = (int)r;
        if (NVSDK_NGX_FAILED(r)) {
            set_error("NGX could not start", r);
            snprintf(caps->message, sizeof caps->message, "%s",
                     r == NVSDK_NGX_Result_FAIL_FeatureNotSupported ? "this graphics card does not support DLSS"
                     : r == NVSDK_NGX_Result_FAIL_OutOfDate ? "the NVIDIA graphics driver is too old for DLSS"
                     : s_error);
            snprintf(s_init_message, sizeof s_init_message, "%s", caps->message);
            return 1;   // answered: not available
        }
        s_inited = true;
        s_device = device;
    }
    NVSDK_NGX_Result r = NVSDK_NGX_D3D12_GetCapabilityParameters(&s_caps);
    if (NVSDK_NGX_FAILED(r) || !s_caps) {
        set_error("NGX capability parameters", r);
        snprintf(caps->message, sizeof caps->message, "%s", s_error);
        snprintf(s_init_message, sizeof s_init_message, "%s", caps->message);
        return 1;
    }
    int avail = 0, need = 0, init_result = 0;
    unsigned major = 0, minor = 0;
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_SuperSampling_Available, &avail);
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &need);
    NVSDK_NGX_Parameter_GetUI(s_caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &major);
    NVSDK_NGX_Parameter_GetUI(s_caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minor);
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &init_result);
    caps->available = avail != 0;
    caps->needs_driver = need != 0;
    caps->min_driver_major = major; caps->min_driver_minor = minor;
    caps->init_result = init_result;
    if (!avail) {
        if (need)
            snprintf(caps->message, sizeof caps->message, "DLSS needs NVIDIA driver %u.%u or newer", major, minor);
        else {
            set_error("DLSS is not available here", (NVSDK_NGX_Result)init_result);
            snprintf(caps->message, sizeof caps->message, "%s", s_error);
        }
    }
    return 1;
}

int dlss_optimal(unsigned out_w, unsigned out_h, gt5_upscale_mode mode, unsigned* render_w, unsigned* render_h)
{
    if (!s_caps || !render_w || !render_h) return 0;
    unsigned ow = 0, oh = 0, maxw = 0, maxh = 0, minw = 0, minh = 0;
    float sharp = 0.0f;
    const NVSDK_NGX_Result r = NGX_DLSS_GET_OPTIMAL_SETTINGS(s_caps, out_w, out_h, quality_of(mode), &ow, &oh, &maxw,
                                                            &maxh, &minw, &minh, &sharp);
    if (NVSDK_NGX_FAILED(r) || !ow || !oh) { set_error("DLSS optimal settings", r); return 0; }
    *render_w = ow; *render_h = oh;
    return 1;
}

void dlss_destroy()
{
    if (s_feature) { NVSDK_NGX_D3D12_ReleaseFeature(s_feature); s_feature = nullptr; }
    if (s_params) { NVSDK_NGX_D3D12_DestroyParameters(s_params); s_params = nullptr; }
}

int dlss_create(ID3D12GraphicsCommandList* list, unsigned render_w, unsigned render_h, unsigned out_w, unsigned out_h,
                gt5_upscale_mode mode, unsigned flags, int preset)
{
    if (!s_inited || !list) return 0;
    dlss_destroy();
    NVSDK_NGX_Result r = NVSDK_NGX_D3D12_AllocateParameters(&s_params);
    if (NVSDK_NGX_FAILED(r) || !s_params) { set_error("NGX parameters", r); return 0; }
    if (preset > 0) {
        // The same model variant for whatever mode is asked for (the menu's "preset" choice).
        NVSDK_NGX_Parameter_SetUI(s_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, (unsigned)preset);
        NVSDK_NGX_Parameter_SetUI(s_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, (unsigned)preset);
        NVSDK_NGX_Parameter_SetUI(s_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, (unsigned)preset);
        NVSDK_NGX_Parameter_SetUI(s_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, (unsigned)preset);
        NVSDK_NGX_Parameter_SetUI(s_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, (unsigned)preset);
    }
    NVSDK_NGX_DLSS_Create_Params cp;
    memset(&cp, 0, sizeof cp);
    cp.Feature.InWidth = render_w; cp.Feature.InHeight = render_h;
    cp.Feature.InTargetWidth = out_w; cp.Feature.InTargetHeight = out_h;
    cp.Feature.InPerfQualityValue = quality_of(mode);
    int f = NVSDK_NGX_DLSS_Feature_Flags_None;
    if (flags & GT5_UPSCALE_HDR) f |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if (!(flags & GT5_UPSCALE_MV_OUTPUT_SIZE)) f |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if (flags & GT5_UPSCALE_MV_JITTERED) f |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
    if (flags & GT5_UPSCALE_DEPTH_INVERTED) f |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    if (flags & GT5_UPSCALE_AUTO_EXPOSURE) f |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    cp.InFeatureCreateFlags = f;
    r = NGX_D3D12_CREATE_DLSS_EXT(list, 1, 1, &s_feature, s_params, &cp);
    if (NVSDK_NGX_FAILED(r) || !s_feature) {
        set_error("DLSS could not be made for these sizes", r);
        dlss_destroy();
        return 0;
    }
    return 1;
}

int dlss_evaluate(const gt5_upscale_frame* fr)
{
    if (!s_feature || !s_params || !fr || !fr->list) return 0;
    NVSDK_NGX_D3D12_DLSS_Eval_Params ep;
    memset(&ep, 0, sizeof ep);
    ep.Feature.pInColor = fr->color;
    ep.Feature.pInOutput = fr->output;
    ep.pInDepth = fr->depth;
    ep.pInMotionVectors = fr->motion;
    ep.InJitterOffsetX = fr->jitter_x;
    ep.InJitterOffsetY = fr->jitter_y;
    ep.InRenderSubrectDimensions.Width = fr->render_w;
    ep.InRenderSubrectDimensions.Height = fr->render_h;
    ep.InReset = fr->reset ? 1 : 0;
    ep.InMVScaleX = fr->mv_scale_x == 0.0f ? 1.0f : fr->mv_scale_x;
    ep.InMVScaleY = fr->mv_scale_y == 0.0f ? 1.0f : fr->mv_scale_y;
    ep.InFrameTimeDeltaInMsec = fr->frame_ms;
    const NVSDK_NGX_Result r = NGX_D3D12_EVALUATE_DLSS_EXT(fr->list, s_feature, s_params, &ep);
    if (NVSDK_NGX_FAILED(r)) { set_error("DLSS evaluate", r); return 0; }
    return 1;
}

// ---- frame generation ---------------------------------------------------------------------------------

// Is Windows' hardware-accelerated GPU scheduling on for this card? (NVIDIA asks for it.) -1 when it
// cannot be told.
int gpu_scheduling_on()
{
    if (!s_device) return -1;
    D3DKMT_OPENADAPTERFROMLUID oa = {};
    oa.AdapterLuid = s_device->GetAdapterLuid();
    if (D3DKMTOpenAdapterFromLuid(&oa) != 0) return -1;
    D3DKMT_WDDM_2_7_CAPS wc = {};
    D3DKMT_QUERYADAPTERINFO qa = {};
    qa.hAdapter = oa.hAdapter; qa.Type = KMTQAITYPE_WDDM_2_7_CAPS; qa.pPrivateDriverData = &wc; qa.PrivateDriverDataSize = sizeof wc;
    const NTSTATUS ks = D3DKMTQueryAdapterInfo(&qa);
    D3DKMT_CLOSEADAPTER ca = {}; ca.hAdapter = oa.hAdapter;
    D3DKMTCloseAdapter(&ca);
    return ks == 0 ? (wc.HwSchEnabled ? 1 : 0) : -1;
}

int dlss_fg_caps(gt5_fg_caps* caps)
{
    if (!caps) return 0;
    memset(caps, 0, sizeof *caps);
    snprintf(caps->name, sizeof caps->name, "NVIDIA DLSS Frame Generation");
    caps->wants_camera = 1;
    caps->max_generated = 1;
    if (!s_inited || !s_caps) {
        snprintf(caps->message, sizeof caps->message, "%s", s_init_message[0] ? s_init_message : "DLSS did not start");
        return 1;
    }
    if (!find_ngx_file(L"nvngx_dlssg.dll", caps->version, sizeof caps->version)) {
        /* (the menu shows up to 111 characters of a reason) */
        snprintf(caps->message, sizeof caps->message, "nvngx_dlssg.dll is not installed: run \"Get NVIDIA DLSS\" in the launcher again");
        return 1;
    }
    int avail = 0, need = 0, init_result = 0;
    unsigned major = 0, minor = 0, multi = 0;
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_FrameGeneration_Available, &avail);
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_FrameGeneration_NeedsUpdatedDriver, &need);
    NVSDK_NGX_Parameter_GetUI(s_caps, NVSDK_NGX_Parameter_FrameGeneration_MinDriverVersionMajor, &major);
    NVSDK_NGX_Parameter_GetUI(s_caps, NVSDK_NGX_Parameter_FrameGeneration_MinDriverVersionMinor, &minor);
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult, &init_result);
    if (NVSDK_NGX_SUCCEED(NVSDK_NGX_Parameter_GetUI(s_caps, NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &multi)) && multi > 1)
        caps->max_generated = multi > 3 ? 3 : multi;
    caps->available = avail != 0;
    caps->needs_driver = need != 0;
    caps->min_driver_major = major; caps->min_driver_minor = minor;
    caps->init_result = init_result;
    if (!avail) {
        if (need)
            snprintf(caps->message, sizeof caps->message, "DLSS Frame Generation needs NVIDIA driver %u.%u or newer", major, minor);
        else if ((NVSDK_NGX_Result)init_result == NVSDK_NGX_Result_FAIL_FeatureNotSupported)
            snprintf(caps->message, sizeof caps->message, "this graphics card does not support DLSS Frame Generation (it needs an RTX 40 series card or newer)");
        else if (gpu_scheduling_on() == 0)
            snprintf(caps->message, sizeof caps->message, "turn on Hardware-accelerated GPU scheduling in Windows' graphics settings, then restart the PC");
        else {
            set_error("DLSS Frame Generation is not available here", (NVSDK_NGX_Result)init_result);
            snprintf(caps->message, sizeof caps->message, "%s", s_error);
        }
    }
    return 1;
}

void dlss_fg_destroy()
{
    if (s_fg_feature) { NVSDK_NGX_D3D12_ReleaseFeature(s_fg_feature); s_fg_feature = nullptr; }
    if (s_fg_params) { NVSDK_NGX_D3D12_DestroyParameters(s_fg_params); s_fg_params = nullptr; }
    memset(&s_fg, 0, sizeof s_fg);
}

int dlss_fg_create(ID3D12GraphicsCommandList* list, unsigned out_w, unsigned out_h, unsigned rect_w, unsigned rect_h,
                   unsigned render_w, unsigned render_h, unsigned max_generated, unsigned flags)
{
    if (!s_inited || !s_caps || !list || !out_w || !out_h || !render_w || !render_h) return 0;
    if (!rect_w || !rect_h || rect_w > out_w || rect_h > out_h) { rect_w = out_w; rect_h = out_h; }
    dlss_fg_destroy();
    NVSDK_NGX_Result r = NVSDK_NGX_D3D12_AllocateParameters(&s_fg_params);
    if (NVSDK_NGX_FAILED(r) || !s_fg_params) { set_error("NGX parameters", r); return 0; }
    // what every call will bring, and what none will (the HUD-free picture stays optional)
    const unsigned always = NVSDK_NGX_DLSSG_ResourceFlags_Backbuffer | NVSDK_NGX_DLSSG_ResourceFlags_MVecs |
                            NVSDK_NGX_DLSSG_ResourceFlags_Depth | NVSDK_NGX_DLSSG_ResourceFlags_OutputInterpolated;
    const unsigned never = NVSDK_NGX_DLSSG_ResourceFlags_UI | NVSDK_NGX_DLSSG_ResourceFlags_UIAlpha |
                           NVSDK_NGX_DLSSG_ResourceFlags_BidirectionalDistortionField | NVSDK_NGX_DLSSG_ResourceFlags_OutputReal |
                           NVSDK_NGX_DLSSG_ResourceFlags_OutputDisableInterpolation;
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_ResourceAlwaysProvided_Flags, always);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_ResourceNeverProvided_Flags, never);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_FullscreenMode, NVSDK_NGX_DLSSG_FullscreenMode_Windowed);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_AsyncCreateEnabled, 0);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_UserInterfaceRecompositionEnabled, 0);
    // The feature is made for the 3D rectangle's size ("extent"), not for the texture's: with another
    // size NGX refuses every evaluate (invalid parameter).
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_Width, rect_w);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_Height, rect_h);
    NVSDK_NGX_DLSSG_Create_Params cp = {};
    cp.Width = rect_w; cp.Height = rect_h;
    cp.NativeBackbufferFormat = (unsigned)DXGI_FORMAT_R8G8B8A8_UNORM;
    cp.RenderWidth = render_w; cp.RenderHeight = render_h;
    cp.DynamicResolutionScaling = false;
    r = NGX_D3D12_CREATE_DLSSG(list, 1, 1, &s_fg_feature, s_fg_params, &cp);
    if (NVSDK_NGX_FAILED(r) || !s_fg_feature) {
        set_error("DLSS Frame Generation could not be made for these sizes", r);
        dlss_fg_destroy();
        return 0;
    }
    s_fg.out_w = out_w; s_fg.out_h = out_h; s_fg.rect_w = rect_w; s_fg.rect_h = rect_h;
    s_fg.render_w = render_w; s_fg.render_h = render_h;
    s_fg.max_generated = max_generated < 1 ? 1 : max_generated > 3 ? 3 : max_generated;
    s_fg_flags = flags;
    size_t est = 0;
    if (NVSDK_NGX_SUCCEED(NGX_D3D12_ESTIMATE_VRAM_DLSSG(s_caps, render_w, render_h, rect_w, rect_h, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                        DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_UNKNOWN,
                                                        DXGI_FORMAT_UNKNOWN, &est)))
        s_fg.memory_mb = (unsigned)((est + 524288u) / 1048576u);
    return 1;
}

void mat_identity(float m[4][4]) { memset(m, 0, 16 * sizeof(float)); m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f; }
bool mat_given(const float* m) { for (int i = 0; i < 16; i++) if (m[i] != 0.0f) return true; return false; }

int dlss_fg_evaluate(const gt5_fg_frame* fr)
{
    if (!s_fg_feature || !s_fg_params) { snprintf(s_error, sizeof s_error, "DLSS Frame Generation was not made"); return 0; }
    if (!fr || !fr->list || !fr->color || !fr->depth || !fr->motion || !fr->output[0]) {
        snprintf(s_error, sizeof s_error, "DLSS Frame Generation was given a frame without a list, a picture, depth, vectors or an output texture");
        return 0;
    }
    const unsigned gen = fr->generated < 1 ? 1 : fr->generated > s_fg.max_generated ? s_fg.max_generated : fr->generated;
    const unsigned rw = fr->render_w ? fr->render_w : s_fg.render_w, rh = fr->render_h ? fr->render_h : s_fg.render_h;
    const unsigned gw = fr->rect_w ? fr->rect_w : s_fg.out_w, gh = fr->rect_h ? fr->rect_h : s_fg.out_h;
    if (gw != s_fg.rect_w || gh != s_fg.rect_h) {
        snprintf(s_error, sizeof s_error, "DLSS Frame Generation was made for a %ux%u picture and is given %ux%u", s_fg.rect_w, s_fg.rect_h, gw, gh);
        return 0;
    }
    NVSDK_NGX_DLSSG_Opt_Eval_Params op = {};
    // The camera. Known: as given. Not known: a pinhole camera that stands still, with the planes and the
    // field of view that are given or, failing those, plausible ones; all motion is in the vectors.
    const float aspect = fr->cam_aspect > 0.0f ? fr->cam_aspect : (float)rw / (float)rh;
    const float fov = fr->cam_fov_y > 0.01f && fr->cam_fov_y < 3.0f ? fr->cam_fov_y : 0.9f;
    const float zn = fr->cam_near > 0.0f ? fr->cam_near : 0.1f;
    const float zf = fr->cam_far > zn ? fr->cam_far : zn * 10000.0f;
    if (fr->cam_ok && mat_given(fr->view_to_clip) && mat_given(fr->clip_to_view)) {
        memcpy(op.cameraViewToClip, fr->view_to_clip, sizeof op.cameraViewToClip);
        memcpy(op.clipToCameraView, fr->clip_to_view, sizeof op.clipToCameraView);
    } else {
        const float ys = 1.0f / tanf(fov * 0.5f), xs = ys / aspect, zq = zf / (zf - zn);
        memset(op.cameraViewToClip, 0, sizeof op.cameraViewToClip);
        op.cameraViewToClip[0][0] = xs; op.cameraViewToClip[1][1] = ys; op.cameraViewToClip[2][2] = zq;
        op.cameraViewToClip[2][3] = 1.0f; op.cameraViewToClip[3][2] = -zn * zq;
        memset(op.clipToCameraView, 0, sizeof op.clipToCameraView);
        op.clipToCameraView[0][0] = 1.0f / xs; op.clipToCameraView[1][1] = 1.0f / ys; op.clipToCameraView[2][3] = -1.0f / (zn * zq);
        op.clipToCameraView[3][2] = 1.0f; op.clipToCameraView[3][3] = 1.0f / zn;
    }
    mat_identity(op.clipToLensClip);
    if (fr->cam_ok && !fr->reset && mat_given(fr->clip_to_prev_clip) && mat_given(fr->prev_clip_to_clip)) {
        memcpy(op.clipToPrevClip, fr->clip_to_prev_clip, sizeof op.clipToPrevClip);
        memcpy(op.prevClipToClip, fr->prev_clip_to_clip, sizeof op.prevClipToClip);
    } else {
        mat_identity(op.clipToPrevClip); mat_identity(op.prevClipToClip);
    }
    // the jitter as a clip-space offset (untested: the game gives none to frame generation)
    op.jitterOffset[0] = 2.0f * fr->jitter_x / (float)rw; op.jitterOffset[1] = -2.0f * fr->jitter_y / (float)rh;
    // At this level the scale takes the vectors to pixels of the render size.
    op.mvecScale[0] = fr->mv_scale_x == 0.0f ? 1.0f : fr->mv_scale_x;
    op.mvecScale[1] = fr->mv_scale_y == 0.0f ? 1.0f : fr->mv_scale_y;
    op.cameraPinholeOffset[0] = 0.0f; op.cameraPinholeOffset[1] = 0.0f;
    if (fr->cam_ok) {
        memcpy(op.cameraPos, fr->cam_pos, sizeof op.cameraPos);
        memcpy(op.cameraUp, fr->cam_up, sizeof op.cameraUp);
        memcpy(op.cameraRight, fr->cam_right, sizeof op.cameraRight);
        memcpy(op.cameraFwd, fr->cam_fwd, sizeof op.cameraFwd);
    } else {
        op.cameraPos[0] = op.cameraPos[1] = op.cameraPos[2] = 0.0f;
        op.cameraUp[0] = 0.0f; op.cameraUp[1] = 1.0f; op.cameraUp[2] = 0.0f;
        op.cameraRight[0] = 1.0f; op.cameraRight[1] = 0.0f; op.cameraRight[2] = 0.0f;
        op.cameraFwd[0] = 0.0f; op.cameraFwd[1] = 0.0f; op.cameraFwd[2] = 1.0f;
    }
    op.cameraNear = zn; op.cameraFar = zf; op.cameraFOV = fov; op.cameraAspectRatio = aspect;
    op.colorBuffersHDR = (s_fg_flags & GT5_FG_HDR) != 0;
    op.depthInverted = (s_fg_flags & GT5_FG_DEPTH_INVERTED) != 0;
    op.cameraMotionIncluded = true;
    op.reset = fr->reset != 0;
    op.automodeOverrideReset = false;
    op.notRenderingGameFrames = fr->not_game != 0;
    op.orthoProjection = false;
    op.motionVectorsInvalidValue = 0.0f;
    op.motionVectorsDilated = false;
    op.menuDetectionEnabled = false;
    op.mvecsSubrectSize.Width = rw; op.mvecsSubrectSize.Height = rh;
    op.depthSubrectSize.Width = rw; op.depthSubrectSize.Height = rh;
    if (fr->hudless) {
        op.hudLessSubrectBase.X = fr->rect_x; op.hudLessSubrectBase.Y = fr->rect_y;
        op.hudLessSubrectSize.Width = gw; op.hudLessSubrectSize.Height = gh;
    }
    op.backbufferSubrectBase.X = fr->rect_x; op.backbufferSubrectBase.Y = fr->rect_y;
    op.backbufferSubrectSize.Width = gw; op.backbufferSubrectSize.Height = gh;
    op.outputInterpSubrectBase.X = fr->rect_x; op.outputInterpSubrectBase.Y = fr->rect_y;
    op.outputInterpSubrectSize.Width = gw; op.outputInterpSubrectSize.Height = gh;
    NVSDK_NGX_Parameter_SetULL(s_fg_params, NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID, fr->frame_id);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_EvalFlags, 0);   // outside the rectangle: this frame's own pixels
    for (unsigned k = 0; k < gen; k++) {
        if (!fr->output[k]) { snprintf(s_error, sizeof s_error, "no texture for made frame %u", k + 1u); return 0; }
        NVSDK_NGX_D3D12_DLSSG_Eval_Params ep = {};
        ep.pBackbuffer = fr->color;
        ep.pDepth = fr->depth;
        ep.pMVecs = fr->motion;
        ep.pHudless = fr->hudless;
        ep.pOutputInterpFrame = fr->output[k];
        op.multiFrameCount = gen;
        op.multiFrameIndex = k + 1u;
        const NVSDK_NGX_Result r = NGX_D3D12_EVALUATE_DLSSG(fr->list, s_fg_feature, s_fg_params, &ep, &op);
        if (NVSDK_NGX_FAILED(r)) { set_error("DLSS Frame Generation evaluate", r); return 0; }
    }
    return 1;
}

int dlss_fg_state(gt5_fg_state* st)
{
    if (!st) return 0;
    *st = s_fg;
    return s_fg_feature != nullptr;
}

void dlss_shutdown(ID3D12Device* device)
{
    dlss_fg_destroy();
    dlss_destroy();
    if (s_caps) { NVSDK_NGX_D3D12_DestroyParameters(s_caps); s_caps = nullptr; }
    if (s_inited) { NVSDK_NGX_D3D12_Shutdown1(device); s_inited = false; }
    s_device = nullptr;
}

const char* dlss_last_error() { return s_error; }

const gt5_upscale_api kApi = {
    GT5_UPSCALE_API_VERSION, dlss_init, dlss_optimal, dlss_create, dlss_evaluate, dlss_destroy, dlss_shutdown,
    dlss_last_error, dlss_fg_caps, dlss_fg_create, dlss_fg_evaluate, dlss_fg_state, dlss_fg_destroy,
};

}  // namespace

extern "C" __declspec(dllexport) const gt5_upscale_api* gt5_upscale_get(void) { return &kApi; }

#ifdef GT5_UPSCALE_WITH_VK
// ---- Vulkan: the same through NGX's Vulkan calls (gt5_upscale_vk.h) --------------------------------------------
// Built when the Vulkan headers are there (scripts\add_dlss.ps1: the Vulkan renderer's own, GT5\deps\vulkan).
// NGX's Vulkan path differs from its Direct3D 12 one in three ways that matter here:
//   - it needs instance and device extensions, which the renderer enables when it makes them: NGX names them
//     (NVSDK_NGX_VULKAN_GetFeature*ExtensionRequirements) for the upscaler and for frame generation;
//   - images are described by view + image + format + size; the renderer keeps them all in the GENERAL layout;
//   - Vulkan functions come from the renderer's vkGetInstanceProcAddr / vkGetDeviceProcAddr (nothing of Vulkan's
//     is linked: this DLL loads where no Vulkan driver is, for Direct3D 12).
#include <vulkan/vulkan.h>
#include "nvsdk_ngx_vk.h"
#include "nvsdk_ngx_helpers_vk.h"
#include "nvsdk_ngx_helpers_dlssg_vk.h"
#include "gt5_upscale_vk.h"

namespace vkdlss {

NVSDK_NGX_Parameter* s_caps;
NVSDK_NGX_Parameter* s_params;
NVSDK_NGX_Handle* s_feature;
NVSDK_NGX_Parameter* s_fg_params;
NVSDK_NGX_Handle* s_fg_feature;
gt5_fg_state s_fg;
unsigned s_fg_flags;
bool s_inited;
VkDevice s_device;
VkInstance s_instance;
VkPhysicalDevice s_phys;
PFN_vkGetInstanceProcAddr s_gipa;
char s_init_message[200];
unsigned s_missing;                      // extensions the driver lacked (from the renderer)

// The extension names NGX asks for, kept for the renderer (pointers stay valid while the DLL is loaded).
char s_ext_store[2][32][VK_MAX_EXTENSION_NAME_SIZE];
const char* s_ext_ptr[2][32];
unsigned s_ext_n[2];

void add_ext(int which, const char* name)
{
    if (!name || !name[0]) return;
    for (unsigned i = 0; i < s_ext_n[which]; i++) if (!strcmp(s_ext_store[which][i], name)) return;
    if (s_ext_n[which] >= 32) return;
    snprintf(s_ext_store[which][s_ext_n[which]], VK_MAX_EXTENSION_NAME_SIZE, "%s", name);
    s_ext_ptr[which][s_ext_n[which]] = s_ext_store[which][s_ext_n[which]];
    s_ext_n[which]++;
}

// The folder NGX may write its logs to when asked before init (the game's logs folder, as the loader uses).
const wchar_t* data_dir()
{
    static wchar_t dir[MAX_PATH + 16];
    if (!dir[0]) {
        wchar_t exe[MAX_PATH] = L"";
        const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wchar_t* cut = n && n < MAX_PATH ? wcsrchr(exe, L'\\') : nullptr;
        if (cut) { cut[1] = 0; _snwprintf(dir, MAX_PATH + 16, L"%ls..\\logs", exe); dir[MAX_PATH + 15] = 0; }
        else wcscpy(dir, L".");
    }
    return dir;
}

void discovery(NVSDK_NGX_FeatureDiscoveryInfo* d, NVSDK_NGX_Feature f, NVSDK_NGX_FeatureCommonInfo* info)
{
    memset(d, 0, sizeof *d);
    memset(info, 0, sizeof *info);
    find_dirs();
    info->PathListInfo.Path = s_dir_list; info->PathListInfo.Length = s_n_dirs;
    d->SDKVersion = NVSDK_NGX_Version_API;
    d->FeatureID = f;
    d->Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    d->Identifier.v.ProjectDesc.ProjectId = kProjectId;
    d->Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    d->Identifier.v.ProjectDesc.EngineVersion = kEngineVersion;
    d->ApplicationDataPath = data_dir();
    d->FeatureInfo = info;
}

int vk_instance_extensions(unsigned* count, const char* const** names)
{
    if (!count || !names) return 0;
    s_ext_n[0] = 0;
    static const NVSDK_NGX_Feature feats[2] = { NVSDK_NGX_Feature_SuperSampling, NVSDK_NGX_Feature_FrameGeneration };
    for (NVSDK_NGX_Feature f : feats) {
        NVSDK_NGX_FeatureDiscoveryInfo d; NVSDK_NGX_FeatureCommonInfo info;
        discovery(&d, f, &info);
        uint32_t n = 0; VkExtensionProperties* p = nullptr;
        if (NVSDK_NGX_SUCCEED(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&d, &n, &p)) && p)
            for (uint32_t i = 0; i < n; i++) add_ext(0, p[i].extensionName);
    }
    *count = s_ext_n[0]; *names = s_ext_ptr[0];
    return 1;
}

int vk_device_extensions(VkInstance instance, VkPhysicalDevice phys, PFN_vkGetInstanceProcAddr gipa, unsigned* count,
                         const char* const** names)
{
    (void)gipa;
    if (!count || !names) return 0;
    s_ext_n[1] = 0;
    static const NVSDK_NGX_Feature feats[2] = { NVSDK_NGX_Feature_SuperSampling, NVSDK_NGX_Feature_FrameGeneration };
    for (NVSDK_NGX_Feature f : feats) {
        NVSDK_NGX_FeatureDiscoveryInfo d; NVSDK_NGX_FeatureCommonInfo info;
        discovery(&d, f, &info);
        uint32_t n = 0; VkExtensionProperties* p = nullptr;
        if (NVSDK_NGX_SUCCEED(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, phys, &d, &n, &p)) && p)
            for (uint32_t i = 0; i < n; i++) add_ext(1, p[i].extensionName);
    }
    *count = s_ext_n[1]; *names = s_ext_ptr[1];
    return 1;
}

int vk_init(const gt5_vk_device* dev, const wchar_t* dir, gt5_upscale_caps* caps)
{
    memset(caps, 0, sizeof *caps);
    snprintf(caps->name, sizeof caps->name, "NVIDIA DLSS");
    s_error[0] = 0;
    if (!dev || !dev->device || !dev->get_instance_proc_addr) {
        snprintf(caps->message, sizeof caps->message, "no Vulkan device");
        return 0;
    }
    s_missing = dev->missing_extensions;
    if (!s_inited) {
        find_dirs();
        static NVSDK_NGX_FeatureCommonInfo info;
        memset(&info, 0, sizeof info);
        info.PathListInfo.Path = s_dir_list; info.PathListInfo.Length = s_n_dirs;
        const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, dir && *dir ? dir : L".", dev->instance,
            dev->physical_device, dev->device, dev->get_instance_proc_addr, dev->get_device_proc_addr, &info,
            NVSDK_NGX_Version_API);
        caps->init_result = (int)r;
        if (NVSDK_NGX_FAILED(r)) {
            set_error("NGX could not start on Vulkan", r);
            snprintf(caps->message, sizeof caps->message, "%s",
                     r == NVSDK_NGX_Result_FAIL_FeatureNotSupported ? "this graphics card does not support DLSS"
                     : r == NVSDK_NGX_Result_FAIL_OutOfDate ? "the NVIDIA graphics driver is too old for DLSS"
                     : s_missing ? "the graphics driver lacks what DLSS needs on Vulkan"
                     : s_error);
            snprintf(s_init_message, sizeof s_init_message, "%s", caps->message);
            return 1;
        }
        s_inited = true;
        s_device = dev->device; s_instance = dev->instance; s_phys = dev->physical_device;
        s_gipa = dev->get_instance_proc_addr;
    }
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetCapabilityParameters(&s_caps);
    if (NVSDK_NGX_FAILED(r) || !s_caps) {
        set_error("NGX capability parameters", r);
        snprintf(caps->message, sizeof caps->message, "%s", s_error);
        snprintf(s_init_message, sizeof s_init_message, "%s", caps->message);
        return 1;
    }
    int avail = 0, need = 0, init_result = 0;
    unsigned major = 0, minor = 0;
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_SuperSampling_Available, &avail);
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &need);
    NVSDK_NGX_Parameter_GetUI(s_caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &major);
    NVSDK_NGX_Parameter_GetUI(s_caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minor);
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &init_result);
    caps->available = avail != 0;
    caps->needs_driver = need != 0;
    caps->min_driver_major = major; caps->min_driver_minor = minor;
    caps->init_result = init_result;
    if (!avail) {
        if (need)
            snprintf(caps->message, sizeof caps->message, "DLSS needs NVIDIA driver %u.%u or newer", major, minor);
        else {
            set_error("DLSS is not available here", (NVSDK_NGX_Result)init_result);
            snprintf(caps->message, sizeof caps->message, "%s", s_error);
        }
    }
    return 1;
}

int vk_optimal(unsigned out_w, unsigned out_h, gt5_upscale_mode mode, unsigned* render_w, unsigned* render_h)
{
    if (!s_caps || !render_w || !render_h) return 0;
    unsigned ow = 0, oh = 0, maxw = 0, maxh = 0, minw = 0, minh = 0;
    float sharp = 0.0f;
    const NVSDK_NGX_Result r = NGX_DLSS_GET_OPTIMAL_SETTINGS(s_caps, out_w, out_h, quality_of(mode), &ow, &oh, &maxw,
                                                            &maxh, &minw, &minh, &sharp);
    if (NVSDK_NGX_FAILED(r) || !ow || !oh) { set_error("DLSS optimal settings", r); return 0; }
    *render_w = ow; *render_h = oh;
    return 1;
}

void vk_destroy()
{
    if (s_feature) { NVSDK_NGX_VULKAN_ReleaseFeature(s_feature); s_feature = nullptr; }
    if (s_params) { NVSDK_NGX_VULKAN_DestroyParameters(s_params); s_params = nullptr; }
}

int vk_create(VkCommandBuffer cmd, unsigned render_w, unsigned render_h, unsigned out_w, unsigned out_h,
              gt5_upscale_mode mode, unsigned flags, int preset)
{
    if (!s_inited || !cmd) return 0;
    vk_destroy();
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_AllocateParameters(&s_params);
    if (NVSDK_NGX_FAILED(r) || !s_params) { set_error("NGX parameters", r); return 0; }
    if (preset > 0) {
        NVSDK_NGX_Parameter_SetUI(s_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, (unsigned)preset);
        NVSDK_NGX_Parameter_SetUI(s_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, (unsigned)preset);
        NVSDK_NGX_Parameter_SetUI(s_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, (unsigned)preset);
        NVSDK_NGX_Parameter_SetUI(s_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, (unsigned)preset);
        NVSDK_NGX_Parameter_SetUI(s_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, (unsigned)preset);
    }
    NVSDK_NGX_DLSS_Create_Params cp;
    memset(&cp, 0, sizeof cp);
    cp.Feature.InWidth = render_w; cp.Feature.InHeight = render_h;
    cp.Feature.InTargetWidth = out_w; cp.Feature.InTargetHeight = out_h;
    cp.Feature.InPerfQualityValue = quality_of(mode);
    int f = NVSDK_NGX_DLSS_Feature_Flags_None;
    if (flags & GT5_UPSCALE_HDR) f |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if (!(flags & GT5_UPSCALE_MV_OUTPUT_SIZE)) f |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if (flags & GT5_UPSCALE_MV_JITTERED) f |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
    if (flags & GT5_UPSCALE_DEPTH_INVERTED) f |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    if (flags & GT5_UPSCALE_AUTO_EXPOSURE) f |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    cp.InFeatureCreateFlags = f;
    r = NGX_VULKAN_CREATE_DLSS_EXT1(s_device, cmd, 1, 1, &s_feature, s_params, &cp);
    if (NVSDK_NGX_FAILED(r) || !s_feature) {
        set_error("DLSS could not be made for these sizes", r);
        vk_destroy();
        return 0;
    }
    return 1;
}

NVSDK_NGX_Resource_VK res_of(const gt5_vk_image& im, bool write)
{
    const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    return NVSDK_NGX_Create_ImageView_Resource_VK(im.view, im.image, range, im.format, im.w, im.h, write);
}

int vk_evaluate(const gt5_upscale_vk_frame* fr)
{
    if (!s_feature || !s_params || !fr || !fr->cmd) return 0;
    if (!fr->color.view || !fr->depth.view || !fr->motion.view || !fr->output.view) {
        snprintf(s_error, sizeof s_error, "DLSS was given a frame without a picture, depth, vectors or an output image");
        return 0;
    }
    NVSDK_NGX_Resource_VK color = res_of(fr->color, false), depth = res_of(fr->depth, false);
    NVSDK_NGX_Resource_VK motion = res_of(fr->motion, false), output = res_of(fr->output, true);
    NVSDK_NGX_VK_DLSS_Eval_Params ep;
    memset(&ep, 0, sizeof ep);
    ep.Feature.pInColor = &color;
    ep.Feature.pInOutput = &output;
    ep.pInDepth = &depth;
    ep.pInMotionVectors = &motion;
    ep.InJitterOffsetX = fr->jitter_x;
    ep.InJitterOffsetY = fr->jitter_y;
    ep.InRenderSubrectDimensions.Width = fr->render_w;
    ep.InRenderSubrectDimensions.Height = fr->render_h;
    ep.InReset = fr->reset ? 1 : 0;
    ep.InMVScaleX = fr->mv_scale_x == 0.0f ? 1.0f : fr->mv_scale_x;
    ep.InMVScaleY = fr->mv_scale_y == 0.0f ? 1.0f : fr->mv_scale_y;
    ep.InFrameTimeDeltaInMsec = fr->frame_ms;
    const NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSS_EXT(fr->cmd, s_feature, s_params, &ep);
    if (NVSDK_NGX_FAILED(r)) { set_error("DLSS evaluate", r); return 0; }
    return 1;
}

// ---- frame generation on Vulkan ----

// Hardware-accelerated GPU scheduling, for this card (the LUID Vulkan 1.1 gives): -1 when it cannot be told.
int vk_gpu_scheduling_on()
{
    if (!s_gipa || !s_instance || !s_phys) return -1;
    PFN_vkGetPhysicalDeviceProperties2 gp2 = (PFN_vkGetPhysicalDeviceProperties2)s_gipa(s_instance, "vkGetPhysicalDeviceProperties2");
    if (!gp2) gp2 = (PFN_vkGetPhysicalDeviceProperties2)s_gipa(s_instance, "vkGetPhysicalDeviceProperties2KHR");
    if (!gp2) return -1;
    VkPhysicalDeviceIDProperties idp = {};
    idp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 p2 = {};
    p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; p2.pNext = &idp;
    gp2(s_phys, &p2);
    if (!idp.deviceLUIDValid) return -1;
    D3DKMT_OPENADAPTERFROMLUID oa = {};
    memcpy(&oa.AdapterLuid, idp.deviceLUID, sizeof oa.AdapterLuid);
    if (D3DKMTOpenAdapterFromLuid(&oa) != 0) return -1;
    D3DKMT_WDDM_2_7_CAPS wc = {};
    D3DKMT_QUERYADAPTERINFO qa = {};
    qa.hAdapter = oa.hAdapter; qa.Type = KMTQAITYPE_WDDM_2_7_CAPS; qa.pPrivateDriverData = &wc; qa.PrivateDriverDataSize = sizeof wc;
    const NTSTATUS ks = D3DKMTQueryAdapterInfo(&qa);
    D3DKMT_CLOSEADAPTER ca = {}; ca.hAdapter = oa.hAdapter;
    D3DKMTCloseAdapter(&ca);
    return ks == 0 ? (wc.HwSchEnabled ? 1 : 0) : -1;
}

int vk_fg_caps(gt5_fg_caps* caps)
{
    if (!caps) return 0;
    memset(caps, 0, sizeof *caps);
    snprintf(caps->name, sizeof caps->name, "NVIDIA DLSS Frame Generation");
    caps->wants_camera = 1;
    caps->max_generated = 1;
    if (!s_inited || !s_caps) {
        snprintf(caps->message, sizeof caps->message, "%s", s_init_message[0] ? s_init_message : "DLSS did not start");
        return 1;
    }
    if (!find_ngx_file(L"nvngx_dlssg.dll", caps->version, sizeof caps->version)) {
        snprintf(caps->message, sizeof caps->message, "nvngx_dlssg.dll is not installed: run \"Get NVIDIA DLSS\" in the launcher again");
        return 1;
    }
    int avail = 0, need = 0, init_result = 0;
    unsigned major = 0, minor = 0, multi = 0;
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_FrameGeneration_Available, &avail);
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_FrameGeneration_NeedsUpdatedDriver, &need);
    NVSDK_NGX_Parameter_GetUI(s_caps, NVSDK_NGX_Parameter_FrameGeneration_MinDriverVersionMajor, &major);
    NVSDK_NGX_Parameter_GetUI(s_caps, NVSDK_NGX_Parameter_FrameGeneration_MinDriverVersionMinor, &minor);
    NVSDK_NGX_Parameter_GetI(s_caps, NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult, &init_result);
    if (NVSDK_NGX_SUCCEED(NVSDK_NGX_Parameter_GetUI(s_caps, NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &multi)) && multi > 1)
        caps->max_generated = multi > 3 ? 3 : multi;
    caps->available = avail != 0;
    caps->needs_driver = need != 0;
    caps->min_driver_major = major; caps->min_driver_minor = minor;
    caps->init_result = init_result;
    if (!avail) {
        if (need)
            snprintf(caps->message, sizeof caps->message, "DLSS Frame Generation needs NVIDIA driver %u.%u or newer", major, minor);
        else if ((NVSDK_NGX_Result)init_result == NVSDK_NGX_Result_FAIL_FeatureNotSupported)
            snprintf(caps->message, sizeof caps->message, "this graphics card does not support DLSS Frame Generation (it needs an RTX 40 series card or newer)");
        else if (vk_gpu_scheduling_on() == 0)
            snprintf(caps->message, sizeof caps->message, "turn on Hardware-accelerated GPU scheduling in Windows' graphics settings, then restart the PC");
        else {
            set_error("DLSS Frame Generation is not available here", (NVSDK_NGX_Result)init_result);
            snprintf(caps->message, sizeof caps->message, "%s", s_error);
        }
    }
    return 1;
}

void vk_fg_destroy()
{
    if (s_fg_feature) { NVSDK_NGX_VULKAN_ReleaseFeature(s_fg_feature); s_fg_feature = nullptr; }
    if (s_fg_params) { NVSDK_NGX_VULKAN_DestroyParameters(s_fg_params); s_fg_params = nullptr; }
    memset(&s_fg, 0, sizeof s_fg);
}

int vk_fg_create(VkCommandBuffer cmd, unsigned out_w, unsigned out_h, unsigned rect_w, unsigned rect_h,
                 unsigned render_w, unsigned render_h, unsigned max_generated, unsigned flags)
{
    if (!s_inited || !s_caps || !cmd || !out_w || !out_h || !render_w || !render_h) return 0;
    if (!rect_w || !rect_h || rect_w > out_w || rect_h > out_h) { rect_w = out_w; rect_h = out_h; }
    vk_fg_destroy();
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_AllocateParameters(&s_fg_params);
    if (NVSDK_NGX_FAILED(r) || !s_fg_params) { set_error("NGX parameters", r); return 0; }
    const unsigned always = NVSDK_NGX_DLSSG_ResourceFlags_Backbuffer | NVSDK_NGX_DLSSG_ResourceFlags_MVecs |
                            NVSDK_NGX_DLSSG_ResourceFlags_Depth | NVSDK_NGX_DLSSG_ResourceFlags_OutputInterpolated;
    const unsigned never = NVSDK_NGX_DLSSG_ResourceFlags_UI | NVSDK_NGX_DLSSG_ResourceFlags_UIAlpha |
                           NVSDK_NGX_DLSSG_ResourceFlags_BidirectionalDistortionField | NVSDK_NGX_DLSSG_ResourceFlags_OutputReal |
                           NVSDK_NGX_DLSSG_ResourceFlags_OutputDisableInterpolation;
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_ResourceAlwaysProvided_Flags, always);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_ResourceNeverProvided_Flags, never);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_FullscreenMode, NVSDK_NGX_DLSSG_FullscreenMode_Windowed);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_AsyncCreateEnabled, 0);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_UserInterfaceRecompositionEnabled, 0);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_Width, rect_w);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_Height, rect_h);
    NVSDK_NGX_DLSSG_Create_Params cp = {};
    cp.Width = rect_w; cp.Height = rect_h;
    cp.NativeBackbufferFormat = (unsigned)VK_FORMAT_R8G8B8A8_UNORM;
    cp.RenderWidth = render_w; cp.RenderHeight = render_h;
    cp.DynamicResolutionScaling = false;
    r = NGX_VK_CREATE_DLSSG(cmd, 1, 1, &s_fg_feature, s_fg_params, &cp);
    if (NVSDK_NGX_FAILED(r) || !s_fg_feature) {
        set_error("DLSS Frame Generation could not be made for these sizes", r);
        vk_fg_destroy();
        return 0;
    }
    s_fg.out_w = out_w; s_fg.out_h = out_h; s_fg.rect_w = rect_w; s_fg.rect_h = rect_h;
    s_fg.render_w = render_w; s_fg.render_h = render_h;
    s_fg.max_generated = max_generated < 1 ? 1 : max_generated > 3 ? 3 : max_generated;
    s_fg_flags = flags;
    size_t est = 0;
    if (NVSDK_NGX_SUCCEED(NGX_VK_ESTIMATE_VRAM_DLSSG(s_caps, render_w, render_h, rect_w, rect_h, VK_FORMAT_R8G8B8A8_UNORM,
                                                     VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_UNDEFINED,
                                                     VK_FORMAT_UNDEFINED, &est)))
        s_fg.memory_mb = (unsigned)((est + 524288u) / 1048576u);
    return 1;
}

int vk_fg_evaluate(const gt5_fg_vk_frame* fr)
{
    if (!s_fg_feature || !s_fg_params) { snprintf(s_error, sizeof s_error, "DLSS Frame Generation was not made"); return 0; }
    if (!fr || !fr->cmd || !fr->color.view || !fr->depth.view || !fr->motion.view || !fr->output[0].view) {
        snprintf(s_error, sizeof s_error, "DLSS Frame Generation was given a frame without a picture, depth, vectors or an output image");
        return 0;
    }
    const unsigned gen = fr->generated < 1 ? 1 : fr->generated > s_fg.max_generated ? s_fg.max_generated : fr->generated;
    const unsigned rw = fr->render_w ? fr->render_w : s_fg.render_w, rh = fr->render_h ? fr->render_h : s_fg.render_h;
    const unsigned gw = fr->rect_w ? fr->rect_w : s_fg.out_w, gh = fr->rect_h ? fr->rect_h : s_fg.out_h;
    if (gw != s_fg.rect_w || gh != s_fg.rect_h) {
        snprintf(s_error, sizeof s_error, "DLSS Frame Generation was made for a %ux%u picture and is given %ux%u", s_fg.rect_w, s_fg.rect_h, gw, gh);
        return 0;
    }
    const gt5_fg_camera& cam = fr->camera;
    NVSDK_NGX_DLSSG_Opt_Eval_Params op = {};
    const float aspect = cam.cam_aspect > 0.0f ? cam.cam_aspect : (float)rw / (float)rh;
    const float fov = cam.cam_fov_y > 0.01f && cam.cam_fov_y < 3.0f ? cam.cam_fov_y : 0.9f;
    const float zn = cam.cam_near > 0.0f ? cam.cam_near : 0.1f;
    const float zf = cam.cam_far > zn ? cam.cam_far : zn * 10000.0f;
    if (cam.cam_ok && mat_given(cam.view_to_clip) && mat_given(cam.clip_to_view)) {
        memcpy(op.cameraViewToClip, cam.view_to_clip, sizeof op.cameraViewToClip);
        memcpy(op.clipToCameraView, cam.clip_to_view, sizeof op.clipToCameraView);
    } else {
        const float ys = 1.0f / tanf(fov * 0.5f), xs = ys / aspect, zq = zf / (zf - zn);
        memset(op.cameraViewToClip, 0, sizeof op.cameraViewToClip);
        op.cameraViewToClip[0][0] = xs; op.cameraViewToClip[1][1] = ys; op.cameraViewToClip[2][2] = zq;
        op.cameraViewToClip[2][3] = 1.0f; op.cameraViewToClip[3][2] = -zn * zq;
        memset(op.clipToCameraView, 0, sizeof op.clipToCameraView);
        op.clipToCameraView[0][0] = 1.0f / xs; op.clipToCameraView[1][1] = 1.0f / ys; op.clipToCameraView[2][3] = -1.0f / (zn * zq);
        op.clipToCameraView[3][2] = 1.0f; op.clipToCameraView[3][3] = 1.0f / zn;
    }
    mat_identity(op.clipToLensClip);
    if (cam.cam_ok && !fr->reset && mat_given(cam.clip_to_prev_clip) && mat_given(cam.prev_clip_to_clip)) {
        memcpy(op.clipToPrevClip, cam.clip_to_prev_clip, sizeof op.clipToPrevClip);
        memcpy(op.prevClipToClip, cam.prev_clip_to_clip, sizeof op.prevClipToClip);
    } else {
        mat_identity(op.clipToPrevClip); mat_identity(op.prevClipToClip);
    }
    op.jitterOffset[0] = 2.0f * fr->jitter_x / (float)rw; op.jitterOffset[1] = -2.0f * fr->jitter_y / (float)rh;
    op.mvecScale[0] = fr->mv_scale_x == 0.0f ? 1.0f : fr->mv_scale_x;
    op.mvecScale[1] = fr->mv_scale_y == 0.0f ? 1.0f : fr->mv_scale_y;
    if (cam.cam_ok) {
        memcpy(op.cameraPos, cam.cam_pos, sizeof op.cameraPos);
        memcpy(op.cameraUp, cam.cam_up, sizeof op.cameraUp);
        memcpy(op.cameraRight, cam.cam_right, sizeof op.cameraRight);
        memcpy(op.cameraFwd, cam.cam_fwd, sizeof op.cameraFwd);
    } else {
        op.cameraUp[1] = 1.0f; op.cameraRight[0] = 1.0f; op.cameraFwd[2] = 1.0f;
    }
    op.cameraNear = zn; op.cameraFar = zf; op.cameraFOV = fov; op.cameraAspectRatio = aspect;
    op.colorBuffersHDR = (s_fg_flags & GT5_FG_HDR) != 0;
    op.depthInverted = (s_fg_flags & GT5_FG_DEPTH_INVERTED) != 0;
    op.cameraMotionIncluded = true;
    op.reset = fr->reset != 0;
    op.notRenderingGameFrames = fr->not_game != 0;
    op.mvecsSubrectSize.Width = rw; op.mvecsSubrectSize.Height = rh;
    op.depthSubrectSize.Width = rw; op.depthSubrectSize.Height = rh;
    if (fr->hudless.view) {
        op.hudLessSubrectBase.X = fr->rect_x; op.hudLessSubrectBase.Y = fr->rect_y;
        op.hudLessSubrectSize.Width = gw; op.hudLessSubrectSize.Height = gh;
    }
    op.backbufferSubrectBase.X = fr->rect_x; op.backbufferSubrectBase.Y = fr->rect_y;
    op.backbufferSubrectSize.Width = gw; op.backbufferSubrectSize.Height = gh;
    op.outputInterpSubrectBase.X = fr->rect_x; op.outputInterpSubrectBase.Y = fr->rect_y;
    op.outputInterpSubrectSize.Width = gw; op.outputInterpSubrectSize.Height = gh;
    NVSDK_NGX_Parameter_SetULL(s_fg_params, NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID, fr->frame_id);
    NVSDK_NGX_Parameter_SetUI(s_fg_params, NVSDK_NGX_DLSSG_Parameter_EvalFlags, 0);
    NVSDK_NGX_Resource_VK color = res_of(fr->color, false), depth = res_of(fr->depth, false), motion = res_of(fr->motion, false);
    NVSDK_NGX_Resource_VK hudless = fr->hudless.view ? res_of(fr->hudless, false) : NVSDK_NGX_Resource_VK{};
    for (unsigned k = 0; k < gen; k++) {
        if (!fr->output[k].view) { snprintf(s_error, sizeof s_error, "no image for made frame %u", k + 1u); return 0; }
        NVSDK_NGX_Resource_VK out = res_of(fr->output[k], true);
        NVSDK_NGX_VK_DLSSG_Eval_Params ep = {};
        ep.pBackbuffer = &color; ep.pDepth = &depth; ep.pMVecs = &motion;
        ep.pHudless = fr->hudless.view ? &hudless : nullptr;
        ep.pOutputInterpFrame = &out;
        op.multiFrameCount = gen;
        op.multiFrameIndex = k + 1u;
        const NVSDK_NGX_Result r = NGX_VK_EVALUATE_DLSSG(fr->cmd, s_fg_feature, s_fg_params, &ep, &op);
        if (NVSDK_NGX_FAILED(r)) { set_error("DLSS Frame Generation evaluate", r); return 0; }
    }
    return 1;
}

int vk_fg_state(gt5_fg_state* st)
{
    if (!st) return 0;
    *st = s_fg;
    return s_fg_feature != nullptr;
}

void vk_shutdown()
{
    vk_fg_destroy();
    vk_destroy();
    if (s_caps) { NVSDK_NGX_VULKAN_DestroyParameters(s_caps); s_caps = nullptr; }
    if (s_inited) { NVSDK_NGX_VULKAN_Shutdown1(s_device); s_inited = false; }
    s_device = VK_NULL_HANDLE; s_instance = VK_NULL_HANDLE; s_phys = VK_NULL_HANDLE; s_gipa = nullptr;
}

const gt5_upscale_vk_api kVkApi = {
    GT5_UPSCALE_VK_API_VERSION, vk_instance_extensions, vk_device_extensions, vk_init, vk_optimal, vk_create,
    vk_evaluate, vk_destroy, vk_shutdown, dlss_last_error, vk_fg_caps, vk_fg_create, vk_fg_evaluate, vk_fg_state,
    vk_fg_destroy,
};

}  // namespace vkdlss

extern "C" __declspec(dllexport) const gt5_upscale_vk_api* gt5_upscale_get_vk(void) { return &vkdlss::kVkApi; }
#endif  /* GT5_UPSCALE_WITH_VK */
