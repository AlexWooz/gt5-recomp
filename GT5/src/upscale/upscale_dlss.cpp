// upscale_dlss.cpp - gt5_upscale_dlss.dll: NVIDIA DLSS (Super Resolution and DLAA) behind gt5_upscale.h.
//
// This file is the project's own; what it includes and links (nvsdk_ngx*.h, nvsdk_ngx_d.lib) is NVIDIA's
// DLSS SDK, which is NOT part of the project: scripts\add_dlss.ps1 fetches it from NVIDIA's repository
// on the player's PC, after showing NVIDIA's licence, and builds this DLL there. nvngx_dlss.dll (NVIDIA's,
// signed) sits next to it. The game loads gt5_upscale_dlss.dll if it is there and never needs it.
//
// NGX rules kept here: one thread (the renderer's), LDR colour by leaving IsHDR off, vectors at render
// size in pixels, no sharpening (deprecated in DLSS), feature released by destroy() only.
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <d3d12.h>

#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"

#include "gt5_upscale.h"

namespace {

// The project's own identifier for NGX (Init_with_ProjectID wants a GUID-like string; made once, fixed).
const char kProjectId[] = "6f2d9c41-3b7e-4c58-9a10-5e8f27d4b3a6";
const char kEngineVersion[] = "1.0";

NVSDK_NGX_Parameter* s_caps;        // capability parameters (NGX owns their lifetime until destroyed)
NVSDK_NGX_Parameter* s_params;      // the feature's own parameters
NVSDK_NGX_Handle* s_feature;
bool s_inited;
char s_error[240];

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

int dlss_init(ID3D12Device* device, const wchar_t* dir, gt5_upscale_caps* caps)
{
    memset(caps, 0, sizeof *caps);
    snprintf(caps->name, sizeof caps->name, "NVIDIA DLSS");
    s_error[0] = 0;
    if (!device) { snprintf(caps->message, sizeof caps->message, "no Direct3D 12 device"); return 0; }
    if (!s_inited) {
        const NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, dir && *dir ? dir : L".", device, nullptr,
            NVSDK_NGX_Version_API);
        caps->init_result = (int)r;
        if (NVSDK_NGX_FAILED(r)) {
            set_error("NGX could not start", r);
            snprintf(caps->message, sizeof caps->message, "%s",
                     r == NVSDK_NGX_Result_FAIL_FeatureNotSupported ? "this graphics card does not support DLSS"
                     : r == NVSDK_NGX_Result_FAIL_OutOfDate ? "the NVIDIA graphics driver is too old for DLSS"
                     : s_error);
            return 1;   // answered: not available
        }
        s_inited = true;
    }
    NVSDK_NGX_Result r = NVSDK_NGX_D3D12_GetCapabilityParameters(&s_caps);
    if (NVSDK_NGX_FAILED(r) || !s_caps) {
        set_error("NGX capability parameters", r);
        snprintf(caps->message, sizeof caps->message, "%s", s_error);
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

void dlss_shutdown(ID3D12Device* device)
{
    dlss_destroy();
    if (s_caps) { NVSDK_NGX_D3D12_DestroyParameters(s_caps); s_caps = nullptr; }
    if (s_inited) { NVSDK_NGX_D3D12_Shutdown1(device); s_inited = false; }
}

const char* dlss_last_error() { return s_error; }

const gt5_upscale_api kApi = {
    GT5_UPSCALE_API_VERSION, dlss_init, dlss_optimal, dlss_create, dlss_evaluate, dlss_destroy, dlss_shutdown,
    dlss_last_error,
};

}  // namespace

extern "C" __declspec(dllexport) const gt5_upscale_api* gt5_upscale_get(void) { return &kApi; }
