// upscale_fsr.cpp - gt5_upscale_fsr.dll: AMD FSR 3 frame generation behind gt5_upscale.h (the fg_ calls of
// interface version 2). No upscaler in this plug-in yet: its upscaler calls are NULL.
//
// This file is the project's own; what it includes (ffx_api.h, ffx_api_types.h, dx12/ffx_api_dx12.h,
// ffx_framegeneration.h: AMD's, MIT licence) and loads at run time (amd_fidelityfx_loader_dx12.dll and
// amd_fidelityfx_framegeneration_dx12.dll: AMD's, signed) is AMD's FSR SDK, which is NOT part of the
// project: scripts\add_fsr.ps1 fetches it from AMD's repository on the player's PC and builds this DLL
// there. AMD's two DLLs sit next to it. The game loads gt5_upscale_fsr.dll if it is there and never
// needs it. Nothing of AMD's is linked: the five ffx functions are taken from the loader DLL by name.
//
// The generator is driven WITHOUT AMD's swap chain: ffxConfigure with no swap chain and
// FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY, then the prepare and the generation dispatch on
// the renderer's own list, the made frame into the renderer's own texture. What a stand-alone test
// (fsrfgtest) found and this file relies on: vectors this frame -> the one before, in pixels with a
// scale of 1; depth 0 near .. 1 far with no depth flag; a reset must be announced (FSR morphs across
// an unannounced cut); one made frame, half way, whatever number is asked for.
//
// Which generator: AMD's DLL holds the analytic 3.1 one (any card) and a learned one (Radeon RX 9000),
// and by default picks the newest the card can run. This plug-in pins 3.1: the learned one could not
// be tried where this was written, wants a newer Direct3D runtime than the game brings, and insists
// on a camera position and axes that are exact. GT5_FSR_FG_VERSION=auto lets AMD's DLL choose;
// GT5_FSR_FG_VERSION=<text> pins the version whose name starts with that text.
//
// One thread (the renderer's): AMD's context is not thread safe.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include <d3d12.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>

#include "ffx_api.h"
#include "dx12/ffx_api_dx12.h"
#include "ffx_framegeneration.h"

#include "gt5_upscale.h"

namespace {

PfnFfxCreateContext fCreate;
PfnFfxDestroyContext fDestroy;
PfnFfxConfigure fConfigure;
PfnFfxQuery fQuery;
PfnFfxDispatch fDispatch;

HMODULE s_loader;
ID3D12Device* s_device;
bool s_loaded;                 // AMD's DLL is in and offers a frame generator for this card
char s_error[240];
char s_why[200];               // why it is not available, for fg_caps
char s_version[48];            // the generator in use, as AMD names it
uint64_t s_version_id;         // its id, when it is pinned
bool s_pinned;
bool s_debug;

// The descriptions given to ffxCreateContext must stay alive until the context is destroyed.
ffxCreateBackendDX12Desc s_be;
ffxCreateContextDescFrameGenerationVersion s_ver;
ffxOverrideVersion s_ovr;
ffxCreateContextDescFrameGeneration s_cd;
ffxContext s_ctx;
gt5_fg_state s_fg;

const char* rc_name(ffxReturnCode_t r)
{
    switch (r) {
    case FFX_API_RETURN_OK: return "ok";
    case FFX_API_RETURN_ERROR: return "error";
    case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "unknown description type";
    case FFX_API_RETURN_ERROR_RUNTIME_ERROR: return "Direct3D or the effect returned an error";
    case FFX_API_RETURN_NO_PROVIDER: return "no provider";
    case FFX_API_RETURN_ERROR_MEMORY: return "out of memory";
    case FFX_API_RETURN_ERROR_PARAMETER: return "invalid parameter";
    case FFX_API_RETURN_PROVIDER_NO_SUPPORT_NEW_DESCTYPE: return "the provider is too old for this description";
    }
    return "?";
}
void set_error(const char* what, ffxReturnCode_t r) { snprintf(s_error, sizeof s_error, "%s: %u (%s)", what, (unsigned)r, rc_name(r)); }

void ffx_message(uint32_t type, const wchar_t* message)
{
    fprintf(stderr, "[fsr] %s: %ls\n", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", message);
}

// AMD's DLL must be AMD's: Authenticode valid and the signer's name AMD's.
bool signed_by_amd(const wchar_t* path)
{
    WINTRUST_FILE_INFO fi = {};
    fi.cbStruct = sizeof fi; fi.pcwszFilePath = path;
    WINTRUST_DATA wd = {};
    wd.cbStruct = sizeof wd; wd.dwUIChoice = WTD_UI_NONE; wd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wd.dwUnionChoice = WTD_CHOICE_FILE; wd.pFile = &fi; wd.dwStateAction = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID pol = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    bool ok = false;
    if (WinVerifyTrust(nullptr, &pol, &wd) == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA* pd = WTHelperProvDataFromStateData(wd.hWVTStateData);
        CRYPT_PROVIDER_SGNR* sg = pd ? WTHelperGetProvSignerFromChain(pd, 0, FALSE, 0) : nullptr;
        CRYPT_PROVIDER_CERT* c = sg ? WTHelperGetProvCertFromChain(sg, 0) : nullptr;
        wchar_t name[256] = L"";
        if (c && c->pCert) CertGetNameStringW(c->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
        ok = wcsstr(name, L"Advanced Micro Devices") != nullptr;
    }
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &pol, &wd);
    return ok;
}

// The folder AMD's two DLLs are in: this DLL's own, or GT5_FSR_DIR (tests).
bool find_dir(wchar_t* out)
{
    static const wchar_t* need[2] = { L"amd_fidelityfx_loader_dx12.dll", L"amd_fidelityfx_framegeneration_dx12.dll" };
    wchar_t dirs[2][MAX_PATH] = { L"", L"" };
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void*)&find_dir, &self) && self) {
        const DWORD n = GetModuleFileNameW(self, dirs[0], MAX_PATH);
        wchar_t* cut = n && n < MAX_PATH ? wcsrchr(dirs[0], L'\\') : nullptr;
        if (cut) *cut = 0; else dirs[0][0] = 0;
    }
    const DWORD e = GetEnvironmentVariableW(L"GT5_FSR_DIR", dirs[1], MAX_PATH);
    if (!e || e >= MAX_PATH) dirs[1][0] = 0;
    for (int d = 1; d >= 0; d--) {          // the test's folder first, when it is given
        if (!dirs[d][0]) continue;
        bool all = true;
        for (const wchar_t* f : need) {
            wchar_t path[MAX_PATH + 64];
            _snwprintf(path, MAX_PATH + 64, L"%ls\\%ls", dirs[d], f);
            path[MAX_PATH + 63] = 0;
            if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) all = false;
        }
        if (all) { wcscpy(out, dirs[d]); return true; }
    }
    return false;
}

int fsr_init(ID3D12Device* device, const wchar_t* dir, gt5_upscale_caps* caps)
{
    (void)dir;
    memset(caps, 0, sizeof *caps);
    snprintf(caps->name, sizeof caps->name, "AMD FSR 3");
    snprintf(caps->message, sizeof caps->message, "this plug-in makes frames only; it has no upscaler yet");
    s_error[0] = 0; s_why[0] = 0;
    if (!device) { snprintf(s_why, sizeof s_why, "no Direct3D 12 device"); return 0; }
    s_device = device;              // (again after a shutdown: AMD's DLLs stay loaded, the device may be new)
    if (s_loaded) return 1;
    { char e[8] = ""; s_debug = GetEnvironmentVariableA("GT5_FSR_DEBUG", e, sizeof e) && e[0] && e[0] != '0'; }

    wchar_t where[MAX_PATH], path[MAX_PATH + 64];
    if (!find_dir(where)) {
        snprintf(s_why, sizeof s_why, "AMD's FSR files are not installed: run \"Get AMD FSR 3\" in the launcher");
        return 1;
    }
    static const wchar_t* files[2] = { L"amd_fidelityfx_framegeneration_dx12.dll", L"amd_fidelityfx_loader_dx12.dll" };
    for (const wchar_t* f : files) {
        _snwprintf(path, MAX_PATH + 64, L"%ls\\%ls", where, f);
        path[MAX_PATH + 63] = 0;
        if (!signed_by_amd(path)) {
            snprintf(s_why, sizeof s_why, "%ls is not signed by AMD: run \"Get AMD FSR 3\" in the launcher again", f);
            return 1;
        }
    }
    // the loader (the last one checked); it finds the effect DLL in the game's folder or in its own
    s_loader = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!s_loader) {
        snprintf(s_why, sizeof s_why, "AMD's FSR loader could not be loaded (Windows error %lu)", GetLastError());
        return 1;
    }
    fCreate = (PfnFfxCreateContext)(void*)GetProcAddress(s_loader, "ffxCreateContext");
    fDestroy = (PfnFfxDestroyContext)(void*)GetProcAddress(s_loader, "ffxDestroyContext");
    fConfigure = (PfnFfxConfigure)(void*)GetProcAddress(s_loader, "ffxConfigure");
    fQuery = (PfnFfxQuery)(void*)GetProcAddress(s_loader, "ffxQuery");
    fDispatch = (PfnFfxDispatch)(void*)GetProcAddress(s_loader, "ffxDispatch");
    if (!fCreate || !fDestroy || !fConfigure || !fQuery || !fDispatch) {
        snprintf(s_why, sizeof s_why, "AMD's FSR loader is not the expected one (its functions are missing)");
        return 1;
    }
    // FSR 3 needs typed loads from unordered-access textures of more formats than every card has.
    D3D12_FEATURE_DATA_D3D12_OPTIONS o = {};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof o)) || !o.TypedUAVLoadAdditionalFormats) {
        snprintf(s_why, sizeof s_why, "this graphics card cannot run AMD FSR 3 frame generation (no typed unordered-access loads)");
        return 1;
    }
    // Which generators AMD's DLL offers on this card, and which one this plug-in takes.
    ffxQueryDescGetVersions q = {};
    q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; q.device = device;
    uint64_t n = 0; q.outputCount = &n;
    ffxReturnCode_t rc = fQuery(nullptr, &q.header);
    uint64_t ids[16]; const char* names[16];
    if (rc == FFX_API_RETURN_OK && n) {
        if (n > 16) n = 16;
        q.versionIds = ids; q.versionNames = names;
        rc = fQuery(nullptr, &q.header);
    }
    if (rc != FFX_API_RETURN_OK || !n) {
        snprintf(s_why, sizeof s_why, "AMD's FSR files offer no frame generation for this graphics card (%u: %s)", (unsigned)rc, rc_name(rc));
        return 1;
    }
    char want[32] = "";
    if (!GetEnvironmentVariableA("GT5_FSR_FG_VERSION", want, sizeof want) || !want[0]) snprintf(want, sizeof want, "3.1");
    s_pinned = false;
    if (_stricmp(want, "auto") != 0)
        for (uint64_t i = 0; i < n && !s_pinned; i++)
            if (names[i] && !strncmp(names[i], want, strlen(want))) {
                s_pinned = true; s_version_id = ids[i];
                snprintf(s_version, sizeof s_version, "%s", names[i]);
            }
    if (!s_pinned) snprintf(s_version, sizeof s_version, "%s", names[0] ? names[0] : "?");   // AMD's own choice: the first listed
    fprintf(stderr, "[fsr] AMD's frame generators for this card:");
    for (uint64_t i = 0; i < n; i++) fprintf(stderr, " %s", names[i] ? names[i] : "?");
    fprintf(stderr, "; using %s%s\n", s_version, s_pinned ? " (pinned)" : " (AMD's choice)");
    s_loaded = true;
    return 1;
}

int fsr_fg_caps(gt5_fg_caps* caps)
{
    if (!caps) return 0;
    memset(caps, 0, sizeof *caps);
    snprintf(caps->name, sizeof caps->name, "AMD FSR 3");
    snprintf(caps->version, sizeof caps->version, "%s", s_version);
    caps->max_generated = 1;      // 3.1 writes one frame, half way, whatever is asked
    caps->wants_camera = 0;       // 3.1 reads near, far and the field of view only
    caps->available = s_loaded;
    if (!s_loaded) snprintf(caps->message, sizeof caps->message, "%s", s_why[0] ? s_why : "AMD FSR 3 did not start");
    return 1;
}

void fsr_fg_destroy()
{
    if (s_ctx && fConfigure && fDestroy) {
        // generation off first, as AMD's guide asks; with the flag no swap chain is looked for
        ffxConfigureDescFrameGeneration cf = {};
        cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        cf.frameGenerationEnabled = false;
        cf.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
        fConfigure(&s_ctx, &cf.header);
        fDestroy(&s_ctx, nullptr);
    }
    s_ctx = nullptr;
    memset(&s_fg, 0, sizeof s_fg);
}

int fsr_fg_create(ID3D12GraphicsCommandList* list, unsigned out_w, unsigned out_h, unsigned rect_w, unsigned rect_h,
                  unsigned render_w, unsigned render_h, unsigned max_generated, unsigned flags)
{
    (void)list; (void)max_generated;
    if (!s_loaded || !out_w || !out_h || !render_w || !render_h) return 0;
    if (!rect_w || !rect_h || rect_w > out_w || rect_h > out_h) { rect_w = out_w; rect_h = out_h; }
    fsr_fg_destroy();
    memset(&s_be, 0, sizeof s_be); memset(&s_ver, 0, sizeof s_ver); memset(&s_ovr, 0, sizeof s_ovr); memset(&s_cd, 0, sizeof s_cd);
    s_be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; s_be.device = s_device;
    if (s_pinned) {
        s_ovr.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION; s_ovr.versionId = s_version_id;
        s_be.header.pNext = &s_ovr.header;
    }
    s_ver.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION; s_ver.version = FFX_FRAMEGENERATION_VERSION;
    s_ver.header.pNext = &s_be.header;
    s_cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; s_cd.header.pNext = &s_ver.header;
    s_cd.flags = 0;
    if (flags & GT5_FG_HDR) s_cd.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
    if (flags & GT5_FG_DEPTH_INVERTED) s_cd.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
    if (flags & GT5_FG_DEPTH_INFINITE) s_cd.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
    if (flags & GT5_FG_MV_JITTERED) s_cd.flags |= FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
    if (s_debug) s_cd.flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
    s_cd.displaySize.width = out_w; s_cd.displaySize.height = out_h;
    s_cd.maxRenderSize.width = render_w; s_cd.maxRenderSize.height = render_h;
    s_cd.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    s_ctx = nullptr;
    const ffxReturnCode_t rc = fCreate(&s_ctx, &s_cd.header, nullptr);
    if (rc != FFX_API_RETURN_OK || !s_ctx) {
        set_error("AMD FSR 3 frame generation could not be made for these sizes", rc);
        s_ctx = nullptr;
        return 0;
    }
    if (s_debug) {
        ffxConfigureDescGlobalDebug gd = {};
        gd.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG; gd.effectId = FFX_API_EFFECT_ID_FRAMEGENERATION;
        gd.fpMessage = ffx_message; gd.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_VERBOSE;
        fConfigure(&s_ctx, &gd.header);
    }
    {   // the generator really in use (AMD's DLL may have chosen)
        ffxQueryGetProviderVersion pv = {};
        pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if (fQuery(&s_ctx, &pv.header) == FFX_API_RETURN_OK && pv.versionName) snprintf(s_version, sizeof s_version, "%s", pv.versionName);
    }
    s_fg.out_w = out_w; s_fg.out_h = out_h; s_fg.rect_w = rect_w; s_fg.rect_h = rect_h;
    s_fg.render_w = render_w; s_fg.render_h = render_h; s_fg.max_generated = 1;
    FfxApiEffectMemoryUsage mu = {};
    ffxQueryDescFrameGenerationGetGPUMemoryUsage mq = {};
    mq.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE; mq.gpuMemoryUsageFrameGeneration = &mu;
    if (fQuery(&s_ctx, &mq.header) == FFX_API_RETURN_OK) s_fg.memory_mb = (unsigned)((mu.totalUsageInBytes + 524288u) / 1048576u);
    return 1;
}

// One of the renderer's textures as AMD's API wants it described. A typeless 8-bit colour texture is
// read as UNORM.
FfxApiResource res(ID3D12Resource* r, uint32_t state)
{
    FfxApiResource x = ffxApiGetResourceDX12(r, state, 0);
    if (x.description.format == FFX_API_SURFACE_FORMAT_R8G8B8A8_TYPELESS) x.description.format = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    return x;
}

int fsr_fg_evaluate(const gt5_fg_frame* fr)
{
    if (!s_ctx) { snprintf(s_error, sizeof s_error, "AMD FSR 3 frame generation was not made"); return 0; }
    if (!fr || !fr->list || !fr->color || !fr->depth || !fr->motion || !fr->output[0]) {
        snprintf(s_error, sizeof s_error, "AMD FSR 3 was given a frame without a list, a picture, depth, vectors or an output texture");
        return 0;
    }
    const unsigned rw = fr->render_w ? fr->render_w : s_fg.render_w, rh = fr->render_h ? fr->render_h : s_fg.render_h;
    if (rw > s_fg.render_w || rh > s_fg.render_h) {
        snprintf(s_error, sizeof s_error, "AMD FSR 3 was made for vectors up to %ux%u and is given %ux%u", s_fg.render_w, s_fg.render_h, rw, rh);
        return 0;
    }
    FfxApiRect2D rect;
    rect.left = (int32_t)fr->rect_x; rect.top = (int32_t)fr->rect_y;
    rect.width = (int32_t)(fr->rect_w ? fr->rect_w : s_fg.out_w); rect.height = (int32_t)(fr->rect_h ? fr->rect_h : s_fg.out_h);
    const uint32_t in_state = FFX_API_RESOURCE_STATE_COMPUTE_READ;         // NON_PIXEL_SHADER_RESOURCE, as gt5_upscale.h says

    // 1. this frame's settings: no swap chain, no callbacks; the flag keeps the swap chain out of it
    ffxConfigureDescFrameGeneration cf = {};
    cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    cf.swapChain = nullptr;
    cf.frameGenerationEnabled = true;
    cf.allowAsyncWorkloads = false;
    cf.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    if (fr->hudless) cf.HUDLessColor = res(fr->hudless, in_state);
    cf.generationRect = rect;
    cf.frameID = fr->frame_id;
    ffxReturnCode_t rc = fConfigure(&s_ctx, &cf.header);
    if (rc != FFX_API_RETURN_OK) { set_error("AMD FSR 3 configure", rc); return 0; }

    // 2. depth and vectors
    ffxDispatchDescFrameGenerationPrepareV2 pr = {};
    pr.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    pr.frameID = fr->frame_id; pr.flags = cf.flags; pr.commandList = fr->list;
    pr.renderSize.width = rw; pr.renderSize.height = rh;
    pr.jitterOffset.x = fr->jitter_x; pr.jitterOffset.y = fr->jitter_y;
    pr.motionVectorScale.x = fr->mv_scale_x == 0.0f ? 1.0f : fr->mv_scale_x;   // ours are pixels of the render size already
    pr.motionVectorScale.y = fr->mv_scale_y == 0.0f ? 1.0f : fr->mv_scale_y;
    pr.frameTimeDelta = fr->frame_ms > 0.0f ? fr->frame_ms : 16.7f;
    pr.reset = fr->reset != 0;
    pr.cameraNear = fr->cam_near > 0.0f ? fr->cam_near : 0.1f;
    pr.cameraFar = fr->cam_far > pr.cameraNear ? fr->cam_far : pr.cameraNear * 10000.0f;
    pr.cameraFovAngleVertical = fr->cam_fov_y > 0.01f && fr->cam_fov_y < 3.0f ? fr->cam_fov_y : 0.9f;
    pr.viewSpaceToMetersFactor = 1.0f;
    pr.depth = res(fr->depth, in_state);
    pr.motionVectors = res(fr->motion, in_state);
    if (fr->cam_ok) {
        memcpy(pr.cameraPosition, fr->cam_pos, sizeof pr.cameraPosition);
        memcpy(pr.cameraUp, fr->cam_up, sizeof pr.cameraUp);
        memcpy(pr.cameraRight, fr->cam_right, sizeof pr.cameraRight);
        memcpy(pr.cameraForward, fr->cam_fwd, sizeof pr.cameraForward);
    } else {
        pr.cameraRight[0] = 1.0f; pr.cameraUp[1] = 1.0f; pr.cameraForward[2] = 1.0f;
    }
    rc = fDispatch(&s_ctx, &pr.header);
    if (rc != FFX_API_RETURN_OK) { set_error("AMD FSR 3 prepare", rc); return 0; }

    // 3. the made frame, into the renderer's texture
    ffxDispatchDescFrameGeneration dg = {};
    dg.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    dg.commandList = fr->list;
    dg.presentColor = res(fr->color, in_state);
    dg.outputs[0] = res(fr->output[0], FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dg.numGeneratedFrames = 1;
    dg.reset = fr->reset != 0;
    dg.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    dg.minMaxLuminance[0] = 0.0f; dg.minMaxLuminance[1] = 200.0f;
    dg.generationRect = rect;
    dg.frameID = fr->frame_id;
    rc = fDispatch(&s_ctx, &dg.header);
    if (rc != FFX_API_RETURN_OK) { set_error("AMD FSR 3 frame generation", rc); return 0; }
    return 1;
}

int fsr_fg_state(gt5_fg_state* st)
{
    if (!st) return 0;
    *st = s_fg;
    return s_ctx != nullptr;
}

void fsr_shutdown(ID3D12Device* device)
{
    (void)device;
    fsr_fg_destroy();
    // AMD's DLLs stay loaded: unloading them under a device that still lives is not worth the risk.
    s_device = nullptr;
}

const char* fsr_last_error() { return s_error; }

const gt5_upscale_api kApi = {
    GT5_UPSCALE_API_VERSION, fsr_init, nullptr, nullptr, nullptr, nullptr, fsr_shutdown, fsr_last_error,
    fsr_fg_caps, fsr_fg_create, fsr_fg_evaluate, fsr_fg_state, fsr_fg_destroy,
};

}  // namespace

extern "C" __declspec(dllexport) const gt5_upscale_api* gt5_upscale_get(void) { return &kApi; }
