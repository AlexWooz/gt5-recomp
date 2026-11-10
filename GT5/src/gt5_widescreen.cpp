// Ultrawide (wider than 16:9): GT5's own view setup, given the wider shape.
//
// GT5 draws for the shape of the TV. At start-up it reads the PS3's TV setting and hands its view
// setup a shape constant (16:9, or 4:3 for an old TV). The 3D cameras widen their view to that
// shape, keeping the height (Hor+), and the 2D layer (HUD, menus) keeps its 16:9 layout centred
// in it. The picture stays 1280x720, squeezed sideways, ready to be stretched by the TV. So a wider
// screen needs only those constants changed; the renderer then shows the picture across the wider
// window (display_aspect.c, rsx_live_draw.c).
//
// PS3_ASPECT or PS3_WINDOW_SIZE (play.bat) choose the shape. At 16:9, the default, nothing here
// touches the game.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" unsigned char* vm_base;
extern "C" float ps3_display_aspect(void);
extern "C" int ps3_display_wide(void);
extern "C" void ps3_on_loaded(void (*fn)(void));

namespace {

const uint32_t kShape16x9 = 0x3FE38E39;  // 16/9 as a big-endian float

struct ShapeConstant {
    uint32_t ea;
    const char* use;
};

// Europe BCES00569, disc executable 01.00. Each holds 16/9; the code reading it is noted.
const ShapeConstant kEurope0100[] = {
    {0x0186E2CC, "screen setup, picked when the TV setting is 16:9 (0x008CE594)"},
    {0x0186F910, "view setup for 3D scenes (0x0092D358, 0x0092D3BC, 0x0092E388)"},
};

uint32_t rd32(uint32_t ea)
{
    uint32_t v;
    memcpy(&v, vm_base + ea, 4);
    return __builtin_bswap32(v);
}

void wr32(uint32_t ea, uint32_t v)
{
    v = __builtin_bswap32(v);
    memcpy(vm_base + ea, &v, 4);
}

void gt5_widescreen_apply()
{
    if (!ps3_display_wide()) return;
    const float aspect = ps3_display_aspect();
    for (const ShapeConstant& c : kEurope0100) {
        if (rd32(c.ea) != kShape16x9) {
            // Another release (or a changed executable): its constants live elsewhere.
            fprintf(stderr, "[widescreen] this executable has no known shape constants (0x%08X holds 0x%08X);"
                            " the game stays 16:9, stretched to the window\n", c.ea, rd32(c.ea));
            return;
        }
    }
    uint32_t bits;
    memcpy(&bits, &aspect, 4);
    for (const ShapeConstant& c : kEurope0100) wr32(c.ea, bits);
    fprintf(stderr, "[widescreen] game shape %.4f (16:9 = 1.7778): %u constants set\n", aspect,
            (unsigned)(sizeof kEurope0100 / sizeof kEurope0100[0]));
}

const int s_registered = (ps3_on_loaded(gt5_widescreen_apply), 0);

}  // namespace
