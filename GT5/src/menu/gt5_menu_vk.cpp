// gt5_menu_vk.cpp - the in-game settings menu drawn with Vulkan (the API in gt5_menu_vk.h). The window,
// the input, the sizes and what is drawn are shared with the Direct3D 12 renderer (gt5_menu.cpp,
// gt5_menu_ui.cpp); this file has only the drawing. Dear ImGui's Vulkan renderer backend
// (imgui_impl_vulkan, with VK_NO_PROTOTYPES: every function from the renderer's loader) is used as it is.
//
// The renderer (rsx_vulkan_backend.c) copies each frame into a swap chain image that is the window's size
// and presents it from two threads: the one that draws (the window's) and, with frame generation, a
// presenter of its own. So the menu is drawn on the window's thread into an overlay image of the swap
// chain's size (transparent where there is no menu: premultiplied alpha, as Dear ImGui's blending leaves
// it), and whoever presents puts the overlay over the image after its copy, with one small render pass
// (gt5_menu_vk_composite). The overlay is laid out in the window's pixels like the D3D12 one; here the
// swap chain already has the window's size, so it is drawn one to one. UTF-8.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "gt5_menu_internal.h"

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>

#include "gt5_menu_vk.h"
#include "imgui_impl_vulkan.h"

namespace gt5m {
namespace {

enum { RING = 4, MAX_IMAGES = 8 };

// The two shaders of the composite pass, compiled with glslang (Vulkan 1.0, SPIR-V 1.0):
//   vertex:    void main() { vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
//                            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
//   fragment:  layout(set = 0, binding = 0) uniform sampler2D overlay; layout(location = 0) out vec4 color;
//              void main() { color = texelFetch(overlay, ivec2(gl_FragCoord.xy), 0); }
static const uint32_t k_composite_vs[284] = {
    0x07230203, 0x00010000, 0x0008000b, 0x0000002b, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000c, 0x0000001d, 0x00030003,
    0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000009,
    0x00000070, 0x00060005, 0x0000000c, 0x565f6c67, 0x65747265, 0x646e4978, 0x00007865, 0x00060005,
    0x0000001b, 0x505f6c67, 0x65567265, 0x78657472, 0x00000000, 0x00060006, 0x0000001b, 0x00000000,
    0x505f6c67, 0x7469736f, 0x006e6f69, 0x00070006, 0x0000001b, 0x00000001, 0x505f6c67, 0x746e696f,
    0x657a6953, 0x00000000, 0x00070006, 0x0000001b, 0x00000002, 0x435f6c67, 0x4470696c, 0x61747369,
    0x0065636e, 0x00070006, 0x0000001b, 0x00000003, 0x435f6c67, 0x446c6c75, 0x61747369, 0x0065636e,
    0x00030005, 0x0000001d, 0x00000000, 0x00040047, 0x0000000c, 0x0000000b, 0x0000002a, 0x00030047,
    0x0000001b, 0x00000002, 0x00050048, 0x0000001b, 0x00000000, 0x0000000b, 0x00000000, 0x00050048,
    0x0000001b, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000001b, 0x00000002, 0x0000000b,
    0x00000003, 0x00050048, 0x0000001b, 0x00000003, 0x0000000b, 0x00000004, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007,
    0x00000006, 0x00000002, 0x00040020, 0x00000008, 0x00000007, 0x00000007, 0x00040015, 0x0000000a,
    0x00000020, 0x00000001, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a, 0x0004003b, 0x0000000b,
    0x0000000c, 0x00000001, 0x0004002b, 0x0000000a, 0x0000000e, 0x00000001, 0x0004002b, 0x0000000a,
    0x00000010, 0x00000002, 0x00040017, 0x00000017, 0x00000006, 0x00000004, 0x00040015, 0x00000018,
    0x00000020, 0x00000000, 0x0004002b, 0x00000018, 0x00000019, 0x00000001, 0x0004001c, 0x0000001a,
    0x00000006, 0x00000019, 0x0006001e, 0x0000001b, 0x00000017, 0x00000006, 0x0000001a, 0x0000001a,
    0x00040020, 0x0000001c, 0x00000003, 0x0000001b, 0x0004003b, 0x0000001c, 0x0000001d, 0x00000003,
    0x0004002b, 0x0000000a, 0x0000001e, 0x00000000, 0x0004002b, 0x00000006, 0x00000020, 0x40000000,
    0x0004002b, 0x00000006, 0x00000022, 0x3f800000, 0x0004002b, 0x00000006, 0x00000025, 0x00000000,
    0x00040020, 0x00000029, 0x00000003, 0x00000017, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200f8, 0x00000005, 0x0004003b, 0x00000008, 0x00000009, 0x00000007, 0x0004003d,
    0x0000000a, 0x0000000d, 0x0000000c, 0x000500c4, 0x0000000a, 0x0000000f, 0x0000000d, 0x0000000e,
    0x000500c7, 0x0000000a, 0x00000011, 0x0000000f, 0x00000010, 0x0004006f, 0x00000006, 0x00000012,
    0x00000011, 0x0004003d, 0x0000000a, 0x00000013, 0x0000000c, 0x000500c7, 0x0000000a, 0x00000014,
    0x00000013, 0x00000010, 0x0004006f, 0x00000006, 0x00000015, 0x00000014, 0x00050050, 0x00000007,
    0x00000016, 0x00000012, 0x00000015, 0x0003003e, 0x00000009, 0x00000016, 0x0004003d, 0x00000007,
    0x0000001f, 0x00000009, 0x0005008e, 0x00000007, 0x00000021, 0x0000001f, 0x00000020, 0x00050050,
    0x00000007, 0x00000023, 0x00000022, 0x00000022, 0x00050083, 0x00000007, 0x00000024, 0x00000021,
    0x00000023, 0x00050051, 0x00000006, 0x00000026, 0x00000024, 0x00000000, 0x00050051, 0x00000006,
    0x00000027, 0x00000024, 0x00000001, 0x00070050, 0x00000017, 0x00000028, 0x00000026, 0x00000027,
    0x00000025, 0x00000022, 0x00050041, 0x00000029, 0x0000002a, 0x0000001d, 0x0000001e, 0x0003003e,
    0x0000002a, 0x00000028, 0x000100fd, 0x00010038,
};
static const uint32_t k_composite_fs[169] = {
    0x07230203, 0x00010000, 0x0008000b, 0x0000001a, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00000010, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d,
    0x00000000, 0x00040005, 0x00000009, 0x6f6c6f63, 0x00000072, 0x00040005, 0x0000000d, 0x7265766f,
    0x0079616c, 0x00060005, 0x00000010, 0x465f6c67, 0x43676172, 0x64726f6f, 0x00000000, 0x00040047,
    0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000d, 0x00000021, 0x00000000, 0x00040047,
    0x0000000d, 0x00000022, 0x00000000, 0x00040047, 0x00000010, 0x0000000b, 0x0000000f, 0x00020013,
    0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017,
    0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b,
    0x00000008, 0x00000009, 0x00000003, 0x00090019, 0x0000000a, 0x00000006, 0x00000001, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001b, 0x0000000b, 0x0000000a, 0x00040020,
    0x0000000c, 0x00000000, 0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000000, 0x00040020,
    0x0000000f, 0x00000001, 0x00000007, 0x0004003b, 0x0000000f, 0x00000010, 0x00000001, 0x00040017,
    0x00000011, 0x00000006, 0x00000002, 0x00040015, 0x00000014, 0x00000020, 0x00000001, 0x00040017,
    0x00000015, 0x00000014, 0x00000002, 0x0004002b, 0x00000014, 0x00000017, 0x00000000, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000b,
    0x0000000e, 0x0000000d, 0x0004003d, 0x00000007, 0x00000012, 0x00000010, 0x0007004f, 0x00000011,
    0x00000013, 0x00000012, 0x00000012, 0x00000000, 0x00000001, 0x0004006e, 0x00000015, 0x00000016,
    0x00000013, 0x00040064, 0x0000000a, 0x00000018, 0x0000000e, 0x0007005f, 0x00000007, 0x00000019,
    0x00000018, 0x00000016, 0x00000002, 0x00000017, 0x0003003e, 0x00000009, 0x00000019, 0x000100fd,
    0x00010038,
};

// The Vulkan functions this file calls, from the renderer's loader (the runtime loads vulkan-1.dll at
// run time; nothing here links to it).
#define GT5M_VK_FNS(X) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) \
    X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkResetCommandBuffer) \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) \
    X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdDraw) \
    X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkQueueSubmit) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
    X(vkCreateSampler) X(vkDestroySampler)
#define GT5M_VK_DECL(name) PFN_##name name = nullptr;

struct Vk {
    bool inited = false;
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    void (*qlock)(int) = nullptr;
    struct { GT5M_VK_FNS(GT5M_VK_DECL) } f;
    VkPhysicalDeviceMemoryProperties mem = {};
    // the overlay: the menu's own picture, the swap chain's size, drawn on the window's thread
    VkRenderPass rp_overlay = VK_NULL_HANDLE;
    VkImage ov_img = VK_NULL_HANDLE;
    VkDeviceMemory ov_mem = VK_NULL_HANDLE;
    VkImageView ov_view = VK_NULL_HANDLE;
    VkFramebuffer ov_fb = VK_NULL_HANDLE;
    uint32_t ow = 0, oh = 0;              // its size
    uint32_t laid_w = 0, laid_h = 0;      // the size the menu's layout was last told (the window's thread)
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cb[RING] = {};
    VkFence fence[RING] = {};
    bool busy[RING] = {};
    unsigned ring = 0;
    // the composite pass over a swap chain image
    VkFormat sc_fmt = VK_FORMAT_UNDEFINED;
    uint32_t sc_w = 0, sc_h = 0, sc_n = 0;
    VkImage sc_img[MAX_IMAGES] = {};
    VkImageView sc_view[MAX_IMAGES] = {};
    VkFramebuffer sc_fb[MAX_IMAGES] = {};
    VkRenderPass rp_comp = VK_NULL_HANDLE;
    VkPipeline pipe_comp = VK_NULL_HANDLE;
    VkPipelineLayout layout_comp = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkSampler samp = VK_NULL_HANDLE;
    SRWLOCK lock = SRWLOCK_INIT;          // the overlay's and the swap chain's objects: used (shared) against made again (exclusive)
    volatile LONG visible = 0;            // the overlay holds something to put over the picture
};
Vk s;

#define VKF s.f

bool fail(const char* what, VkResult r)
{
    fprintf(stderr, "[menu] Vulkan: %s failed (VkResult %d)\n", what, (int)r);
    return false;
}

bool memory_type(uint32_t bits, VkMemoryPropertyFlags want, uint32_t* out)
{
    for (uint32_t i = 0; i < s.mem.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (s.mem.memoryTypes[i].propertyFlags & want) == want) { *out = i; return true; }
    return false;
}

// Waits for every command buffer of the menu's own that may still run (the overlay's draws).
void wait_own()
{
    for (int i = 0; i < RING; i++)
        if (s.busy[i]) { VKF.vkWaitForFences(s.dev, 1, &s.fence[i], VK_TRUE, UINT64_MAX); s.busy[i] = false; }
}

// ---- the overlay -------------------------------------------------------------------------------------

// Cleared to transparent, drawn by Dear ImGui, then read by the composite pass (any later submission:
// the dependencies to and from "outside" cover the other threads' command buffers too).
bool overlay_pass_create()
{
    VkAttachmentDescription a = {};
    a.format = VK_FORMAT_R8G8B8A8_UNORM;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp = {};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;
    VkSubpassDependency dep[2] = {};
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;   // after the last composite read it
    dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[0].srcAccessMask = 0;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    dep[1].srcSubpass = 0;                     // before the next composite reads it
    dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo ci = {}; ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1; ci.pAttachments = &a;
    ci.subpassCount = 1; ci.pSubpasses = &sp;
    ci.dependencyCount = 2; ci.pDependencies = dep;
    const VkResult r = VKF.vkCreateRenderPass(s.dev, &ci, nullptr, &s.rp_overlay);
    return r == VK_SUCCESS || fail("the overlay's render pass", r);
}

void overlay_destroy()
{
    if (s.ov_fb) VKF.vkDestroyFramebuffer(s.dev, s.ov_fb, nullptr);
    if (s.ov_view) VKF.vkDestroyImageView(s.dev, s.ov_view, nullptr);
    if (s.ov_img) VKF.vkDestroyImage(s.dev, s.ov_img, nullptr);
    if (s.ov_mem) VKF.vkFreeMemory(s.dev, s.ov_mem, nullptr);
    s.ov_fb = VK_NULL_HANDLE; s.ov_view = VK_NULL_HANDLE; s.ov_img = VK_NULL_HANDLE; s.ov_mem = VK_NULL_HANDLE;
    s.ow = s.oh = 0;
}

bool overlay_create(uint32_t w, uint32_t h)
{
    VkImageCreateInfo ii = {}; ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = { w, h, 1 };
    ii.mipLevels = 1; ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = VKF.vkCreateImage(s.dev, &ii, nullptr, &s.ov_img);
    if (r != VK_SUCCESS) return fail("the overlay image", r);
    VkMemoryRequirements req;
    VKF.vkGetImageMemoryRequirements(s.dev, s.ov_img, &req);
    VkMemoryAllocateInfo ai = {}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    if (!memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ai.memoryTypeIndex) &&
        !memory_type(req.memoryTypeBits, 0, &ai.memoryTypeIndex)) { overlay_destroy(); return fail("the overlay's memory type", VK_ERROR_UNKNOWN); }
    r = VKF.vkAllocateMemory(s.dev, &ai, nullptr, &s.ov_mem);
    if (r == VK_SUCCESS) r = VKF.vkBindImageMemory(s.dev, s.ov_img, s.ov_mem, 0);
    if (r != VK_SUCCESS) { overlay_destroy(); return fail("the overlay's memory", r); }
    VkImageViewCreateInfo vi = {}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = s.ov_img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    r = VKF.vkCreateImageView(s.dev, &vi, nullptr, &s.ov_view);
    if (r != VK_SUCCESS) { overlay_destroy(); return fail("the overlay's view", r); }
    VkFramebufferCreateInfo fi = {}; fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = s.rp_overlay;
    fi.attachmentCount = 1; fi.pAttachments = &s.ov_view;
    fi.width = w; fi.height = h; fi.layers = 1;
    r = VKF.vkCreateFramebuffer(s.dev, &fi, nullptr, &s.ov_fb);
    if (r != VK_SUCCESS) { overlay_destroy(); return fail("the overlay's framebuffer", r); }
    s.ow = w; s.oh = h;
    // the composite pass reads it through the one descriptor set
    VkDescriptorImageInfo di = { s.samp, s.ov_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet wr = {}; wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = s.set; wr.dstBinding = 0; wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &di;
    VKF.vkUpdateDescriptorSets(s.dev, 1, &wr, 0, nullptr);
    return true;
}

// ---- the composite pass ---------------------------------------------------------------------------------

bool composite_objects_create()
{
    VkSamplerCreateInfo si = {}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    VkResult r = VKF.vkCreateSampler(s.dev, &si, nullptr, &s.samp);
    if (r != VK_SUCCESS) return fail("the overlay's sampler", r);
    VkDescriptorSetLayoutBinding b = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li = {}; li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1; li.pBindings = &b;
    r = VKF.vkCreateDescriptorSetLayout(s.dev, &li, nullptr, &s.dsl);
    if (r != VK_SUCCESS) return fail("the composite's set layout", r);
    VkPipelineLayoutCreateInfo pli = {}; pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &s.dsl;
    r = VKF.vkCreatePipelineLayout(s.dev, &pli, nullptr, &s.layout_comp);
    if (r != VK_SUCCESS) return fail("the composite's pipeline layout", r);
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo dpi = {}; dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1; dpi.poolSizeCount = 1; dpi.pPoolSizes = &ps;
    r = VKF.vkCreateDescriptorPool(s.dev, &dpi, nullptr, &s.dpool);
    if (r != VK_SUCCESS) return fail("the composite's descriptor pool", r);
    VkDescriptorSetAllocateInfo ai = {}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = s.dpool; ai.descriptorSetCount = 1; ai.pSetLayouts = &s.dsl;
    r = VKF.vkAllocateDescriptorSets(s.dev, &ai, &s.set);
    return r == VK_SUCCESS || fail("the composite's descriptor set", r);
}

// The pass over a swap chain image: it comes from the frame's copy (TRANSFER_DST_OPTIMAL), keeps what
// is there, takes the overlay over it, and is left ready to present.
bool composite_pass_create(VkFormat fmt)
{
    VkAttachmentDescription a = {};
    a.format = fmt;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    a.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp = {};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;
    VkSubpassDependency dep[2] = {};
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;   // after the frame's copy into the image and the overlay's draw
    dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    dep[1].srcSubpass = 0;
    dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].dstAccessMask = 0;
    VkRenderPassCreateInfo ci = {}; ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1; ci.pAttachments = &a;
    ci.subpassCount = 1; ci.pSubpasses = &sp;
    ci.dependencyCount = 2; ci.pDependencies = dep;
    VkResult r = VKF.vkCreateRenderPass(s.dev, &ci, nullptr, &s.rp_comp);
    if (r != VK_SUCCESS) return fail("the composite's render pass", r);

    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo mi = {}; mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    mi.codeSize = sizeof k_composite_vs; mi.pCode = k_composite_vs;
    r = VKF.vkCreateShaderModule(s.dev, &mi, nullptr, &vs);
    if (r == VK_SUCCESS) {
        mi.codeSize = sizeof k_composite_fs; mi.pCode = k_composite_fs;
        r = VKF.vkCreateShaderModule(s.dev, &mi, nullptr, &fs);
    }
    if (r == VK_SUCCESS) {
        VkPipelineShaderStageCreateInfo st[2] = {};
        st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vs; st[0].pName = "main";
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vin = {}; vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia = {}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp = {}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs = {}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms = {}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba = {};
        ba.blendEnable = VK_TRUE;
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.colorBlendOp = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.alphaBlendOp = VK_BLEND_OP_ADD;
        ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb = {}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &ba;
        const VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo ds = {}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
        VkGraphicsPipelineCreateInfo pi = {}; pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2; pi.pStages = st;
        pi.pVertexInputState = &vin; pi.pInputAssemblyState = &ia; pi.pViewportState = &vp;
        pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pColorBlendState = &cb; pi.pDynamicState = &ds;
        pi.layout = s.layout_comp; pi.renderPass = s.rp_comp; pi.subpass = 0;
        r = VKF.vkCreateGraphicsPipelines(s.dev, VK_NULL_HANDLE, 1, &pi, nullptr, &s.pipe_comp);
    }
    if (vs) VKF.vkDestroyShaderModule(s.dev, vs, nullptr);
    if (fs) VKF.vkDestroyShaderModule(s.dev, fs, nullptr);
    if (r != VK_SUCCESS) return fail("the composite's pipeline", r);
    s.sc_fmt = fmt;
    return true;
}

void composite_pass_destroy()
{
    if (s.pipe_comp) VKF.vkDestroyPipeline(s.dev, s.pipe_comp, nullptr);
    if (s.rp_comp) VKF.vkDestroyRenderPass(s.dev, s.rp_comp, nullptr);
    s.pipe_comp = VK_NULL_HANDLE; s.rp_comp = VK_NULL_HANDLE;
    s.sc_fmt = VK_FORMAT_UNDEFINED;
}

void swapchain_release()
{
    for (uint32_t i = 0; i < MAX_IMAGES; i++) {
        if (s.sc_fb[i]) VKF.vkDestroyFramebuffer(s.dev, s.sc_fb[i], nullptr);
        if (s.sc_view[i]) VKF.vkDestroyImageView(s.dev, s.sc_view[i], nullptr);
        s.sc_fb[i] = VK_NULL_HANDLE; s.sc_view[i] = VK_NULL_HANDLE; s.sc_img[i] = VK_NULL_HANDLE;
    }
    s.sc_n = 0;
}

// ---- Dear ImGui -----------------------------------------------------------------------------------------

PFN_vkVoidFunction imgui_loader(const char* name, void*) { return s.gipa(s.inst, name); }
void vk_new_frame() { ImGui_ImplVulkan_NewFrame(); }
void check_result(VkResult r) { if (r < 0) fprintf(stderr, "[menu] Vulkan: Dear ImGui's backend got VkResult %d\n", (int)r); }

void objects_destroy()
{
    swapchain_release();
    composite_pass_destroy();
    overlay_destroy();
    for (int i = 0; i < RING; i++) if (s.fence[i]) VKF.vkDestroyFence(s.dev, s.fence[i], nullptr);
    if (s.pool) VKF.vkDestroyCommandPool(s.dev, s.pool, nullptr);
    if (s.dpool) VKF.vkDestroyDescriptorPool(s.dev, s.dpool, nullptr);
    if (s.layout_comp) VKF.vkDestroyPipelineLayout(s.dev, s.layout_comp, nullptr);
    if (s.dsl) VKF.vkDestroyDescriptorSetLayout(s.dev, s.dsl, nullptr);
    if (s.samp) VKF.vkDestroySampler(s.dev, s.samp, nullptr);
    if (s.rp_overlay) VKF.vkDestroyRenderPass(s.dev, s.rp_overlay, nullptr);
}

}  // namespace
}  // namespace gt5m

using namespace gt5m;

// ---- the API (gt5_menu_vk.h) ------------------------------------------------------------------------------

extern "C" int gt5_menu_vk_init(const gt5_menu_vk_init_info* info)
{
    if (s.inited) return 1;
    if (core_inited()) return 0;              // the menu is already drawn by the other renderer
    if (!info || !info->hwnd || !info->instance || !info->physical_device || !info->device || !info->queue ||
        !info->get_instance_proc_addr || !info->queue_lock) return 0;
    s.inst = info->instance; s.phys = info->physical_device; s.dev = info->device;
    s.queue = info->queue; s.family = info->queue_family;
    s.gipa = info->get_instance_proc_addr; s.qlock = info->queue_lock;
    bool ok = true;
#define GT5M_VK_LOAD(name) if (!(s.f.name = (PFN_##name)s.gipa(s.inst, #name))) { fprintf(stderr, "[menu] Vulkan: no " #name "\n"); ok = false; }
    GT5M_VK_FNS(GT5M_VK_LOAD)
#undef GT5M_VK_LOAD
    if (!ok) { s = Vk(); fprintf(stderr, "[menu] Vulkan: the settings menu is off\n"); return 0; }
    VKF.vkGetPhysicalDeviceMemoryProperties(s.phys, &s.mem);
    core_begin((HWND)info->hwnd, info->width, info->height, info->version, info->title);
    s.laid_w = info->width; s.laid_h = info->height;

    VkCommandPoolCreateInfo pci = {}; pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = s.family;
    ok = VKF.vkCreateCommandPool(s.dev, &pci, nullptr, &s.pool) == VK_SUCCESS;
    if (ok) {
        VkCommandBufferAllocateInfo cai = {}; cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = s.pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = RING;
        ok = VKF.vkAllocateCommandBuffers(s.dev, &cai, s.cb) == VK_SUCCESS;
    }
    VkFenceCreateInfo fci = {}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    for (int i = 0; ok && i < RING; i++) ok = VKF.vkCreateFence(s.dev, &fci, nullptr, &s.fence[i]) == VK_SUCCESS;
    ok = ok && overlay_pass_create() && composite_objects_create();
    if (!ok) {
        fprintf(stderr, "[menu] Vulkan objects could not be made: the settings menu is off\n");
        objects_destroy();
        s = Vk();
        return 0;
    }

    core_context();
    ImGui_ImplVulkan_InitInfo ii = {};
    ii.ApiVersion = VK_API_VERSION_1_0;       // the renderer's (it uses Vulkan 1.0 only)
    ii.Instance = s.inst;
    ii.PhysicalDevice = s.phys;
    ii.Device = s.dev;
    ii.QueueFamily = s.family;
    ii.Queue = s.queue;
    ii.DescriptorPoolSize = 16;               // the font atlas (and any atlas it grows into)
    ii.MinImageCount = 2;
    ii.ImageCount = RING;                     // its vertex buffers turn over with the menu's command buffers
    ii.PipelineInfoMain.RenderPass = s.rp_overlay;
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.CheckVkResultFn = check_result;
    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0, imgui_loader, nullptr) || !ImGui_ImplVulkan_Init(&ii)) {
        fprintf(stderr, "[menu] Dear ImGui's Vulkan backend did not start: the settings menu is off\n");
        core_context_abort();
        objects_destroy();
        s = Vk();
        return 0;
    }
    s.inited = true;
    core_finish(vk_new_frame, "Vulkan");
    return 1;
}

extern "C" void gt5_menu_vk_shutdown(void)
{
    if (!s.inited) return;
    AcquireSRWLockExclusive(&s.lock);
    s.inited = false;
    InterlockedExchange(&s.visible, 0);
    core_shutdown_begin();
    wait_own();
    ImGui_ImplVulkan_Shutdown();
    core_shutdown_end();
    objects_destroy();
    ReleaseSRWLockExclusive(&s.lock);
    s = Vk();
}

extern "C" void gt5_menu_vk_swapchain(VkFormat format, uint32_t width, uint32_t height, uint32_t count, const VkImage* images)
{
    if (!s.inited) return;
    AcquireSRWLockExclusive(&s.lock);        // no composite and no draw into the overlay while this runs
    wait_own();
    swapchain_release();
    if (!count || !images || !width || !height) {
        ReleaseSRWLockExclusive(&s.lock);
        return;
    }
    if (count > MAX_IMAGES) count = MAX_IMAGES;
    bool ok = true;
    if (format != s.sc_fmt) {
        composite_pass_destroy();
        ok = composite_pass_create(format);
    }
    for (uint32_t i = 0; ok && i < count; i++) {
        VkImageViewCreateInfo vi = {}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkResult r = VKF.vkCreateImageView(s.dev, &vi, nullptr, &s.sc_view[i]);
        if (r == VK_SUCCESS) {
            VkFramebufferCreateInfo fi = {}; fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fi.renderPass = s.rp_comp;
            fi.attachmentCount = 1; fi.pAttachments = &s.sc_view[i];
            fi.width = width; fi.height = height; fi.layers = 1;
            r = VKF.vkCreateFramebuffer(s.dev, &fi, nullptr, &s.sc_fb[i]);
        }
        if (r != VK_SUCCESS) ok = fail("the swap chain image's view", r);
        s.sc_img[i] = images[i];
    }
    if (ok && (width != s.ow || height != s.oh)) {
        InterlockedExchange(&s.visible, 0);  // the new overlay holds nothing until it is drawn
        overlay_destroy();
        ok = overlay_create(width, height);
    }
    if (ok) { s.sc_w = width; s.sc_h = height; s.sc_n = count; }
    else    { swapchain_release(); fprintf(stderr, "[menu] Vulkan: nothing put over the picture until the next swap chain\n"); }
    ReleaseSRWLockExclusive(&s.lock);
}

extern "C" int gt5_menu_vk_frame(void)
{
    if (!s.inited) return 0;
    AcquireSRWLockShared(&s.lock);
    if (!s.ov_img) {
        // Nothing to draw into (no swap chain yet, or its overlay couldn't be made): the menu doesn't stay
        // open unseen with the game's input held.
        ReleaseSRWLockShared(&s.lock);
        InterlockedExchange(&s.visible, 0);
        if (gt5_menu_is_open()) gt5_menu_set_open(0);
        return 0;
    }
    // The layout is in the window's pixels and drawn at the overlay's: tell the shared part when the
    // overlay's size changed (a new swap chain, made on whichever thread presented).
    if (s.ow != s.laid_w || s.oh != s.laid_h) { core_set_size(s.ow, s.oh); s.laid_w = s.ow; s.laid_h = s.oh; }
    if (!core_build_frame()) { ReleaseSRWLockShared(&s.lock); InterlockedExchange(&s.visible, 0); return 0; }
    ImDrawData* dd = ImGui::GetDrawData();

    const unsigned k = s.ring++ % RING;
    if (s.busy[k]) { VKF.vkWaitForFences(s.dev, 1, &s.fence[k], VK_TRUE, UINT64_MAX); s.busy[k] = false; }
    // New glyphs: Dear ImGui's backend copies them into its atlas with a submission of its own and waits
    // for the queue. The queue is the renderer's, so that happens here, under its lock (the draw below
    // then finds every texture up to date).
    if (dd->Textures) {
        bool any = false;
        for (ImTextureData* tex : *dd->Textures) if (tex->Status != ImTextureStatus_OK) any = true;
        if (any) {
            s.qlock(1);
            for (ImTextureData* tex : *dd->Textures) if (tex->Status != ImTextureStatus_OK) ImGui_ImplVulkan_UpdateTexture(tex);
            s.qlock(0);
        }
    }
    VkCommandBuffer cb = s.cb[k];
    VkCommandBufferBeginInfo bi = {}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    bool ok = VKF.vkResetCommandBuffer(cb, 0) == VK_SUCCESS && VKF.vkBeginCommandBuffer(cb, &bi) == VK_SUCCESS;
    if (ok) {
        VkClearValue clear = {};
        VkRenderPassBeginInfo rb = {}; rb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rb.renderPass = s.rp_overlay;
        rb.framebuffer = s.ov_fb;
        rb.renderArea.extent = { s.ow, s.oh };
        rb.clearValueCount = 1; rb.pClearValues = &clear;
        VKF.vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(dd, cb);
        VKF.vkCmdEndRenderPass(cb);
        ok = VKF.vkEndCommandBuffer(cb) == VK_SUCCESS;
    }
    if (ok) {
        VkSubmitInfo si = {}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        VKF.vkResetFences(s.dev, 1, &s.fence[k]);
        s.qlock(1);
        const VkResult r = VKF.vkQueueSubmit(s.queue, 1, &si, s.fence[k]);
        s.qlock(0);
        ok = r == VK_SUCCESS;
        if (ok) s.busy[k] = true; else fail("the menu's submission", r);
    }
    ReleaseSRWLockShared(&s.lock);
    InterlockedExchange(&s.visible, ok ? 1 : 0);
    return ok ? 1 : 0;
}

extern "C" int gt5_menu_vk_composite(VkCommandBuffer cb, uint32_t image_index)
{
    if (!cb || !s.visible) return 0;
    AcquireSRWLockShared(&s.lock);
    const bool can = s.inited && s.visible && s.pipe_comp && image_index < s.sc_n && s.sc_fb[image_index] &&
                     s.ow == s.sc_w && s.oh == s.sc_h;
    if (can) {
        VkRenderPassBeginInfo rb = {}; rb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rb.renderPass = s.rp_comp;
        rb.framebuffer = s.sc_fb[image_index];
        rb.renderArea.extent = { s.sc_w, s.sc_h };
        VKF.vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport vp = { 0.0f, 0.0f, (float)s.sc_w, (float)s.sc_h, 0.0f, 1.0f };
        const VkRect2D sc = { { 0, 0 }, { s.sc_w, s.sc_h } };
        VKF.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipe_comp);
        VKF.vkCmdSetViewport(cb, 0, 1, &vp);
        VKF.vkCmdSetScissor(cb, 0, 1, &sc);
        VKF.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s.layout_comp, 0, 1, &s.set, 0, nullptr);
        VKF.vkCmdDraw(cb, 3, 1, 0, 0);
        VKF.vkCmdEndRenderPass(cb);
    }
    ReleaseSRWLockShared(&s.lock);
    return can ? 1 : 0;
}
