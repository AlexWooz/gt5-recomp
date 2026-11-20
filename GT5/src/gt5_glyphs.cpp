// Button symbols: an Xbox pad's or the keyboard's symbols in GT5's own menus and button bars, in place of the
// PlayStation ones. GT5_BUTTON_SYMBOLS (Controls page and the launcher): "" as in the game (the default: nothing
// here runs), "xbox", "keys" (the keys bound now, PS3_KEYS), "auto" (the device used last; a PlayStation
// controller gets the game's own).
//
// How GT5 draws them: each symbol is a small texture of its own (or a cell of a bigger picture) that its UI
// samples like any other picture; the help bars' face buttons are 36x36 A8R8G8B8 pictures (Project Tourline 5
// found them on US 1.04; the table below has ours by content). Its text has no button symbols of its own.
//
// What this does: before each draw (rsx_dispatch.c g_rsx_draw_textures_hook, set only once the setting has
// been something else than "As in the game" in this run), each texture the draw samples whose size is one of
// the table's is checked against the table by a hash of its top level: the same hash the texture dump names
// its files by (rsx_texpack.c), so a dumped file's name is its table key. A match is painted over in guest
// memory, in the texture's own format and layout, with the symbol for the button the cell shows
// (gt5_glyph_art.c: drawn in code). The renderers then see a texture the game rewrote and upload it again,
// so DirectX 12, Vulkan and the other platforms all show it, with no renderer code. The game's own bytes are
// kept: "As in the game" again (or a PlayStation pad in Automatic) puts them back the same way. A texture whose
// bytes are not in the table is never touched, so a release with other pictures simply keeps its own.
//
// The Xbox symbols follow cellPad's mapping (A Cross, B Circle, X Square, Y Triangle, bumpers L1 / R1,
// triggers L2 / R2, and with PAD_TRIGGERS=drive while the game drives, cellPad_symbol_xbox's layout); the
// keyboard's name the first key bound to the button. A button nothing presses keeps the game's symbol.
//
// Tests: GT5_GLYPH_LOG=1 logs every small texture the draws sample (hash, size, format, remap, address),
// once each, to find more symbol pictures; GT5_GLYPH_TABLE=<file> adds table lines
// ("<hash> <w>x<h> <button>[@x,y,w,h[/x,y,w,h]] ...", buttons cross circle square triangle l1 r1 l2 r2 l3 r3 start
// select up down left right). Each picture painted over is one "[glyphs]" line in the log.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "rsx_dispatch.h"
#include "rsx_texture_layout.h"
#include "gt5_glyph_art.h"

extern "C" {
extern uint8_t* vm_base;
extern uint32_t ppu_vm_size;
uint32_t cellGcmResolveLocated(int local, uint32_t offset);
void ps3_on_loaded(void (*fn)(void));
// The settings registry (runtime/settings/gt5_settings.h).
size_t gt5_setting_str(const char* key, char* buf, size_t cap);
extern volatile unsigned g_gt5_settings_gen;
void gt5_settings_on_change(const char* key,
                            void (*fn)(const char* key, const char* old_value, const char* new_value, void* user),
                            void* user);
// libs/input/cellPad.h, button symbols.
int cellPad_symbol_device(void);
int cellPad_symbol_xbox(unsigned bit);
int cellPad_symbol_key(unsigned bit, char* buf, int cap);
unsigned cellPad_symbol_gen(void);
}

namespace {

// cellPad.h's values.
enum { DEV_KEYBOARD = 1, DEV_XBOX = 2, DEV_PLAYSTATION = 3 };
enum { SYM_A = 1, SYM_B, SYM_X, SYM_Y, SYM_LB, SYM_RB, SYM_LT, SYM_RT, SYM_LS, SYM_RS, SYM_VIEW, SYM_MENU, SYM_DPAD };

// The PS3 buttons (CELL_PAD_CTRL_* bits) and their names in table lines.
const struct { const char* name; uint16_t bit; } kButtons[16] = {
    { "select", 1u << 0 }, { "l3", 1u << 1 }, { "r3", 1u << 2 }, { "start", 1u << 3 },
    { "up", 1u << 4 }, { "right", 1u << 5 }, { "down", 1u << 6 }, { "left", 1u << 7 },
    { "l2", 1u << 8 }, { "r2", 1u << 9 }, { "l1", 1u << 10 }, { "r1", 1u << 11 },
    { "triangle", 1u << 12 }, { "circle", 1u << 13 }, { "cross", 1u << 14 }, { "square", 1u << 15 },
};

enum Style { STYLE_GAME = 0, STYLE_XBOX = 1, STYLE_KEYS = 2 };
enum Choice { CHOICE_GAME = 0, CHOICE_XBOX, CHOICE_KEYS, CHOICE_AUTO };

struct Cell {
    uint16_t bit;                 // the button it shows
    int16_t x, y, w, h;           // its part of the picture in texels; w == 0: the whole picture
    int16_t fx, fy, fw, fh;       // the box the new symbol goes in; fw == 0: the button's own, less 2 texels
};

// The symbol pictures, by the texture-pack hash of their top level (rsx_texpack_hash: the format's base and
// linear bits, size, and for anything not DXT the remap). From texture dumps of the menus (PS3_TEX_DUMP=1).
struct Known {
    uint64_t hash;
    uint16_t w, h;
    const char* what;
    Cell cells[16];
};
#define B_SELECT (1u << 0)
#define B_START (1u << 3)
#define B_UP (1u << 4)
#define B_RIGHT (1u << 5)
#define B_DOWN (1u << 6)
#define B_LEFT (1u << 7)
#define B_L2 (1u << 8)
#define B_R2 (1u << 9)
#define B_L1 (1u << 10)
#define B_R1 (1u << 11)
#define B_TRIANGLE (1u << 12)
#define B_CIRCLE (1u << 13)
#define B_CROSS (1u << 14)
#define B_SQUARE (1u << 15)
const Known kKnown[] = {
    // Europe 1.00, texture dump of the menus (19 Nov 2026). The help bars' face buttons ("Cross Accept /
    // Circle Cancel"): 36x36 A8R8G8B8, one button each.
    { 0x6A857D6529BBBDF6ull, 36, 36, "Cross symbol", { { B_CROSS } } },
    { 0x29E451C2291E686Eull, 36, 36, "Circle symbol", { { B_CIRCLE } } },
    { 0xA2561DAA234DBD49ull, 36, 36, "Triangle symbol", { { B_TRIANGLE } } },
    // Options > Hardware > Change Button Assignments: the controller picture, 460x720 DXT5 (the same layout
    // Project Tourline 5 measured on Prologue). Each cell is its button and a little around it; the d-pad arms'
    // and the face buttons' boxes overlap their neighbours', so the symbol goes in a box of its own there.
    // The sticks and their arrows keep the game's picture; so do the printed SELECT / START.
    { 0xDA32F417898BCF38ull, 460, 720, "controller picture", {
        { B_L2, 64, 61, 119, 76 }, { B_R2, 277, 61, 119, 76 }, { B_L1, 64, 136, 119, 55 }, { B_R1, 277, 136, 119, 55 },
        { B_UP, 96, 227, 55, 62, 100, 231, 47, 38 }, { B_DOWN, 96, 306, 55, 62, 100, 327, 47, 38 },
        { B_LEFT, 54, 270, 61, 57, 58, 275, 38, 47 }, { B_RIGHT, 132, 270, 61, 57, 151, 275, 38, 47 },
        { B_TRIANGLE, 309, 228, 55, 57, 315, 234, 43, 45 }, { B_SQUARE, 268, 270, 55, 57, 274, 276, 43, 45 },
        { B_CIRCLE, 350, 270, 55, 57, 356, 276, 43, 45 }, { B_CROSS, 309, 311, 55, 57, 315, 317, 43, 45 },
        { B_SELECT, 138, 437, 49, 35 }, { B_START, 273, 435, 55, 39 } } },
};

struct Art {
    uint32_t key = 0;
    uint64_t hash = 0;
    std::vector<uint8_t> bytes;
};

struct Entry {
    uint64_t hash;
    uint32_t w, h;
    char what[48];
    std::vector<Cell> cells;
    std::vector<uint8_t> orig;    // the game's bytes, every level, once seen
    uint32_t orig_fmt = 0, orig_remap = 0, orig_pitch = 0, orig_levels = 0;
    Art art[4];
    int art_next = 0;
    bool said = false;
};

std::vector<Entry> g_entries;
uint32_t g_sizes[32];   // w << 16 | h of the entries
int g_nsizes;
bool g_log;             // GT5_GLYPH_LOG=1
bool g_installed;

// What is wanted now (the hook's thread only).
int g_choice = CHOICE_GAME;
int g_style = STYLE_GAME;
uint32_t g_key = 0;                      // changes with everything that changes the pictures
int g_xbox[16];                          // cellPad_symbol_xbox of each button
char g_keyname[16][16];                  // cellPad_symbol_key of each button
unsigned g_seen_settings = ~0u, g_seen_pad = ~0u;
int g_seen_dev = -1;

// ---- the texture-pack hash (rsx_texpack.c: never change one without the other) --------------------------

uint64_t hash_run(const uint8_t* p, uint32_t n, uint64_t seed)
{
    uint64_t a = seed ^ 0x9E3779B97F4A7C15ull, b = seed + 0xC2B2AE3D27D4EB4Full,
             c = seed ^ 0x165667B19E3779F9ull, d = seed - 0x27D4EB2F165667C5ull;
    uint32_t i = 0;
    for (; i + 32 <= n; i += 32) {
        uint64_t w0, w1, w2, w3;
        memcpy(&w0, p + i, 8); memcpy(&w1, p + i + 8, 8); memcpy(&w2, p + i + 16, 8); memcpy(&w3, p + i + 24, 8);
        a = (a ^ w0) * 0x9E3779B97F4A7C15ull; a ^= a >> 29;
        b = (b ^ w1) * 0xC2B2AE3D27D4EB4Full; b ^= b >> 29;
        c = (c ^ w2) * 0x165667B19E3779F9ull; c ^= c >> 29;
        d = (d ^ w3) * 0xD6E8FEB86659FD93ull; d ^= d >> 29;
    }
    for (; i + 8 <= n; i += 8) { uint64_t w; memcpy(&w, p + i, 8); a = (a ^ w) * 0x9E3779B97F4A7C15ull; a ^= a >> 29; }
    for (; i < n; i++) { b = (b ^ p[i]) * 0xC2B2AE3D27D4EB4Full; b ^= b >> 29; }
    uint64_t h = a ^ (b * 31u) ^ (c * 961u) ^ (d * 29791u) ^ (uint64_t)n;
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return h;
}

// A texture's shape as the texture dump sees it (rsx_live_draw.c ld_tp_top), and how many bytes its levels
// take. False for what has no name there (cube, 3D, bordered, float, depth textures).
struct Shape {
    uint32_t base, linear, block, texel, row, rows, pitch;   // top level: rows of row bytes, pitch apart
    uint32_t levels, span;                                    // every level
    rsx_tex_level lv[RSX_MAX_TEXTURE_LEVELS];
};

bool shape_of(const rsx_dsp_texture& t, Shape& s)
{
    if (t.cubemap || t.border || t.dimension != 2 || t.depth > 1) return false;
    const uint32_t w = t.width, h = t.height;
    if (!w || !h || w > 1024 || h > 1024) return false;
    s.base = t.format & RSX_TEX_FMT_BASE_MASK;
    s.linear = t.format & RSX_TEX_FMT_LINEAR;
    s.block = 0; s.texel = 0;
    switch (s.base) {
    case RSX_TEX_FMT_DXT1: s.block = 8; break;
    case RSX_TEX_FMT_DXT23: case RSX_TEX_FMT_DXT45: s.block = 16; break;
    case RSX_TEX_FMT_A4R4G4B4: case RSX_TEX_FMT_A1R5G5B5: s.texel = 2; break;
    case RSX_TEX_FMT_A8R8G8B8: s.texel = 4; break;
    default: return false;   // what can carry a coloured symbol
    }
    if (s.block) {
        s.row = ((w + 3) / 4) * s.block; s.rows = (h + 3) / 4; s.pitch = s.row;
    } else if (s.linear) {
        s.row = w * s.texel; s.rows = h; s.pitch = t.pitch ? t.pitch : s.row;
        if (s.pitch < s.row) return false;
    } else {
        if ((w & (w - 1)) || (h & (h - 1))) return false;
        s.row = w * h * s.texel; s.rows = 1; s.pitch = s.row;
    }
    s.levels = rsx_texture_mip_chain(t.format, w, h, t.mipmaps ? t.mipmaps : 1, s.linear ? s.pitch : 0, s.lv);
    if (!s.levels) return false;
    s.span = s.lv[s.levels - 1].offset + s.lv[s.levels - 1].tl.face_bytes;
    const uint32_t top = s.pitch * (s.rows - 1) + s.row;
    if (s.span < top) s.span = top;
    return true;
}

uint64_t tex_hash(const rsx_dsp_texture& t, const Shape& s, const uint8_t* src)
{
    const uint32_t tag = t.format & (RSX_TEX_FMT_BASE_MASK | RSX_TEX_FMT_LINEAR);
    const uint32_t remap = s.block ? 0u : (t.remap & 0xFFFFu);
    uint64_t x = 0x5450585445583130ull ^ ((uint64_t)(tag & 0xFF) << 56) ^ ((uint64_t)t.width << 36) ^
                 ((uint64_t)t.height << 16) ^ (uint64_t)(remap & 0xFFFF);
    if (s.rows <= 1 || s.pitch == s.row)
        x = hash_run(src, s.row * (s.rows ? s.rows : 1), x);
    else
        for (uint32_t y = 0; y < s.rows; y++) x = hash_run(src + (size_t)y * s.pitch, s.row, x);
    return x ? x : 1;
}

// A cheap look at the top level: 16 words spread over it.
uint64_t quick_sig(const uint8_t* src, uint32_t bytes)
{
    uint64_t h = 1469598103934665603ull ^ bytes;
    const uint32_t step = bytes >= 8 * 16 ? (bytes - 8) / 15 : 8;
    for (uint32_t i = 0; i + 8 <= bytes && i / step < 16; i += step) {
        uint64_t w;
        memcpy(&w, src + i, 8);
        h = (h ^ w) * 1099511628211ull;
    }
    return h;
}

const uint8_t* guest(const rsx_dsp_texture& t, uint32_t bytes)
{
    if (!vm_base || !bytes) return nullptr;
    const uint32_t ea = cellGcmResolveLocated(t.location == RSX_LOCATION_LOCAL, t.offset);
    if (!ea || (ppu_vm_size && (uint64_t)ea + bytes > ppu_vm_size)) return nullptr;
    return vm_base + ea;
}

// ---- pictures: decode a level to RGBA as the draw shows it (after the remap), and back --------------------

struct Img {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // RGBA, straight alpha
};

void rgb565(uint32_t c, int* r, int* g, int* b)
{
    const int r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
    *r = (r5 << 3) | (r5 >> 2); *g = (g6 << 2) | (g6 >> 4); *b = (b5 << 3) | (b5 >> 2);
}

// One 4x4 block of DXT1/3/5 to RGBA texels (the resource's channels).
void bc_block(uint32_t base, const uint8_t* b, uint8_t out[16][4])
{
    const uint8_t* col = base == RSX_TEX_FMT_DXT1 ? b : b + 8;
    const uint32_t c0 = col[0] | (col[1] << 8), c1 = col[2] | (col[3] << 8);
    int p[4][4];
    rgb565(c0, &p[0][0], &p[0][1], &p[0][2]);
    rgb565(c1, &p[1][0], &p[1][1], &p[1][2]);
    p[0][3] = p[1][3] = p[2][3] = 255;
    p[3][3] = 255;
    if (base != RSX_TEX_FMT_DXT1 || c0 > c1) {
        for (int k = 0; k < 3; k++) { p[2][k] = (2 * p[0][k] + p[1][k]) / 3; p[3][k] = (p[0][k] + 2 * p[1][k]) / 3; }
    } else {
        for (int k = 0; k < 3; k++) { p[2][k] = (p[0][k] + p[1][k]) / 2; p[3][k] = 0; }
        p[3][3] = 0;
    }
    const uint32_t ci = col[4] | (col[5] << 8) | (col[6] << 16) | ((uint32_t)col[7] << 24);
    for (int i = 0; i < 16; i++) {
        const int c = (ci >> (2 * i)) & 3;
        for (int k = 0; k < 4; k++) out[i][k] = (uint8_t)p[c][k];
    }
    if (base == RSX_TEX_FMT_DXT23) {
        for (int i = 0; i < 16; i++) out[i][3] = (uint8_t)(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
    } else if (base == RSX_TEX_FMT_DXT45) {
        int a[8]; a[0] = b[0]; a[1] = b[1];
        if (a[0] > a[1]) for (int i = 2; i < 8; i++) a[i] = ((8 - i) * a[0] + (i - 1) * a[1]) / 7;
        else { for (int i = 2; i < 6; i++) a[i] = ((6 - i) * a[0] + (i - 1) * a[1]) / 5; a[6] = 0; a[7] = 255; }
        uint64_t ai = 0;
        for (int i = 0; i < 6; i++) ai |= (uint64_t)b[2 + i] << (8 * i);
        for (int i = 0; i < 16; i++) out[i][3] = (uint8_t)a[(ai >> (3 * i)) & 7];
    }
}

uint16_t to565(const uint8_t* c)
{
    return (uint16_t)((((c[0] * 31 + 127) / 255) << 11) | (((c[1] * 63 + 127) / 255) << 5) | ((c[2] * 31 + 127) / 255));
}

// One 4x4 block of RGBA (the resource's channels) to DXT1/3/5. Flat art: the colour line through the two
// visible texels furthest apart; alpha by its own range (DXT5), 4 bits (DXT3) or a cut at half (DXT1).
void bc_encode(uint32_t base, const uint8_t px[16][4], uint8_t* out)
{
    uint8_t* col = base == RSX_TEX_FMT_DXT1 ? out : out + 8;
    const bool punch = base == RSX_TEX_FMT_DXT1;
    bool any_clear = false;
    if (base == RSX_TEX_FMT_DXT45) {
        int amin = 255, amax = 0;
        for (int i = 0; i < 16; i++) { amin = px[i][3] < amin ? px[i][3] : amin; amax = px[i][3] > amax ? px[i][3] : amax; }
        out[0] = (uint8_t)amax; out[1] = (uint8_t)amin;
        uint64_t ai = 0;
        if (amax > amin) {
            int pal[8]; pal[0] = amax; pal[1] = amin;
            for (int i = 2; i < 8; i++) pal[i] = ((8 - i) * amax + (i - 1) * amin) / 7;
            for (int i = 0; i < 16; i++) {
                int best = 0, bd = 1 << 30;
                for (int k = 0; k < 8; k++) { const int d = abs(pal[k] - px[i][3]); if (d < bd) { bd = d; best = k; } }
                ai |= (uint64_t)best << (3 * i);
            }
        }
        for (int i = 0; i < 6; i++) out[2 + i] = (uint8_t)(ai >> (8 * i));
    } else if (base == RSX_TEX_FMT_DXT23) {
        for (int i = 0; i < 8; i++)
            out[i] = (uint8_t)(((px[2 * i][3] + 8) / 17) | (((px[2 * i + 1][3] + 8) / 17) << 4));
    }
    int e0 = -1, e1 = -1, fd = -1;
    for (int i = 0; i < 16; i++) {
        if (px[i][3] < (punch ? 128 : 8)) { any_clear = true; continue; }
        if (e0 < 0) { e0 = e1 = i; fd = 0; }
        for (int j = i + 1; j < 16; j++) {
            if (px[j][3] < (punch ? 128 : 8)) continue;
            int d = 0;
            for (int k = 0; k < 3; k++) { const int t = px[i][k] - px[j][k]; d += t * t; }
            if (d > fd) { fd = d; e0 = i; e1 = j; }
        }
    }
    uint16_t c0 = 0, c1 = 0;
    if (e0 >= 0) { c0 = to565(px[e0]); c1 = to565(px[e1]); }
    // DXT1 with clear texels needs c0 <= c1 (three colours and clear); otherwise c0 > c1 keeps four colours.
    if (punch && any_clear) { if (c0 > c1) { const uint16_t t = c0; c0 = c1; c1 = t; } }
    else if (c0 < c1) { const uint16_t t = c0; c0 = c1; c1 = t; }
    else if (c0 == c1 && punch && !any_clear && c0 > 0) c1 = (uint16_t)(c0 - 1);   // keep four-colour mode
    int p[4][3];
    rgb565(c0, &p[0][0], &p[0][1], &p[0][2]);
    rgb565(c1, &p[1][0], &p[1][1], &p[1][2]);
    const bool three = punch && c0 <= c1;
    for (int k = 0; k < 3; k++) {
        if (three) { p[2][k] = (p[0][k] + p[1][k]) / 2; p[3][k] = 0; }
        else { p[2][k] = (2 * p[0][k] + p[1][k]) / 3; p[3][k] = (p[0][k] + 2 * p[1][k]) / 3; }
    }
    uint32_t ci = 0;
    for (int i = 0; i < 16; i++) {
        if (three && px[i][3] < 128) { ci |= 3u << (2 * i); continue; }
        int best = 0, bd = 1 << 30;
        for (int c = 0; c < (three ? 3 : 4); c++) {
            int d = 0;
            for (int k = 0; k < 3; k++) { const int t = p[c][k] - px[i][k]; d += t * t; }
            if (d < bd) { bd = d; best = c; }
        }
        ci |= (uint32_t)best << (2 * i);
    }
    col[0] = (uint8_t)c0; col[1] = (uint8_t)(c0 >> 8); col[2] = (uint8_t)c1; col[3] = (uint8_t)(c1 >> 8);
    col[4] = (uint8_t)ci; col[5] = (uint8_t)(ci >> 8); col[6] = (uint8_t)(ci >> 16); col[7] = (uint8_t)(ci >> 24);
}

// Byte offset of texel x, y of a level.
size_t texel_at(const rsx_tex_level& lv, uint32_t x, uint32_t y)
{
    if (lv.tl.swizzled) return (size_t)rsx_swizzle_offset(x, y, rsx_log2_ceil(lv.w), rsx_log2_ceil(lv.h)) * lv.tl.bytes_per_texel;
    return (size_t)y * lv.tl.row_bytes + (size_t)x * lv.tl.bytes_per_texel;
}

// Uncompressed texel <-> the resource's RGBA (what rsx_texture_decode makes of it).
void texel_get(uint32_t base, const uint8_t* s, uint8_t d[4])
{
    switch (base) {
    case RSX_TEX_FMT_A8R8G8B8:
        if (rsx_texture_argb_is_rgba()) { memcpy(d, s, 4); }
        else { d[0] = s[1]; d[1] = s[2]; d[2] = s[3]; d[3] = s[0]; }
        break;
    case RSX_TEX_FMT_A4R4G4B4: {
        const uint32_t v = (uint32_t)s[0] << 8 | s[1];
        d[3] = (uint8_t)(((v >> 12) & 15) * 17); d[0] = (uint8_t)(((v >> 8) & 15) * 17);
        d[1] = (uint8_t)(((v >> 4) & 15) * 17); d[2] = (uint8_t)((v & 15) * 17);
        break;
    }
    case RSX_TEX_FMT_A1R5G5B5: {
        const uint32_t v = (uint32_t)s[0] << 8 | s[1];
        d[3] = (v >> 15) ? 255 : 0; d[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31);
        d[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31); d[2] = (uint8_t)((v & 31) * 255 / 31);
        break;
    }
    }
}

void texel_put(uint32_t base, uint8_t* s, const uint8_t d[4])
{
    switch (base) {
    case RSX_TEX_FMT_A8R8G8B8:
        if (rsx_texture_argb_is_rgba()) { memcpy(s, d, 4); }
        else { s[0] = d[3]; s[1] = d[0]; s[2] = d[1]; s[3] = d[2]; }
        break;
    case RSX_TEX_FMT_A4R4G4B4: {
        const uint32_t v = (uint32_t)((d[3] + 8) / 17) << 12 | (uint32_t)((d[0] + 8) / 17) << 8 |
                           (uint32_t)((d[1] + 8) / 17) << 4 | (uint32_t)((d[2] + 8) / 17);
        s[0] = (uint8_t)(v >> 8); s[1] = (uint8_t)v;
        break;
    }
    case RSX_TEX_FMT_A1R5G5B5: {
        const uint32_t v = (uint32_t)(d[3] >= 128) << 15 | (uint32_t)((d[0] * 31 + 127) / 255) << 10 |
                           (uint32_t)((d[1] * 31 + 127) / 255) << 5 | (uint32_t)((d[2] * 31 + 127) / 255);
        s[0] = (uint8_t)(v >> 8); s[1] = (uint8_t)v;
        break;
    }
    }
}

// The remap: sel[c] = the resource channel (0-3 = R,G,B,A) or RSX_REMAP_ZERO / ONE for each shown channel,
// in R, G, B, A order.
void remap_sel(uint32_t remap, uint32_t fmt, uint8_t sel[4])
{
    uint8_t o[4];   // A, R, G, B
    rsx_texture_component_remap(remap, fmt, o);
    sel[0] = o[1]; sel[1] = o[2]; sel[2] = o[3]; sel[3] = o[0];
}

void shown_from(const uint8_t res[4], const uint8_t sel[4], uint8_t out[4])
{
    for (int c = 0; c < 4; c++) out[c] = sel[c] < 4 ? res[sel[c]] : sel[c] == RSX_REMAP_ONE ? 255 : 0;
}

void res_from(const uint8_t shown[4], const uint8_t sel[4], uint8_t res[4])   // res holds the old texel
{
    bool set[4] = {false, false, false, false};
    for (int c = 0; c < 4; c++)
        if (sel[c] < 4 && !set[sel[c]]) { res[sel[c]] = shown[c]; set[sel[c]] = true; }
}

// The resource's RGBA of a level (no remap).
bool level_get(const uint8_t* base_ptr, uint32_t base, const rsx_tex_level& lv, Img& im)
{
    im.w = (int)lv.w; im.h = (int)lv.h;
    im.px.assign((size_t)lv.w * lv.h * 4, 0);
    const uint8_t* src = base_ptr + lv.offset;
    if (lv.tl.compressed) {
        const uint32_t bw = (lv.w + 3) / 4, bh = (lv.h + 3) / 4;
        for (uint32_t by = 0; by < bh; by++)
            for (uint32_t bx = 0; bx < bw; bx++) {
                uint8_t t[16][4];
                bc_block(base, src + (size_t)by * lv.tl.row_bytes + (size_t)bx * lv.tl.block_bytes, t);
                for (int i = 0; i < 16; i++) {
                    const uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                    if (x < lv.w && y < lv.h) memcpy(&im.px[((size_t)y * lv.w + x) * 4], t[i], 4);
                }
            }
        return true;
    }
    for (uint32_t y = 0; y < lv.h; y++)
        for (uint32_t x = 0; x < lv.w; x++) texel_get(base, src + texel_at(lv, x, y), &im.px[((size_t)y * lv.w + x) * 4]);
    return true;
}

// A level back into the texture's format: only the texels chg marks (a DXT block when one of its texels is).
void level_put(uint8_t* base_ptr, uint32_t base, const rsx_tex_level& lv, const Img& im, const std::vector<uint8_t>& chg)
{
    uint8_t* dst = base_ptr + lv.offset;
    if (lv.tl.compressed) {
        const uint32_t bw = (lv.w + 3) / 4, bh = (lv.h + 3) / 4;
        for (uint32_t by = 0; by < bh; by++)
            for (uint32_t bx = 0; bx < bw; bx++) {
                bool any = false;
                for (uint32_t y = by * 4; y < by * 4 + 4 && y < lv.h && !any; y++)
                    for (uint32_t x = bx * 4; x < bx * 4 + 4 && x < lv.w && !any; x++) any = chg[(size_t)y * lv.w + x] != 0;
                if (!any) continue;
                uint8_t t[16][4];
                for (int i = 0; i < 16; i++) {   // edge blocks repeat the last row / column
                    uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                    if (x >= lv.w) x = lv.w - 1;
                    if (y >= lv.h) y = lv.h - 1;
                    memcpy(t[i], &im.px[((size_t)y * lv.w + x) * 4], 4);
                }
                bc_encode(base, t, dst + (size_t)by * lv.tl.row_bytes + (size_t)bx * lv.tl.block_bytes);
            }
        return;
    }
    for (uint32_t y = 0; y < lv.h; y++)
        for (uint32_t x = 0; x < lv.w; x++)
            if (chg[(size_t)y * lv.w + x]) texel_put(base, dst + texel_at(lv, x, y), &im.px[((size_t)y * lv.w + x) * 4]);
}

// The texels of the next level down that take something from a changed one.
std::vector<uint8_t> half_mask(const std::vector<uint8_t>& m, int w, int h, int nw, int nh)
{
    std::vector<uint8_t> o((size_t)nw * nh, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (m[(size_t)y * w + x]) {
                const int u = x / 2 < nw ? x / 2 : nw - 1, v = y / 2 < nh ? y / 2 : nh - 1;
                o[(size_t)v * nw + u] = 1;
            }
    return o;
}

// Half size, alpha-weighted box filter.
Img half(const Img& s, int w, int h)
{
    Img d; d.w = w; d.h = h;
    d.px.assign((size_t)w * h * 4, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double c[4] = {0, 0, 0, 0};
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    const int sx = x * 2 + i < s.w ? x * 2 + i : s.w - 1, sy = y * 2 + j < s.h ? y * 2 + j : s.h - 1;
                    const uint8_t* p = &s.px[((size_t)sy * s.w + sx) * 4];
                    const double a = p[3] / 255.0;
                    for (int k = 0; k < 3; k++) c[k] += p[k] * a;
                    c[3] += a;
                }
            uint8_t* o = &d.px[((size_t)y * w + x) * 4];
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)(c[3] > 0 ? c[k] / c[3] + 0.5 : 0);
            o[3] = (uint8_t)(c[3] / 4 * 255 + 0.5);
        }
    return d;
}

// Clear texels take the colour of their visible neighbours, so filtering at the symbol's edge doesn't pull in
// black.
void bleed(Img& im, const std::vector<uint8_t>& chg)
{
    std::vector<uint8_t> got(chg.size(), 0);   // a clear texel that has a colour now
    for (int pass = 0; pass < 3; pass++) {
        std::vector<uint8_t> next = im.px;
        std::vector<uint8_t> got_next = got;
        for (int y = 0; y < im.h; y++)
            for (int x = 0; x < im.w; x++) {
                const size_t at = (size_t)y * im.w + x;
                if (!chg[at] || got[at] || im.px[at * 4 + 3] >= 8) continue;
                int c[3] = {0, 0, 0}, n = 0;
                for (int j = -1; j <= 1; j++)
                    for (int i = -1; i <= 1; i++) {
                        const int u = x + i, v = y + j;
                        if (u < 0 || v < 0 || u >= im.w || v >= im.h) continue;
                        const size_t q = (size_t)v * im.w + u;
                        if (im.px[q * 4 + 3] < 8 && !got[q]) continue;
                        for (int k = 0; k < 3; k++) c[k] += im.px[q * 4 + k];
                        n++;
                    }
                if (!n) continue;
                for (int k = 0; k < 3; k++) next[at * 4 + k] = (uint8_t)(c[k] / n);
                got_next[at] = 1;
            }
        im.px.swap(next);
        got.swap(got_next);
    }
}

// ---- what each button shows now ---------------------------------------------------------------------------

int bit_index(uint16_t bit)
{
    for (int i = 0; i < 16; i++) if (kButtons[i].bit == bit) return i;
    return -1;
}

// The symbol for a button in the style in force: GT5G_* (GT5G_NONE = keep the game's) and the key's name.
int symbol_for(uint16_t bit, const char** key)
{
    *key = nullptr;
    const int i = bit_index(bit);
    if (i < 0) return GT5G_NONE;
    if (g_style == STYLE_KEYS) {
        if (!g_keyname[i][0]) return GT5G_NONE;
        *key = g_keyname[i];
        return GT5G_KEY;
    }
    if (g_style != STYLE_XBOX) return GT5G_NONE;
    switch (g_xbox[i]) {
    case SYM_A: return GT5G_XA;
    case SYM_B: return GT5G_XB;
    case SYM_X: return GT5G_XX;
    case SYM_Y: return GT5G_XY;
    case SYM_LB: return GT5G_XLB;
    case SYM_RB: return GT5G_XRB;
    case SYM_LT: return GT5G_XLT;
    case SYM_RT: return GT5G_XRT;
    case SYM_LS: return GT5G_XLS;
    case SYM_RS: return GT5G_XRS;
    case SYM_VIEW: return GT5G_XVIEW;
    case SYM_MENU: return GT5G_XMENU;
    default: return GT5G_NONE;   // the d-pad (the same on both), or nothing presses it
    }
}

const char* style_name(int s) { return s == STYLE_XBOX ? "Xbox" : s == STYLE_KEYS ? "keyboard" : "the game's own"; }

// Reads the setting and cellPad's answers when any of them may have changed; true when the pictures change.
bool refresh()
{
    const unsigned sg = g_gt5_settings_gen, pg = cellPad_symbol_gen();
    const int dev = g_choice == CHOICE_AUTO ? cellPad_symbol_device() : 0;
    if (sg == g_seen_settings && pg == g_seen_pad && dev == g_seen_dev) return false;
    if (sg != g_seen_settings) {
        char v[16] = "";
        gt5_setting_str("GT5_BUTTON_SYMBOLS", v, sizeof v);
        g_choice = !strcmp(v, "xbox") ? CHOICE_XBOX : !strcmp(v, "keys") ? CHOICE_KEYS : !strcmp(v, "auto") ? CHOICE_AUTO : CHOICE_GAME;
    }
    g_seen_settings = sg; g_seen_pad = pg;
    g_seen_dev = g_choice == CHOICE_AUTO ? cellPad_symbol_device() : 0;
    int style = STYLE_GAME;
    if (g_choice == CHOICE_XBOX) style = STYLE_XBOX;
    else if (g_choice == CHOICE_KEYS) style = STYLE_KEYS;
    else if (g_choice == CHOICE_AUTO)
        style = g_seen_dev == DEV_KEYBOARD ? STYLE_KEYS : g_seen_dev == DEV_XBOX ? STYLE_XBOX : STYLE_GAME;
    uint64_t k = 1469598103934665603ull ^ (uint64_t)style;
    for (int i = 0; i < 16; i++) {
        g_xbox[i] = cellPad_symbol_xbox(kButtons[i].bit);
        g_keyname[i][0] = 0;
        cellPad_symbol_key(kButtons[i].bit, g_keyname[i], (int)sizeof g_keyname[i]);
        if (style == STYLE_XBOX) k = (k ^ (uint64_t)g_xbox[i]) * 1099511628211ull;
        if (style == STYLE_KEYS)
            for (const char* p = g_keyname[i]; ; p++) { k = (k ^ (uint8_t)*p) * 1099511628211ull; if (!*p) break; }
    }
    const uint32_t key = style == STYLE_GAME ? 0u : (uint32_t)(k ^ (k >> 32)) | 1u;
    const bool changed = key != g_key || style != g_style;
    if (changed && (g_style != style)) fprintf(stderr, "[glyphs] button symbols: %s\n", style_name(style));
    g_style = style;
    g_key = key;
    return changed;
}

// ---- painting ---------------------------------------------------------------------------------------------

// The picture for an entry in the look wanted now, from the game's own bytes; empty when nothing changes.
std::vector<uint8_t> build(const Entry& e, const rsx_dsp_texture& t, const Shape& s)
{
    std::vector<uint8_t> out(e.orig);
    uint8_t sel[4];
    remap_sel(t.remap, t.format, sel);
    Img res;
    level_get(e.orig.data(), s.base, s.lv[0], res);
    Img shown = res;
    for (size_t i = 0; i < res.px.size(); i += 4) shown_from(&res.px[i], sel, &shown.px[i]);
    const Img before = shown;
    bool changed = false;
    for (const Cell& c : e.cells) {
        const char* key = nullptr;
        const int sym = symbol_for(c.bit, &key);
        if (sym == GT5G_NONE) continue;
        const bool whole = !c.w;
        const int x0 = whole ? 0 : c.x, y0 = whole ? 0 : c.y;
        const int x1 = whole ? shown.w : c.x + c.w, y1 = whole ? shown.h : c.y + c.h;
        if (x0 < 0 || y0 < 0 || x1 > shown.w || y1 > shown.h || x1 <= x0 || y1 <= y0) continue;
        const int cw = x1 - x0, ch = y1 - y0;
        // The game's button: the whole picture for a picture of one button, else what a flood from the cell's
        // middle reaches over visible texels (a neighbour's corner in the cell stays).
        std::vector<uint8_t> mask((size_t)cw * ch, 0);
        auto vis = [&](int x, int y) { return shown.px[((size_t)y * shown.w + x) * 4 + 3] > 40; };
        if (whole) {
            for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) mask[(size_t)y * cw + x] = vis(x, y);
        } else {
            int sx = x0 + cw / 2, sy = y0 + ch / 2;
            if (!vis(sx, sy)) {
                int best = 1 << 30;
                for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++)
                        if (vis(x, y)) {
                            const int d = (x - sx) * (x - sx) + (y - sy) * (y - sy);
                            if (d < best) { best = d; sx = x; sy = y; }
                        }
                if (best == 1 << 30) continue;
            }
            std::vector<std::pair<int, int>> st{{sx, sy}};
            mask[(size_t)(sy - y0) * cw + (sx - x0)] = 1;
            while (!st.empty()) {
                const auto q = st.back();
                st.pop_back();
                const int nx[4] = {q.first + 1, q.first - 1, q.first, q.first}, ny[4] = {q.second, q.second, q.second + 1, q.second - 1};
                for (int k = 0; k < 4; k++) {
                    if (nx[k] < x0 || ny[k] < y0 || nx[k] >= x1 || ny[k] >= y1) continue;
                    uint8_t& mm = mask[(size_t)(ny[k] - y0) * cw + (nx[k] - x0)];
                    if (mm || !vis(nx[k], ny[k])) continue;
                    mm = 1;
                    st.push_back({nx[k], ny[k]});
                }
            }
        }
        int bx0 = x1, by0 = y1, bx1 = x0 - 1, by1 = y0 - 1;
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++)
                if (mask[(size_t)y * cw + x]) {
                    bx0 = x0 + x < bx0 ? x0 + x : bx0; by0 = y0 + y < by0 ? y0 + y : by0;
                    bx1 = x0 + x > bx1 ? x0 + x : bx1; by1 = y0 + y > by1 ? y0 + y : by1;
                }
        if (bx1 < bx0) { bx0 = x0; by0 = y0; bx1 = x1 - 1; by1 = y1 - 1; }
        // Cleared: the whole picture, or the button and the soft edge 2 texels around it.
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                bool hit = whole;
                for (int j = -2; j <= 2 && !hit; j++)
                    for (int i = -2; i <= 2 && !hit; i++) {
                        const int u = x + i, v = y + j;
                        hit = u >= 0 && v >= 0 && u < cw && v < ch && mask[(size_t)v * cw + u];
                    }
                if (hit) memset(&shown.px[((size_t)(y0 + y) * shown.w + x0 + x) * 4], 0, 4);
            }
        // The new symbol where the game's was; a key cap in a picture of several buttons goes in the cell's own
        // box (its square corners would reach a neighbour).
        float fx = (float)bx0, fy = (float)by0, fw = (float)(bx1 - bx0 + 1), fh = (float)(by1 - by0 + 1);
        if (!whole && c.fw && sym == GT5G_KEY) { fx = c.fx; fy = c.fy; fw = c.fw; fh = c.fh; }
        else if (!whole) { fx += 1; fy += 1; fw -= 2; fh -= 2; }
        gt5g_image gi = { shown.px.data(), shown.w, shown.h };
        gt5g_paint(&gi, fx, fy, fw, fh, sym, key);
        changed = true;
    }
    if (!changed) return {};
    // Only what changed is written back: the rest of a bigger picture keeps the game's bytes exactly (a DXT
    // block is encoded again only when a texel of it changed).
    std::vector<uint8_t> chg((size_t)shown.w * shown.h, 0);
    for (size_t i = 0; i < chg.size(); i++) chg[i] = memcmp(&shown.px[i * 4], &before.px[i * 4], 4) != 0;
    bleed(shown, chg);
    for (size_t i = 0; i < chg.size(); i++) if (chg[i]) res_from(&shown.px[i * 4], sel, &res.px[i * 4]);
    Img lv = res;
    for (uint32_t l = 0; l < s.levels; l++) {
        if (l) {
            lv = half(lv, (int)s.lv[l].w, (int)s.lv[l].h);
            chg = half_mask(chg, (int)s.lv[l - 1].w, (int)s.lv[l - 1].h, (int)s.lv[l].w, (int)s.lv[l].h);
        }
        level_put(out.data(), s.base, s.lv[l], lv, chg);
    }
    return out;
}

// One texture a draw samples.
struct Memo {
    uint32_t loc_off = 0, fmt_wh = 0, key = 0;
    uint64_t quick = 0;
    bool valid = false;
};
Memo g_memo[1024];

// Only the texture's own bytes: a pitched top level's rows, not the gaps between them (another picture's).
void write_texture(uint8_t* dst, const uint8_t* src, const Shape& s)
{
    for (uint32_t l = 0; l < s.levels; l++) {
        const rsx_tex_level& lv = s.lv[l];
        if (l == 0 && s.rows > 1 && s.pitch != s.row) {
            for (uint32_t y = 0; y < s.rows; y++)
                memcpy(dst + (size_t)y * s.pitch, src + (size_t)y * s.pitch, s.row);
        } else {
            memcpy(dst + lv.offset, src + lv.offset, lv.tl.face_bytes);
        }
    }
}

void log_texture(const rsx_dsp_texture& t, uint64_t hash)
{
    static std::vector<uint64_t> seen;
    if (seen.size() >= 4000) return;
    for (uint64_t h : seen) if (h == hash) return;
    seen.push_back(hash);
    fprintf(stderr, "[glyphs] texture %016llX %ux%u format 0x%02X remap 0x%04X pitch %u levels %u at %s 0x%08X\n",
            (unsigned long long)hash, t.width, t.height, t.format, t.remap & 0xFFFFu, t.pitch, t.mipmaps,
            t.location == RSX_LOCATION_LOCAL ? "local" : "main", t.offset);
}

void handle(const rsx_dsp_texture& t)
{
    Shape s;
    if (!shape_of(t, s)) return;
    uint8_t* p = const_cast<uint8_t*>(guest(t, s.span));
    if (!p) return;
    const uint32_t top = s.pitch * (s.rows - 1) + s.row;
    const uint64_t quick = quick_sig(p, top);
    const uint32_t loc_off = (t.location << 31) ^ t.offset, fmt_wh = (t.format << 24) ^ (t.width << 12) ^ t.height ^ (t.remap << 16);
    Memo& m = g_memo[((t.offset >> 4) ^ (t.offset >> 14) ^ t.location) & 1023u];
    if (m.valid && m.loc_off == loc_off && m.fmt_wh == fmt_wh && m.quick == quick && m.key == g_key) return;
    const uint64_t h = tex_hash(t, s, p);
    if (g_log) log_texture(t, h);
    m.valid = true; m.loc_off = loc_off; m.fmt_wh = fmt_wh; m.quick = quick; m.key = g_key;
    Entry* e = nullptr;
    bool is_orig = false;
    for (Entry& x : g_entries) {
        if (x.w != t.width || x.h != t.height) continue;
        if (x.hash == h) { e = &x; is_orig = true; break; }
        for (const Art& a : x.art) if (a.hash == h && !a.bytes.empty()) { e = &x; break; }
        if (e) break;
    }
    if (!e) return;
    if (is_orig && e->orig.empty()) {
        e->orig.assign(p, p + s.span);
        e->orig_fmt = t.format; e->orig_remap = t.remap; e->orig_pitch = s.pitch; e->orig_levels = s.levels;
    }
    if (e->orig.empty() || e->orig.size() != s.span || e->orig_fmt != t.format || e->orig_remap != t.remap ||
        e->orig_pitch != s.pitch || e->orig_levels != s.levels)
        return;   // our picture seen in another shape than the game's: leave it
    const std::vector<uint8_t>* want = &e->orig;
    uint64_t want_hash = e->hash;
    if (g_style != STYLE_GAME) {
        Art* a = nullptr;
        for (Art& x : e->art) if (!x.bytes.empty() && x.key == g_key) a = &x;
        if (!a) {
            a = &e->art[e->art_next];
            e->art_next = (e->art_next + 1) % 4;
            a->key = g_key;
            a->bytes = build(*e, t, s);
            a->hash = a->bytes.empty() ? 0 : tex_hash(t, s, a->bytes.data());
            if (a->bytes.empty()) { a->bytes = e->orig; a->hash = e->hash; }   // nothing to change in this look
        }
        want = &a->bytes;
        want_hash = a->hash;
    }
    if (want_hash == h) return;
    write_texture(p, want->data(), s);
    m.quick = quick_sig(p, top);
    if (!e->said || g_style == STYLE_GAME) {
        static int lines;
        if (lines++ < 200)
            fprintf(stderr, "[glyphs] %s (%016llX, %ux%u) at %s 0x%08X: %s\n", e->what, (unsigned long long)e->hash,
                    t.width, t.height, t.location == RSX_LOCATION_LOCAL ? "local" : "main", t.offset, style_name(g_style));
        e->said = g_style != STYLE_GAME;
    }
}

bool size_listed(uint32_t wh)
{
    for (int i = 0; i < g_nsizes; i++) if (g_sizes[i] == wh) return true;
    return false;
}

void draw_hook(const rsx_dispatch* rsx)
{
    // The setting at once; the pad's answers (device, bindings, layout) every 32 draws, well within a frame.
    static unsigned calls;
    if ((++calls & 31u) == 0 || g_gt5_settings_gen != g_seen_settings) {
        if (refresh())
            for (Memo& m : g_memo) m.valid = false;
    }
    for (uint32_t unit = 0; unit < RSX_DSP_NUM_TEXTURES; unit++) {
        const uint32_t base = 0x1A00u + unit * 0x20u;
        if (!(rsx_dsp_reg(rsx, base + 0x0Cu) >> 31)) continue;          // TEX_ENABLE
        const uint32_t wh = rsx_dsp_reg(rsx, base + 0x18u);              // width << 16 | height
        if (!g_log && !size_listed(wh)) continue;
        if (g_log && ((wh >> 16) > 512 || (wh & 0xFFFF) > 512)) continue;
        rsx_dsp_texture t;
        rsx_dsp_get_texture(rsx, unit, &t);
        handle(t);
    }
}

// ---- the table ----------------------------------------------------------------------------------------------

void add_entry(uint64_t hash, uint32_t w, uint32_t h, const char* what, const Cell* cells, int n)
{
    if (!hash || !w || !h) return;
    for (const Entry& e : g_entries) if (e.hash == hash) return;
    Entry e;
    e.hash = hash; e.w = w; e.h = h;
    snprintf(e.what, sizeof e.what, "%s", what ? what : "symbol");
    for (int i = 0; i < n; i++) if (cells[i].bit) e.cells.push_back(cells[i]);
    if (e.cells.empty()) return;
    g_entries.push_back(std::move(e));
    const uint32_t wh = w << 16 | h;
    if (!size_listed(wh) && g_nsizes < (int)(sizeof g_sizes / sizeof g_sizes[0])) g_sizes[g_nsizes++] = wh;
}

// GT5_GLYPH_TABLE lines: "<hash> <w>x<h> <button>[@x,y,w,h[/x,y,w,h]] ..." ('#' starts a comment; the second box: a key cap's).
void load_table_file(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "[glyphs] GT5_GLYPH_TABLE: can't read %s\n", path); return; }
    char line[512];
    int added = 0;
    while (fgets(line, sizeof line, f)) {
        char* hashp = strtok(line, " \t\r\n");
        if (!hashp || hashp[0] == '#') continue;
        char* dim = strtok(nullptr, " \t\r\n");
        unsigned w = 0, h = 0;
        if (!dim || sscanf(dim, "%ux%u", &w, &h) != 2) continue;
        Cell cells[16];
        int n = 0;
        for (char* tok; n < 16 && (tok = strtok(nullptr, " \t\r\n")) && tok[0] != '#'; ) {
            char* at = strchr(tok, '@');
            if (at) *at = 0;
            int bi = -1;
            for (int i = 0; i < 16; i++) if (!strcmp(tok, kButtons[i].name)) bi = i;
            if (bi < 0) { fprintf(stderr, "[glyphs] GT5_GLYPH_TABLE: no button called '%s'\n", tok); continue; }
            Cell c = { kButtons[bi].bit, 0, 0, 0, 0 };
            int x, y, cw, ch, fx, fy, fw, fh;
            const int got = at ? sscanf(at + 1, "%d,%d,%d,%d/%d,%d,%d,%d", &x, &y, &cw, &ch, &fx, &fy, &fw, &fh) : 0;
            if (got >= 4) { c.x = (int16_t)x; c.y = (int16_t)y; c.w = (int16_t)cw; c.h = (int16_t)ch; }
            if (got == 8) { c.fx = (int16_t)fx; c.fy = (int16_t)fy; c.fw = (int16_t)fw; c.fh = (int16_t)fh; }
            cells[n++] = c;
        }
        const size_t before = g_entries.size();
        add_entry(strtoull(hashp, nullptr, 16), w, h, "listed symbol", cells, n);
        added += g_entries.size() > before;
    }
    fclose(f);
    fprintf(stderr, "[glyphs] GT5_GLYPH_TABLE: %d picture(s) from %s\n", added, path);
}

void install()
{
    if (g_installed) return;
    g_installed = true;
    g_rsx_draw_textures_hook = draw_hook;
    fprintf(stderr, "[glyphs] watching the draws' textures for %zu symbol picture(s)%s\n", g_entries.size(),
            g_log ? " (and logging small textures)" : "");
}

void setting_changed(const char*, const char*, const char* now, void*)
{
    if (now && now[0]) install();
}

void glyphs_start()
{
    for (const Known& k : kKnown) {
        int n = 0;
        while (n < 16 && k.cells[n].bit) n++;
        add_entry(k.hash, k.w, k.h, k.what, k.cells, n);
    }
    const char* tf = getenv("GT5_GLYPH_TABLE");
    if (tf && *tf) load_table_file(tf);
    const char* lg = getenv("GT5_GLYPH_LOG");
    g_log = lg && *lg && *lg != '0';
    char v[16] = "";
    gt5_setting_str("GT5_BUTTON_SYMBOLS", v, sizeof v);
    if (v[0] || g_log) install();
    gt5_settings_on_change("GT5_BUTTON_SYMBOLS", setting_changed, nullptr);
}

const int s_registered = (ps3_on_loaded(glyphs_start), 0);

}  // namespace

// For tests (glyph1/tools/glyph_test.cpp): the module's start and its hook, without a game.
extern "C" void gt5_glyphs_test_start(void) { glyphs_start(); }
extern "C" void gt5_glyphs_test_add(uint64_t hash, unsigned w, unsigned h, unsigned bit, int x, int y, int cw, int ch)
{
    const Cell c = { (uint16_t)bit, (int16_t)x, (int16_t)y, (int16_t)cw, (int16_t)ch };
    add_entry(hash, w, h, "test symbol", &c, 1);
}
extern "C" uint64_t gt5_glyphs_test_hash(const rsx_dsp_texture* t)
{
    Shape s;
    if (!shape_of(*t, s)) return 0;
    const uint8_t* p = guest(*t, s.span);
    return p ? tex_hash(*t, s, p) : 0;
}
