// gt5_menu_dx12.cpp - the in-game settings menu drawn with Direct3D 12 (the API in gt5_menu.h). The
// window, the input, the sizes and what is drawn are shared with the Vulkan renderer (gt5_menu.cpp,
// gt5_menu_ui.cpp); this file has only the drawing. Dear ImGui's Direct3D 12 renderer backend
// (imgui_impl_dx12) is used as it is. UTF-8.
#include "gt5_menu_internal.h"

#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

#include "imgui_impl_dx12.h"

namespace gt5m {
namespace {

enum { SRV_SLOTS = 64, LIST_RING = 4 };

struct Dx12 {
    bool inited = false;
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    unsigned w = 0, h = 0;                    // the back buffers' size: the overlay's
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
};
Dx12 s;

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

void dx12_new_frame() { ImGui_ImplDX12_NewFrame(); }

}  // namespace
}  // namespace gt5m

using namespace gt5m;

// ---- the API (gt5_menu.h) ------------------------------------------------------------------------------

extern "C" int gt5_menu_init(const gt5_menu_init_info* info)
{
    if (s.inited) return 1;
    if (core_inited()) return 0;              // the menu is already drawn by the other renderer
    if (!info || !info->hwnd || !info->device || !info->queue || !info->frame_w || !info->frame_h) return 0;
    core_begin(info->hwnd, info->frame_w, info->frame_h, info->version, info->title);
    s.dev = info->device;
    s.queue = info->queue;
    s.fmt = info->rtv_format ? info->rtv_format : DXGI_FORMAT_R8G8B8A8_UNORM;
    s.w = info->frame_w; s.h = info->frame_h;
    s.no_overlay = info->no_overlay != 0;

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

    core_context();
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
        core_context_abort();
        return 0;
    }
    s.inited = true;
    core_finish(dx12_new_frame, "frame");
    return 1;
}

extern "C" void gt5_menu_shutdown(void)
{
    if (!s.inited) return;
    s.inited = false;
    InterlockedExchange(&s.overlay_visible, 0);
    core_shutdown_begin();
    wait_gpu();
    ImGui_ImplDX12_Shutdown();
    core_shutdown_end();
    if (s.pso) s.pso->Release();
    if (s.rs) s.rs->Release();
    if (s.overlay) s.overlay->Release();
    if (s.list) s.list->Release();
    for (int i = 0; i < LIST_RING; i++) if (s.alloc[i]) s.alloc[i]->Release();
    if (s.fence) s.fence->Release();
    if (s.rtv_heap) s.rtv_heap->Release();
    if (s.srv_heap) s.srv_heap->Release();
    if (s.fence_ev) CloseHandle(s.fence_ev);
    s = Dx12();
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
    core_set_size(w, h);
}

extern "C" int gt5_menu_frame(void)
{
    if (!s.inited || s.no_overlay || !s.overlay) return 0;
    if (!core_build_frame()) { InterlockedExchange(&s.overlay_visible, 0); return 0; }
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
    if (!core_build_frame()) return 0;
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list->SetDescriptorHeaps(1, &s.srv_heap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), list);
    return 1;
}
