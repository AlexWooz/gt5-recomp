// gt5_menu.cpp - the in-game settings menu: window messages, the controller, fonts and Direct3D 12.
// What is drawn is in gt5_menu_ui.cpp; the API is in gt5_menu.h. UTF-8.
//
// Dear ImGui's Win32 platform backend isn't used: it takes the window's client area for the display
// and reads XInput itself, while here the menu is laid out in the back buffer's pixels (the swap chain
// is stretched to the window, so the mouse is scaled to match) and the controller comes from the
// port's own pad code (which also reads DualSense and DualShock 4). The few messages it needs are
// handled below. The Direct3D 12 renderer backend (imgui_impl_dx12) is used as it is.
#include "gt5_menu_internal.h"

#include <d3dcompiler.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui_impl_dx12.h"

volatile long g_gt5_menu_captured;   // gt5_menu.h; `long` there so that plain C on any platform can read it

namespace gt5m {

Ui ui;

namespace {

// ---- state -----------------------------------------------------------------------------------------

enum { SRV_SLOTS = 64, LIST_RING = 4 };

struct FontFile { HANDLE file = INVALID_HANDLE_VALUE; HANDLE map = nullptr; void* data = nullptr; size_t size = 0; };

struct State {
    bool inited = false;
    HWND hwnd = nullptr;
    char version[96] = "";
    char title[160] = "";
    // Direct3D 12
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    unsigned w = 0, h = 0;
    bool no_overlay = false;
    ID3D12DescriptorHeap* srv_heap = nullptr;
    UINT srv_step = 0;
    bool srv_used[SRV_SLOTS] = {};
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    ID3D12Resource* overlay = nullptr;
    ID3D12RootSignature* rs = nullptr;
    ID3D12PipelineState* pso = nullptr;
    ID3D12CommandAllocator* alloc[LIST_RING] = {};
    UINT64 alloc_fence[LIST_RING] = {};
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 fence_v = 0;
    HANDLE fence_ev = nullptr;
    unsigned ring = 0, ring_k = 0;
    SRWLOCK overlay_lock = SRWLOCK_INIT;      // composite (shared) against a change of size (exclusive)
    volatile LONG overlay_visible = 0;
    // fonts
    ImFont* font = nullptr;
    FontFile font_main, font_sym, font_cjk, font_kr;
    bool fallback_fonts = false;
    // input
    SRWLOCK feed_lock = SRWLOCK_INIT;
    gt5_menu_pad pad = {};
    ULONGLONG pad_at = 0;
    gt5_menu_wheel wheel = {};
    unsigned short pad_prev = 0;
    volatile LONG toggle_req = 0, chord_hold = 0, release_wait = 0;
    ULONGLONG chord_t0 = 0, release_until = 0;
    bool chord_fired = false;
    int buttons_down = 0;
    int swallow_up_vk = 0;
    // settings read through the registry
    unsigned gen = ~0u;
    int menu_vk = VK_F1;
    unsigned short chord_mask = 0;
    bool osd = false;
    float user_scale = 1.0f;
    // window mode
    volatile LONG apply_mode = 0, apply_size = 0;
    volatile LONG64 save_due = 0;             // GetTickCount64 at which the changes are written, 0 = none waiting
    WINDOWPLACEMENT windowed_place = {};
    bool have_windowed_place = false;
    // readout
    SRWLOCK stats_lock = SRWLOCK_INIT;
    gt5_menu_stats stats = {};
    bool have_stats = false;
    // time
    LARGE_INTEGER qpf = {}, last = {};
    float applied_scale = 0.0f;
};
State s;

void update_captured()
{
    InterlockedExchange(&g_gt5_menu_captured, (ui.open || s.chord_hold || s.release_wait) ? 1 : 0);
}

// ---- settings the menu itself goes by ------------------------------------------------------------------

void refresh_settings()
{
    if (s.gen == gt5_settings_gen()) return;
    s.gen = gt5_settings_gen();
    char b[64];
    gt5_setting_str("GT5_MENU_KEY", b, sizeof b);
    const int vk = b[0] ? gt5_kb_vk(b) : 0;
    s.menu_vk = vk ? vk : VK_F1;
    gt5_setting_str("GT5_MENU_PAD", b, sizeof b);
    s.chord_mask = !_stricmp(b, "off") ? 0 : !_stricmp(b, "l3r3") ? (unsigned short)((1 << 1) | (1 << 2))
                                                                : (unsigned short)((1 << 0) | (1 << 3));
    s.osd = gt5_setting_bool("GT5_OSD", 0) != 0;
    const int sc = gt5_setting_int("GT5_MENU_SCALE", 100);
    s.user_scale = (sc >= 50 && sc <= 300) ? (float)sc / 100.0f : 1.0f;
    gt5_setting_str("GT5_MENU_LANG", b, sizeof b);
    if (!b[0]) gt5_setting_str("GT5_LAUNCHER_LANG", b, sizeof b);
    ui.zh = !_stricmp(b, "zh");
}

// ---- fonts -------------------------------------------------------------------------------------------
// Dear ImGui's own font is a 13-pixel bitmap: unreadable from a sofa and no Chinese. The menu uses
// Segoe UI from Windows' Fonts folder, and further fonts for what it lacks (symbols, Chinese and
// Japanese, Korean) only once such text is about to be drawn. The files are mapped, not read: Windows
// pages in the parts the rasteriser touches, so a 20 MB font collection costs what its used glyphs cost.
// Glyphs are rasterised as they are first drawn (Dear ImGui 1.92's dynamic atlas), at whatever size
// the menu has, so no glyph ranges are set and a change of size needs no rebuild here.

bool map_font(const wchar_t* name, FontFile* f)
{
    wchar_t path[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(path, MAX_PATH);
    if (!n || n > MAX_PATH - 40) return false;
    wcscat_s(path, MAX_PATH, L"\\Fonts\\");
    wcscat_s(path, MAX_PATH, name);
    f->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f->file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f->file, &size) || size.QuadPart <= 0 || size.QuadPart > 0x7FFFFFFF) { CloseHandle(f->file); f->file = INVALID_HANDLE_VALUE; return false; }
    f->map = CreateFileMappingW(f->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    f->data = f->map ? MapViewOfFile(f->map, FILE_MAP_READ, 0, 0, 0) : nullptr;
    if (!f->data) {
        if (f->map) CloseHandle(f->map);
        CloseHandle(f->file);
        f->map = nullptr; f->file = INVALID_HANDLE_VALUE;
        return false;
    }
    f->size = (size_t)size.QuadPart;
    return true;
}
void unmap_font(FontFile* f)
{
    if (f->data) UnmapViewOfFile(f->data);
    if (f->map) CloseHandle(f->map);
    if (f->file != INVALID_HANDLE_VALUE) CloseHandle(f->file);
    *f = FontFile();
}
bool map_first(FontFile* f, const wchar_t* const* names, int n, int* which)
{
    for (int i = 0; i < n; i++) if (map_font(names[i], f)) { if (which) *which = i; return true; }
    return false;
}
ImFont* add_font(const FontFile& f, const char* name, ImFont* merge_into)
{
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;      // the mapping is ours, and stays for as long as the font
    cfg.MergeMode = merge_into != nullptr;
    cfg.DstFont = merge_into;
    snprintf(cfg.Name, sizeof cfg.Name, "%s", name);
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(f.data, (int)f.size, 22.0f, &cfg);
}
void fonts_init()
{
    static const wchar_t* const mains[] = { L"segoeui.ttf", L"tahoma.ttf", L"arial.ttf" };
    if (map_first(&s.font_main, mains, 3, nullptr)) s.font = add_font(s.font_main, "main", nullptr);
    if (!s.font) s.font = ImGui::GetIO().Fonts->AddFontDefaultVector();   // no Windows font at all: Dear ImGui's own
    ImGui::GetIO().FontDefault = s.font;
}
void fonts_fallback()
{
    if (s.fallback_fonts || !s.font) return;
    s.fallback_fonts = true;
    static const wchar_t* const syms[] = { L"seguisym.ttf" };
    static const wchar_t* const cjks[] = { L"msyh.ttc", L"msyh.ttf", L"simsun.ttc", L"meiryo.ttc" };
    static const wchar_t* const krs[] = { L"malgun.ttf", L"gulim.ttc" };
    if (map_first(&s.font_sym, syms, 1, nullptr)) add_font(s.font_sym, "symbols", s.font);
    if (map_first(&s.font_cjk, cjks, 4, nullptr)) add_font(s.font_cjk, "cjk", s.font);
    if (map_first(&s.font_kr, krs, 2, nullptr)) add_font(s.font_kr, "korean", s.font);
}

// ---- Direct3D 12 -------------------------------------------------------------------------------------

D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu(unsigned slot)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = s.srv_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)slot * s.srv_step;
    return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu(unsigned slot)
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = s.srv_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)slot * s.srv_step;
    return h;
}
// Slot 0 is the overlay's; Dear ImGui's textures (the font atlas) take the others.
void srv_alloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
{
    for (unsigned i = 1; i < SRV_SLOTS; i++)
        if (!s.srv_used[i]) { s.srv_used[i] = true; *cpu = srv_cpu(i); *gpu = srv_gpu(i); return; }
    *cpu = srv_cpu(SRV_SLOTS - 1); *gpu = srv_gpu(SRV_SLOTS - 1);   // never reached: one or two textures exist
}
void srv_free(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
{
    const SIZE_T base = s.srv_heap->GetCPUDescriptorHandleForHeapStart().ptr;
    const unsigned i = (unsigned)((cpu.ptr - base) / s.srv_step);
    if (i > 0 && i < SRV_SLOTS) s.srv_used[i] = false;
}

ID3D12GraphicsCommandList* list_begin()
{
    const unsigned k = s.ring++ % LIST_RING;
    if (s.fence->GetCompletedValue() < s.alloc_fence[k]) {   // four lists back: rarely waits
        s.fence->SetEventOnCompletion(s.alloc_fence[k], s.fence_ev);
        WaitForSingleObject(s.fence_ev, 2000);
    }
    if (FAILED(s.alloc[k]->Reset()) || FAILED(s.list->Reset(s.alloc[k], nullptr))) return nullptr;
    s.ring_k = k;
    return s.list;
}
void list_end()
{
    if (FAILED(s.list->Close())) return;
    ID3D12CommandList* l[1] = { s.list };
    s.queue->ExecuteCommandLists(1, l);
    s.queue->Signal(s.fence, ++s.fence_v);
    s.alloc_fence[s.ring_k] = s.fence_v;
}
// Everything executed on the queue so far, the renderer's lists included, has run.
void wait_gpu()
{
    if (!s.fence) return;
    const UINT64 v = ++s.fence_v;
    if (FAILED(s.queue->Signal(s.fence, v))) return;
    if (s.fence->GetCompletedValue() < v) {
        s.fence->SetEventOnCompletion(v, s.fence_ev);
        WaitForSingleObject(s.fence_ev, 5000);
    }
}
void barrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    l->ResourceBarrier(1, &b);
}

bool overlay_create()
{
    if (s.no_overlay) return true;
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = s.w; rd.Height = s.h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = s.fmt; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv = {};
    cv.Format = s.fmt;
    if (FAILED(s.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                              &cv, IID_PPV_ARGS(&s.overlay)))) return false;
    s.overlay->SetName(L"gt5-menu-overlay");
    s.dev->CreateRenderTargetView(s.overlay, nullptr, s.rtv_heap->GetCPUDescriptorHandleForHeapStart());
    s.dev->CreateShaderResourceView(s.overlay, nullptr, srv_cpu(0));
    return true;
}

// The overlay over a frame: one triangle that covers the target, each pixel taken from the overlay
// at the same place (it is the frame's size), blended as premultiplied alpha, which is what Dear ImGui's
// blending leaves in a target that started transparent.
const char k_overlay_vs[] =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "    float2 p = float2((id << 1) & 2, id & 2);\n"
    "    return float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
    "}\n";
const char k_overlay_ps[] =
    "Texture2D<float4> Overlay : register(t0);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { return Overlay.Load(int3(pos.xy, 0)); }\n";

bool composite_create()
{
    if (s.no_overlay) return true;
    ID3DBlob *vs = nullptr, *ps = nullptr, *err = nullptr, *sig = nullptr;
    bool ok = SUCCEEDED(D3DCompile(k_overlay_vs, sizeof k_overlay_vs - 1, "gt5_menu_vs", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs, &err)) &&
              SUCCEEDED(D3DCompile(k_overlay_ps, sizeof k_overlay_ps - 1, "gt5_menu_ps", nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps, &err));
    if (ok) {
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = &range;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 1;
        rsd.pParameters = &param;
        ok = SUCCEEDED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) &&
             SUCCEEDED(s.dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&s.rs)));
    }
    if (ok) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = s.rs;
        pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pd.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = s.fmt;
        pd.SampleDesc.Count = 1;
        ok = SUCCEEDED(s.dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&s.pso)));
    }
    if (!ok) fprintf(stderr, "[menu] the overlay's shaders or pipeline could not be made%s%s\n",
                     err ? ": " : "", err ? (const char*)err->GetBufferPointer() : "");
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (sig) sig->Release();
    if (err) err->Release();
    return ok;
}

void composite_record(ID3D12GraphicsCommandList* l, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
    const D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)s.w, (float)s.h, 0.0f, 1.0f };
    const D3D12_RECT sc = { 0, 0, (LONG)s.w, (LONG)s.h };
    l->SetDescriptorHeaps(1, &s.srv_heap);
    l->SetGraphicsRootSignature(s.rs);
    l->SetPipelineState(s.pso);
    l->RSSetViewports(1, &vp);
    l->RSSetScissorRects(1, &sc);
    l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    l->SetGraphicsRootDescriptorTable(0, srv_gpu(0));
    l->DrawInstanced(3, 1, 0, 0);
}

// ---- the window ---------------------------------------------------------------------------------------

ImGuiKey vk_to_key(WPARAM vk, LPARAM lp)
{
    if (vk >= '0' && vk <= '9') return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
    if (vk >= 'A' && vk <= 'Z') return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
    if (vk >= VK_F1 && vk <= VK_F12) return (ImGuiKey)(ImGuiKey_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (ImGuiKey)(ImGuiKey_Keypad0 + (vk - VK_NUMPAD0));
    switch (vk) {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return (HIWORD(lp) & KF_EXTENDED) ? ImGuiKey_KeypadEnter : ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_SHIFT: case VK_LSHIFT: return ImGuiKey_LeftShift;
    case VK_RSHIFT: return ImGuiKey_RightShift;
    case VK_CONTROL: case VK_LCONTROL: return ImGuiKey_LeftCtrl;
    case VK_RCONTROL: return ImGuiKey_RightCtrl;
    case VK_MENU: case VK_LMENU: return ImGuiKey_LeftAlt;
    case VK_RMENU: return ImGuiKey_RightAlt;
    case VK_ADD: return ImGuiKey_KeypadAdd;
    case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
    default: return ImGuiKey_None;
    }
}

void mods_event(ImGuiIO& io)
{
    io.AddKeyEvent(ImGuiMod_Ctrl, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (GetKeyState(VK_MENU) & 0x8000) != 0);
}

// The window's client area is the back buffer stretched: a point of the one in the other's pixels.
void mouse_pos_event(ImGuiIO& io, LPARAM lp)
{
    RECT rc;
    if (!GetClientRect(s.hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
    const float x = (float)(short)LOWORD(lp) * (float)s.w / (float)rc.right;
    const float y = (float)(short)HIWORD(lp) * (float)s.h / (float)rc.bottom;
    io.AddMousePosEvent(x, y);
    ui.keyboard_last = false;
}

void set_open(bool open)
{
    if (!s.inited || ui.open == open) return;
    ImGuiIO& io = ImGui::GetIO();
    ui.open = open;
    if (open) {
        ui_opened();
        ClipCursor(nullptr);                    // whatever held the mouse in a race lets go
        s.last.QuadPart = 0;
    } else {
        bind_cancel();
        if (gt5_settings_dirty()) { ui.save_failed = !gt5_settings_save(); InterlockedExchange64(&s.save_due, 0); }
        // The press that closed the menu must not reach the game: it stays shut out until the
        // controller's buttons are up again.
        s.release_until = GetTickCount64() + 150;
        InterlockedExchange(&s.release_wait, 1);
        if (GetCapture() == s.hwnd) ReleaseCapture();
        s.buttons_down = 0;
    }
    io.ClearInputKeys();
    io.ClearInputMouse();
    update_captured();
    // The cursor over the window now, not at the next mouse movement.
    PostMessageW(s.hwnd, WM_SETCURSOR, (WPARAM)s.hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    fprintf(stderr, "[menu] %s\n", open ? "opened" : "closed");
}

// ---- window mode and size, applied from here (the launcher does the same from outside at start) ------

void window_apply(bool mode_changed)
{
    char mode[32], size[32];
    gt5_setting_stored("GT5_LAUNCH_WINDOW", mode, sizeof mode);
    gt5_setting_stored("PS3_WINDOW_SIZE", size, sizeof size);
    const LONG_PTR style = GetWindowLongPtrW(s.hwnd, GWL_STYLE);
    const bool borderless_now = (style & WS_POPUP) != 0 && (style & WS_CAPTION) == 0;
    HMONITOR mon = MonitorFromWindow(s.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);

    if (!_stricmp(mode, "borderless")) {
        if (borderless_now) return;
        s.windowed_place.length = sizeof s.windowed_place;
        s.have_windowed_place = GetWindowPlacement(s.hwnd, &s.windowed_place) != 0;
        SetWindowLongPtrW(s.hwnd, GWL_STYLE, (style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(s.hwnd, nullptr, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    if (borderless_now) {
        SetWindowLongPtrW(s.hwnd, GWL_STYLE, (style & ~(LONG_PTR)WS_POPUP) | WS_OVERLAPPEDWINDOW);
        // Back where the window was (rcNormalPosition is in workspace coordinates: the same as the
         // screen's unless a taskbar sits at the top or the left of the main monitor).
        const RECT& n = s.windowed_place.rcNormalPosition;
        if (s.have_windowed_place && n.right > n.left && n.bottom > n.top) {
            MONITORINFO pm = {};
            pm.cbSize = sizeof pm;
            GetMonitorInfoW(MonitorFromRect(&n, MONITOR_DEFAULTTONEAREST), &pm);
            SetWindowPos(s.hwnd, nullptr, n.left + (pm.rcWork.left - pm.rcMonitor.left), n.top + (pm.rcWork.top - pm.rcMonitor.top),
                         n.right - n.left, n.bottom - n.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            SetWindowPos(s.hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    // Maximizing and restoring are for a window that is on the screen (ShowWindow would show a hidden one).
    const bool shown = IsWindowVisible(s.hwnd) != 0;
    if (!_stricmp(mode, "max")) { if (shown) ShowWindow(s.hwnd, SW_MAXIMIZE); return; }
    if (mode_changed && shown && IsZoomed(s.hwnd)) ShowWindow(s.hwnd, SW_RESTORE);
    // A window: the size asked for, else (coming out of full screen with nothing to go back to) the
    // frame's shape at 720 lines. Kept on the monitor.
    unsigned cw = 0, ch = 0;
    if (sscanf(size, "%ux%u", &cw, &ch) != 2 || cw < 64 || ch < 64) {
        if (!(borderless_now && !s.have_windowed_place)) return;
        ch = 720; cw = s.h ? (unsigned)((double)s.w * 720.0 / (double)s.h + 0.5) : 1280;
    }
    if (shown && IsZoomed(s.hwnd)) ShowWindow(s.hwnd, SW_RESTORE);
    RECT wr = { 0, 0, (LONG)cw, (LONG)ch };
    AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(s.hwnd, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(s.hwnd, GWL_EXSTYLE));
    const int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    RECT cur;
    GetWindowRect(s.hwnd, &cur);
    int x = cur.left, y = cur.top;
    if (x + ww > mi.rcWork.right) x = mi.rcWork.right - ww;
    if (y + wh > mi.rcWork.bottom) y = mi.rcWork.bottom - wh;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    SetWindowPos(s.hwnd, nullptr, x, y, ww, wh, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

void on_window_setting(const char* key, const char*, const char*, void*)
{
    if (!_stricmp(key, "GT5_LAUNCH_WINDOW")) InterlockedExchange(&s.apply_mode, 1);
    else InterlockedExchange(&s.apply_size, 1);
}

// Any change to any setting, from the menu or from anywhere else, on whatever thread: written to
// settings.bat half a second after the last one, by the window's thread.
void on_any_setting(const char*, const char*, const char*, void*)
{
    InterlockedExchange64(&s.save_due, (LONG64)GetTickCount64() + 500);
}
void save_if_due()
{
    const LONG64 due = s.save_due;
    if (!due || (LONG64)GetTickCount64() < due) return;
    if (ui.open && ImGui::IsAnyItemActive()) return;      // not while a slider is being dragged
    InterlockedCompareExchange64(&s.save_due, 0, due);
    if (!gt5_settings_dirty()) return;
    ui.save_failed = !gt5_settings_save();
    if (!ui.save_failed) ui.saved_at = now_s();
}

// ---- a frame -----------------------------------------------------------------------------------------

float dpi_scale()
{
    typedef UINT (WINAPI* get_dpi_fn)(HWND);
    static get_dpi_fn get_dpi = (get_dpi_fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    const UINT dpi = get_dpi ? get_dpi(s.hwnd) : 96;
    return dpi >= 96 ? (float)dpi / 96.0f : 1.0f;
}

// The text's height in back-buffer pixels. A fixed share of the frame (22 px in a 1080-line frame),
// so the menu looks the same at 720p and at 4K; but never under 15 px on the screen itself, DPI
// scaling counted, when the window is smaller than the frame it shows; and never so big that fewer
// than 20 lines fit. Then the player's own size on top.
float text_px()
{
    float px = 22.0f * (float)s.h / 1080.0f;
    RECT rc;
    if (GetClientRect(s.hwnd, &rc) && rc.bottom > 0) {
        const float on_screen_min = 15.0f * dpi_scale();
        const float need = on_screen_min * (float)s.h / (float)rc.bottom;
        if (px < need) px = need;
    }
    px *= s.user_scale;
    const float most = (float)s.h / 20.0f;
    if (px > most) px = most;
    if (px < 8.0f) px = 8.0f;
    return px;
}

void pad_to_imgui(ImGuiIO& io, PadEdges* edges)
{
    gt5_menu_pad p;
    AcquireSRWLockShared(&s.feed_lock);
    p = s.pad;
    const ULONGLONG at = s.pad_at;
    ReleaseSRWLockShared(&s.feed_lock);
    if (!p.connected || GetTickCount64() - at > 500) { memset(&p, 0, sizeof p); p.lx = p.ly = p.rx = p.ry = 128; }
    const unsigned short b = p.buttons;
    const bool up = (b & (1 << 4)) || p.ly < 48, down = (b & (1 << 6)) || p.ly > 208;
    const bool left = (b & (1 << 7)) || p.lx < 48, right = (b & (1 << 5)) || p.lx > 208;
    if (p.connected) io.BackendFlags |= ImGuiBackendFlags_HasGamepad; else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, up);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, down);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, left);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, right);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (b & (1 << 14)) != 0);    // Cross: OK
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (b & (1 << 13)) != 0);   // Circle: back
    io.AddKeyEvent(ImGuiKey_GamepadL1, (b & (1 << 10)) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadR1, (b & (1 << 11)) != 0);
    // Square, Triangle and Start are the menu's own (Dear ImGui would open its window-moving mode on Square).
    const unsigned short pressed = (unsigned short)(b & ~s.pad_prev);
    edges->square = (pressed & (1 << 15)) != 0;
    edges->triangle = (pressed & (1 << 12)) != 0;
    edges->start = (pressed & (1 << 3)) != 0;
    edges->fast = p.l2 > 64 || p.r2 > 64;
    const int ry = (int)p.ry - 128;
    edges->ry = ry > 40 || ry < -40 ? (float)ry / 128.0f : 0.0f;
    if (pressed || up || down || left || right) ui.keyboard_last = true;
    s.pad_prev = b;
}

// Builds the frame's draw data. false: nothing to show.
bool build_frame()
{
    if (!s.inited) return false;
    refresh_settings();
    if (InterlockedExchange(&s.toggle_req, 0)) set_open(!ui.open);
    if (InterlockedExchange(&s.apply_mode, 0)) window_apply(true);
    if (InterlockedExchange(&s.apply_size, 0)) window_apply(false);
    if (s.release_wait && GetTickCount64() >= s.release_until) {
        AcquireSRWLockShared(&s.feed_lock);
        const bool idle = !s.pad.connected || s.pad.buttons == 0 || GetTickCount64() - s.pad_at > 500;
        ReleaseSRWLockShared(&s.feed_lock);
        if (idle) { InterlockedExchange(&s.release_wait, 0); update_captured(); }
    }
    save_if_due();
    if (!ui.open && !s.osd) return false;

    ImGuiIO& io = ImGui::GetIO();
    // Text beyond Latin is on its way: the menu in Chinese, or the Game page's list of languages.
    if (ui.zh || (ui.open && ui.page == GT5_PAGE_GAME)) fonts_fallback();
    const float px = text_px();
    if (px < s.applied_scale * 0.99f || px > s.applied_scale * 1.01f) {
        s.applied_scale = px;
        ui.scale = px / 22.0f;
        ui_apply_style(ui.scale);
    }
    io.DisplaySize = ImVec2((float)s.w, (float)s.h);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    double dt = s.last.QuadPart ? (double)(t.QuadPart - s.last.QuadPart) / (double)s.qpf.QuadPart : 1.0 / 60.0;
    if (dt <= 0.0) dt = 1.0 / 1000.0;
    if (dt > 0.25) dt = 0.25;
    io.DeltaTime = (float)dt;
    s.last = t;
    PadEdges edges;
    pad_to_imgui(io, &edges);

    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    if (ui.open) ui_build(edges); else ui_build_osd();
    ImGui::Render();

    return true;
}

}  // namespace

// ---- shared with gt5_menu_ui.cpp ----------------------------------------------------------------------

double now_s()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return s.qpf.QuadPart ? (double)t.QuadPart / (double)s.qpf.QuadPart : 0.0;
}
const char* version_text() { return s.version; }
const char* title_text() { return s.title; }
unsigned frame_w() { return s.w; }
unsigned frame_h() { return s.h; }
void need_fallback_fonts() { /* picked up before the next frame: build_frame() looks at the page and the language */ }
void note_changed() { /* every change is noticed through the registry: on_any_setting() */ }
void request_close() { InterlockedExchange(&s.toggle_req, ui.open ? 1 : 0); }

bool stats_get(gt5_menu_stats* out)
{
    AcquireSRWLockShared(&s.stats_lock);
    *out = s.stats;
    const bool have = s.have_stats;
    ReleaseSRWLockShared(&s.stats_lock);
    return have;
}
bool wheel_get(gt5_menu_wheel* out)
{
    AcquireSRWLockShared(&s.feed_lock);
    *out = s.wheel;
    ReleaseSRWLockShared(&s.feed_lock);
    return out->connected != 0;
}
void bind_begin(int kind, int row, bool add)
{
    ui.bind = kind;
    ui.bind_row = row;
    ui.bind_add = add;
    ui.bind_t0 = now_s();
    wheel_get(&ui.bind_wheel0);
}
void bind_cancel() { ui.bind = BIND_NONE; }

void open_url(const char* url)
{
    wchar_t w[512];
    if (MultiByteToWideChar(CP_UTF8, 0, url, -1, w, 512)) ShellExecuteW(nullptr, L"open", w, nullptr, nullptr, SW_SHOWNORMAL);
}
void open_logs_folder()
{
    // GT5\logs, next to settings.bat (play.bat makes it).
    char path[1100];
    snprintf(path, sizeof path, "%s", gt5_settings_path());
    char* cut = strrchr(path, '\\');
    if (!cut) cut = strrchr(path, '/');
    if (cut) snprintf(cut + 1, sizeof path - (size_t)(cut + 1 - path), "logs"); else snprintf(path, sizeof path, "logs");
    wchar_t w[1100];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1100)) ShellExecuteW(nullptr, L"open", w, nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace gt5m

using namespace gt5m;

// ---- the API ------------------------------------------------------------------------------------------

extern "C" int gt5_menu_init(const gt5_menu_init_info* info)
{
    if (s.inited) return 1;
    if (!info || !info->hwnd || !info->device || !info->queue || !info->frame_w || !info->frame_h) return 0;
    if (!gt5_settings_path()[0]) gt5_settings_init(nullptr);
    s.hwnd = info->hwnd;
    s.dev = info->device;
    s.queue = info->queue;
    s.fmt = info->rtv_format ? info->rtv_format : DXGI_FORMAT_R8G8B8A8_UNORM;
    s.w = info->frame_w; s.h = info->frame_h;
    s.no_overlay = info->no_overlay != 0;
    QueryPerformanceFrequency(&s.qpf);
    {
        const char* v = info->version && *info->version ? info->version : getenv("GT5_VER");
        snprintf(s.version, sizeof s.version, "%s", v && *v && _stricmp(v, "none") ? v : "");
        const char* t = info->title && *info->title ? info->title : getenv("PS3_TITLE");
        snprintf(s.title, sizeof s.title, "%s", t && *t ? t : "Gran Turismo 5");
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = SRV_SLOTS;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    bool ok = SUCCEEDED(s.dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&s.srv_heap)));
    s.srv_step = s.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 1;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ok = ok && SUCCEEDED(s.dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&s.rtv_heap)));
    for (int i = 0; ok && i < LIST_RING; i++)
        ok = SUCCEEDED(s.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.alloc[i])));
    ok = ok && SUCCEEDED(s.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.alloc[0], nullptr, IID_PPV_ARGS(&s.list)));
    if (ok) { s.list->Close(); s.list->SetName(L"gt5-menu-list"); }
    ok = ok && SUCCEEDED(s.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s.fence)));
    s.fence_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ok = ok && s.fence_ev && overlay_create() && composite_create();
    if (!ok) {
        fprintf(stderr, "[menu] Direct3D 12 objects could not be made: the settings menu is off\n");
        return 0;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;                 // nothing of the menu's is written to disk but settings.bat
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigNavEscapeClearFocusItem = false;
    io.ConfigNavCursorVisibleAuto = true;
    io.BackendPlatformName = "gt5_menu";
    fonts_init();

    ImGui_ImplDX12_InitInfo ii;
    ii.Device = s.dev;
    ii.CommandQueue = s.queue;
    ii.NumFramesInFlight = 3;
    ii.RTVFormat = s.fmt;
    ii.DSVFormat = DXGI_FORMAT_UNKNOWN;
    ii.SrvDescriptorHeap = s.srv_heap;
    ii.SrvDescriptorAllocFn = srv_alloc;
    ii.SrvDescriptorFreeFn = srv_free;
    if (!ImGui_ImplDX12_Init(&ii)) {
        fprintf(stderr, "[menu] Dear ImGui's Direct3D 12 backend did not start: the settings menu is off\n");
        ImGui::DestroyContext();
        return 0;
    }
    gt5_settings_on_change("GT5_LAUNCH_WINDOW", on_window_setting, nullptr);
    gt5_settings_on_change("PS3_WINDOW_SIZE", on_window_setting, nullptr);
    gt5_settings_on_change(nullptr, on_any_setting, nullptr);
    gt5_menu_stats_clear(&s.stats);
    s.inited = true;
    refresh_settings();
    char key[32];
    gt5_setting_str("GT5_MENU_KEY", key, sizeof key);
    fprintf(stderr, "[menu] settings menu ready: %s opens it (frame %ux%u, settings in %s)\n", key[0] ? key : "F1",
            s.w, s.h, gt5_settings_path());
    return 1;
}

extern "C" void gt5_menu_shutdown(void)
{
    if (!s.inited) return;
    s.inited = false;
    InterlockedExchange(&s.overlay_visible, 0);
    InterlockedExchange(&g_gt5_menu_captured, 0);
    if (gt5_settings_dirty()) gt5_settings_save();
    wait_gpu();
    ImGui_ImplDX12_Shutdown();
    ImGui::DestroyContext();
    unmap_font(&s.font_main); unmap_font(&s.font_sym); unmap_font(&s.font_cjk); unmap_font(&s.font_kr);
    if (s.pso) s.pso->Release();
    if (s.rs) s.rs->Release();
    if (s.overlay) s.overlay->Release();
    if (s.list) s.list->Release();
    for (int i = 0; i < LIST_RING; i++) if (s.alloc[i]) s.alloc[i]->Release();
    if (s.fence) s.fence->Release();
    if (s.rtv_heap) s.rtv_heap->Release();
    if (s.srv_heap) s.srv_heap->Release();
    if (s.fence_ev) CloseHandle(s.fence_ev);
    s = State();
    ui = Ui();
}

extern "C" void gt5_menu_set_frame_size(unsigned w, unsigned h)
{
    if (!s.inited || !w || !h || (w == s.w && h == s.h)) return;
    // No list that draws the old overlay may be waiting: the caller has let its presenter finish (a
    // new swap chain needs that anyway), and whatever is on the queue is run to the end here.
    AcquireSRWLockExclusive(&s.overlay_lock);
    InterlockedExchange(&s.overlay_visible, 0);
    wait_gpu();
    if (s.overlay) { s.overlay->Release(); s.overlay = nullptr; }
    s.w = w; s.h = h;
    if (!overlay_create()) fprintf(stderr, "[menu] no overlay texture at %ux%u\n", w, h);
    ReleaseSRWLockExclusive(&s.overlay_lock);
    s.applied_scale = 0.0f;
}

extern "C" int gt5_menu_is_open(void) { return ui.open ? 1 : 0; }
extern "C" void gt5_menu_set_open(int open) { set_open(open != 0); }

extern "C" int gt5_menu_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
    if (!s.inited || hwnd != s.hwnd) return 0;
    ImGuiIO& io = ImGui::GetIO();
    if (result) *result = 0;
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const bool repeat = (lp & (1L << 30)) != 0;
        if (!ui.open) {
            refresh_settings();
            if ((int)wp == s.menu_vk && !repeat) { set_open(true); s.swallow_up_vk = (int)wp; return 1; }
            return 0;
        }
        ui.keyboard_last = true;
        if (ui.bind == BIND_KEY_ROW || ui.bind == BIND_MENU_KEY) {
            // The next key is the one being chosen: it never reaches the menu's own navigation.
            if (!repeat) {
                s.swallow_up_vk = (int)wp;
                if (wp == VK_ESCAPE) ui_key_taken(nullptr);
                else { const char* name = gt5_kb_key_from_vk((int)wp, (long long)lp); if (name) ui_key_taken(name); }
            }
            return 1;
        }
        if ((int)wp == s.menu_vk) { if (!repeat) { set_open(false); s.swallow_up_vk = (int)wp; } return 1; }
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) return 0;            // Alt+F4 stays the window's
        mods_event(io);
        const ImGuiKey k = vk_to_key(wp, lp);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, true);
        return 1;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        if (s.swallow_up_vk && (int)wp == s.swallow_up_vk) { s.swallow_up_vk = 0; return 1; }
        if (!ui.open) return 0;
        mods_event(io);
        const ImGuiKey k = vk_to_key(wp, lp);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, false);
        return 1;
    }
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
        return ui.open || s.swallow_up_vk ? 1 : 0;
    case WM_SYSCOMMAND:
        return ui.open && (wp & 0xFFF0) == SC_KEYMENU ? 1 : 0;        // Alt alone doesn't open the window menu
    case WM_MOUSEMOVE:
        if (!ui.open) return 0;
        mouse_pos_event(io, lp);
        return 1;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: {
        if (!ui.open) return 0;
        if ((msg == WM_XBUTTONDOWN || msg == WM_XBUTTONDBLCLK) && ui.bind == BIND_KEY_ROW) {
            ui_key_taken(GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? "Mouse4" : "Mouse5");
            if (result) *result = TRUE;
            return 1;
        }
        const int button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) ? 0 :
                           (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) ? 1 :
                           (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK) ? 2 : -1;
        if (button >= 0) {
            if (s.buttons_down++ == 0) SetCapture(hwnd);
            mouse_pos_event(io, lp);
            io.AddMouseButtonEvent(button, true);
        }
        return 1;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP: {
        if (!ui.open) return 0;
        const int button = msg == WM_LBUTTONUP ? 0 : msg == WM_RBUTTONUP ? 1 : msg == WM_MBUTTONUP ? 2 : -1;
        if (button >= 0) {
            if (s.buttons_down > 0 && --s.buttons_down == 0 && GetCapture() == hwnd) ReleaseCapture();
            io.AddMouseButtonEvent(button, false);
        }
        return 1;
    }
    case WM_MOUSEWHEEL:
        if (!ui.open) return 0;
        io.AddMouseWheelEvent(0.0f, (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA);
        ui.keyboard_last = false;
        return 1;
    case WM_SETCURSOR:
        // The arrow over the picture while the menu is open, whatever hid it for the game.
        if (ui.open && LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW)); if (result) *result = TRUE; return 1; }
        return 0;
    case WM_KILLFOCUS:
        if (ui.open) { io.AddFocusEvent(false); s.buttons_down = 0; }
        return 0;
    case WM_SETFOCUS:
        if (ui.open) io.AddFocusEvent(true);
        return 0;
    default:
        return 0;
    }
}

extern "C" void gt5_menu_feed_pad(const gt5_menu_pad* pad)
{
    if (!pad) return;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&s.feed_lock);
    s.pad = *pad;
    s.pad_at = now;
    // The buttons that open and close the menu, held together for half a second. The game is shut out
    // from the moment both are down, so it never sees the pair (it has seen the first of the two).
    const unsigned short mask = s.chord_mask;
    const bool down = s.inited && mask && pad->connected && (pad->buttons & mask) == mask;
    if (down) {
        if (!s.chord_t0) { s.chord_t0 = now; s.chord_fired = false; InterlockedExchange(&s.chord_hold, 1); }
        else if (!s.chord_fired && now - s.chord_t0 >= 500) { s.chord_fired = true; InterlockedExchange(&s.toggle_req, 1); }
    } else if (s.chord_t0) {
        s.chord_t0 = 0;
        InterlockedExchange(&s.chord_hold, 0);
        // let go: if the menu didn't open, the game gets the controller back once the buttons are up
        if (!ui.open) { s.release_until = now + 100; InterlockedExchange(&s.release_wait, 1); }
    }
    if (s.release_wait && !down && pad->buttons == 0 && now >= s.release_until) InterlockedExchange(&s.release_wait, 0);
    ReleaseSRWLockExclusive(&s.feed_lock);
    update_captured();
}

extern "C" void gt5_menu_feed_wheel(const gt5_menu_wheel* wheel)
{
    if (!wheel) return;
    AcquireSRWLockExclusive(&s.feed_lock);
    s.wheel = *wheel;
    s.wheel.name[sizeof s.wheel.name - 1] = 0;
    ReleaseSRWLockExclusive(&s.feed_lock);
}

extern "C" void gt5_menu_stats_clear(gt5_menu_stats* st)
{
    if (!st) return;
    memset(st, 0, sizeof *st);
    st->game_fps = st->shown_fps = st->drawn_fps = st->made_fps = st->cpu_cores = st->gpu_ms = -1.0f;
    st->draws = st->frame_w = st->frame_h = st->display_hz = -1;
}

extern "C" void gt5_menu_set_stats(const gt5_menu_stats* st)
{
    if (!st) return;
    AcquireSRWLockExclusive(&s.stats_lock);
    s.stats = *st;
    s.stats.gpu_name[sizeof s.stats.gpu_name - 1] = 0;
    s.have_stats = true;
    ReleaseSRWLockExclusive(&s.stats_lock);
}

extern "C" int gt5_menu_frame(void)
{
    if (!s.inited || s.no_overlay || !s.overlay) return 0;
    if (!build_frame()) { InterlockedExchange(&s.overlay_visible, 0); return 0; }
    ID3D12GraphicsCommandList* l = list_begin();
    if (!l) return 0;
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = s.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    barrier(l, s.overlay, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    l->ClearRenderTargetView(rtv, clear, 0, nullptr);
    l->SetDescriptorHeaps(1, &s.srv_heap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), l);
    barrier(l, s.overlay, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    list_end();
    InterlockedExchange(&s.overlay_visible, 1);
    return 1;
}

extern "C" int gt5_menu_overlay_visible(void) { return s.overlay_visible != 0; }

extern "C" void gt5_menu_overlay_composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
    if (!list || !s.overlay_visible) return;
    AcquireSRWLockShared(&s.overlay_lock);
    if (s.overlay && s.pso) composite_record(list, rtv);
    ReleaseSRWLockShared(&s.overlay_lock);
}

extern "C" void gt5_menu_overlay_blit(ID3D12Resource* back_buffer, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
    if (!back_buffer || !s.overlay_visible || !s.overlay || !s.pso) return;
    ID3D12GraphicsCommandList* l = list_begin();
    if (!l) return;
    barrier(l, back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    composite_record(l, rtv);
    barrier(l, back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    list_end();
}

extern "C" int gt5_menu_frame_direct(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
    if (!s.inited || !list) return 0;
    if (!build_frame()) return 0;
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list->SetDescriptorHeaps(1, &s.srv_heap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), list);
    return 1;
}

extern "C" void gt5_menu_test_goto(int page, int sub)
{
    if (page >= 0 && page < GT5_PAGE_COUNT) ui.page = page;
    ui.sub = sub;
    ui.bind = BIND_NONE;
    ui.in_content = true;
    ui.focus_content = true;
    ui.focus_nav = false;
    if (sub == SUB_KEYS) { char b[1200]; gt5_setting_stored("PS3_KEYS", b, sizeof b); gt5_kb_parse(b, &ui.keys); }
    if (sub == SUB_WHEEL) {
        char b[600], p[200];
        gt5_setting_stored("PS3_WHEEL_BUTTONS", b, sizeof b);
        gt5_setting_stored("PS3_WHEEL_PEDALS", p, sizeof p);
        gt5_wheel_parse(b, p, &ui.wheel);
    }
}
extern "C" int gt5_menu_test_bind_row(int row)
{
    if (ui.sub == SUB_KEYS) { bind_begin(BIND_KEY_ROW, row, false); return 1; }
    if (ui.sub == SUB_WHEEL) { bind_begin(row >= GT5_WHEEL_BUTTONS ? BIND_WHEEL_PEDAL : BIND_WHEEL_BUTTON, row >= GT5_WHEEL_BUTTONS ? row - GT5_WHEEL_BUTTONS : row, false); return 1; }
    return 0;
}
extern "C" int gt5_menu_test_page(void) { return ui.page; }
extern "C" int gt5_menu_test_focus(const char* key)
{
    const gt5_setting_def* d = gt5_settings_find(key);
    if (!d || d->page < 0) return 0;
    ui.page = d->page;
    ui.sub = SUB_NONE;
    ui.in_content = true;
    snprintf(ui.focus_key, sizeof ui.focus_key, "%s", d->key);
    return 1;
}
