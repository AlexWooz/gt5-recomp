/* Button symbols drawn in code: see gt5_glyph_art.h.
 *
 * A symbol is a short list of layers (filled discs, rounded rectangles with their own top and bottom
 * corner radius, bands just inside their edge, and strokes of the font), painted back to front. Each
 * pixel is 4 x 4 samples, so edges are smooth at any size, and nothing is a picture: the same code
 * draws a 24-pixel help-bar button and a 120-pixel one. */
#include "gt5_glyph_art.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the stroke font --------------------------------------------------------------------------------
 * Capitals, digits, a few signs and key symbols, drawn on a 4 x 6 grid (y down) as lines of a fixed
 * width with round ends. A path is "M x,y" (move), "L x,y" (line to) and "E cx,cy,rx,ry,a0,a1" (an
 * elliptic arc from angle a0 to a1, degrees, 0 = right, 90 = down; it carries on from the last point when
 * it starts there, else it starts a new line). Our own design: straight lines and plain arcs. */
static const struct { char c; const char* path; } k_font[] = {
    { 'A', "M0,6 L2,0 L4,6 M0.67,4 L3.33,4" },
    { 'B', "M0,6 L0,0 L2.3,0 E2.3,1.45,1.45,1.45,270,450 L0,2.9 M2.3,2.9 E2.3,4.45,1.6,1.55,270,450 L0,6" },
    { 'C', "E2,2,2,2,330,180 L0,4 E2,4,2,2,180,30" },
    { 'D', "M0,0 L1.8,0 E1.8,2,2.2,2,270,360 L4,4 E1.8,4,2.2,2,0,90 L0,6 L0,0" },
    { 'E', "M4,0 L0,0 L0,6 L4,6 M0,3 L3,3" },
    { 'F', "M4,0 L0,0 L0,6 M0,3 L3,3" },
    { 'G', "E2,2,2,2,330,180 L0,4 E2,4,2,2,180,0 L4,3.2 L2.3,3.2" },
    { 'H', "M0,0 L0,6 M4,0 L4,6 M0,3 L4,3" },
    { 'I', "M2,0 L2,6 M1,0 L3,0 M1,6 L3,6" },
    { 'J', "M4,0 L4,4.2 E2.2,4.2,1.8,1.8,0,180" },
    { 'K', "M0,0 L0,6 M4,0 L0,3.8 M1.4,2.5 L4,6" },
    { 'L', "M0,0 L0,6 L4,6" },
    { 'M', "M0,6 L0,0 L2,3.6 L4,0 L4,6" },
    { 'N', "M0,6 L0,0 L4,6 L4,0" },
    { 'O', "E2,2,2,2,180,360 L4,4 E2,4,2,2,0,180 L0,2" },
    { 'P', "M0,6 L0,0 L2.3,0 E2.3,1.6,1.7,1.6,270,450 L0,3.2" },
    { 'Q', "E2,2,2,2,180,360 L4,4 E2,4,2,2,0,180 L0,2 M2.6,4.4 L4.2,6.3" },
    { 'R', "M0,6 L0,0 L2.3,0 E2.3,1.6,1.7,1.6,270,450 L0,3.2 M2.1,3.2 L4,6" },
    { 'S', "E2,1.5,1.9,1.5,345,90 E2,4.5,2,1.5,270,525" },
    { 'T', "M0,0 L4,0 M2,0 L2,6" },
    { 'U', "M0,0 L0,4 E2,4,2,2,180,0 L4,0" },
    { 'V', "M0,0 L2,6 L4,0" },
    { 'W', "M0,0 L1,6 L2,2.4 L3,6 L4,0" },
    { 'X', "M0,0 L4,6 M4,0 L0,6" },
    { 'Y', "M0,0 L2,3 L4,0 M2,3 L2,6" },
    { 'Z', "M0,0 L4,0 L0,6 L4,6" },
    { '0', "E2,2,2,2,180,360 L4,4 E2,4,2,2,0,180 L0,2" },
    { '1', "M0.8,1.2 L2.2,0 L2.2,6 M0.8,6 L3.6,6" },
    { '2', "E2,2,2,2,195,375 L0,6 L4,6" },
    { '3', "E2,1.5,1.9,1.5,200,450 E2,4.5,2,1.5,270,520 M1.2,3 L2,3" },
    { '4', "M3,6 L3,0 L0,4.2 L4,4.2" },
    { '5', "M3.8,0 L0.6,0 L0.4,2.9 E2,4.2,2,1.8,232,520" },
    { '6', "E2,4.1,1.9,1.9,0,360 M0.1,4.1 L0.1,2 E2,2,1.9,2,180,320" },
    { '7', "M0,0 L4,0 L1.6,6" },
    { '8', "E2,1.5,1.7,1.5,0,360 M3.9,4.4 E2,4.4,1.9,1.6,0,360" },
    { '9', "E2,1.9,1.9,1.9,0,360 M3.9,1.9 L3.9,4 E2,4,1.9,2,0,140" },
    { '+', "M2,1.5 L2,4.5 M0.5,3 L3.5,3" },
    { '-', "M0.6,3 L3.4,3" },
    { '.', "M2,5.6 L2,5.6" },
    { ',', "M2.2,5.1 L1.6,6.6" },
    { '/', "M3.6,0 L0.4,6" },
    { '\\', "M0.4,0 L3.6,6" },
    { ';', "M2,2.2 L2,2.2 M2.2,5.1 L1.6,6.6" },
    { ':', "M2,2.2 L2,2.2 M2,5.6 L2,5.6" },
    { '\'', "M2,0 L2,1.8" },
    { '[', "M3,0 L1.2,0 L1.2,6 L3,6" },
    { ']', "M1,0 L2.8,0 L2.8,6 L1,6" },
    { '=', "M0.5,2.2 L3.5,2.2 M0.5,3.8 L3.5,3.8" },
    { '`', "M1.4,0 L2.4,1.4" },
    { '*', "M2,1.4 L2,4.6 M0.6,2.2 L3.4,3.8 M3.4,2.2 L0.6,3.8" },
    /* key symbols: 1 Enter, 2 Tab, 3 Shift, 4 Space, 5 Backspace, 6-9 arrows up / down / left / right */
    { 1, "M3.8,0.4 L3.8,3.8 L0.4,3.8 M1.8,2.4 L0.4,3.8 L1.8,5.2" },
    { 2, "M0.2,3 L3.4,3 M2,1.6 L3.4,3 L2,4.4 M4,1.4 L4,4.6" },
    { 3, "M2,0.5 L4,3 L3,3 L3,5.5 L1,5.5 L1,3 L0,3 L2,0.5" },
    { 4, "M0,3.2 L0,4.9 L4,4.9 L4,3.2" },
    { 5, "M4.6,0.4 L1.6,0.4 L-0.6,3 L1.6,5.6 L4.6,5.6 L4.6,0.4 M2.2,1.9 L3.4,4.1 M3.4,1.9 L2.2,4.1" },
    { 6, "M2,5.6 L2,0.6 M0.4,2.2 L2,0.6 L3.6,2.2" },
    { 7, "M2,0.4 L2,5.4 M0.4,3.8 L2,5.4 L3.6,3.8" },
    { 8, "M4,3 L-0.6,3 M1,1.4 L-0.6,3 L1,4.6" },
    { 9, "M0,3 L4.6,3 M3,1.4 L4.6,3 L3,4.6" },
};
#define FONT_N (int)(sizeof k_font / sizeof k_font[0])
#define FONT_MAX_SEGS 96
#define FONT_ADVANCE 5.3f     /* glyph width 4 + the gap */
#define FONT_STROKE 0.95f     /* line width in grid units (cap height 6) */

typedef struct { float x0, y0, x1, y1; } seg;
static seg s_glyph_seg[FONT_N][FONT_MAX_SEGS];
static int s_glyph_nseg[FONT_N];
static volatile int s_font_ready;

static void font_add(int g, float x0, float y0, float x1, float y1)
{
    if (s_glyph_nseg[g] >= FONT_MAX_SEGS) return;
    seg* s = &s_glyph_seg[g][s_glyph_nseg[g]++];
    s->x0 = x0; s->y0 = y0; s->x1 = x1; s->y1 = y1;
}

static void font_parse(int g, const char* p)
{
    float cx = 0, cy = 0;
    int have = 0;
    while (*p) {
        while (*p == ' ') p++;
        const char op = *p;
        if (!op) break;
        p++;
        float v[6] = {0, 0, 0, 0, 0, 0};
        const int want = op == 'E' ? 6 : 2;
        for (int i = 0; i < want; i++) {
            char* end;
            v[i] = (float)strtod(p, &end);
            p = end;
            if (*p == ',') p++;
        }
        if (op == 'M') { cx = v[0]; cy = v[1]; have = 1; }
        else if (op == 'L') {
            if (have) font_add(g, cx, cy, v[0], v[1]);
            cx = v[0]; cy = v[1]; have = 1;
        } else if (op == 'E') {
            const float pi = 3.14159265f;
            const float a0 = v[4] * pi / 180.0f, a1 = v[5] * pi / 180.0f;
            const float sx = v[0] + v[2] * cosf(a0), sy = v[1] + v[3] * sinf(a0);
            int n = (int)(fabsf(v[5] - v[4]) / 10.0f + 0.999f);
            if (n < 2) n = 2;
            float px = sx, py = sy;
            if (have && fabsf(cx - sx) < 0.05f && fabsf(cy - sy) < 0.05f) { px = cx; py = cy; }
            for (int i = 1; i <= n; i++) {
                const float a = a0 + (a1 - a0) * (float)i / (float)n;
                const float x = v[0] + v[2] * cosf(a), y = v[1] + v[3] * sinf(a);
                font_add(g, px, py, x, y);
                px = x; py = y;
            }
            cx = px; cy = py; have = 1;
        }
        /* "M x,y L x,y" with the same point is a dot: a zero-length line */
    }
}

static void font_init(void)
{
    if (s_font_ready) return;
    for (int g = 0; g < FONT_N; g++) { s_glyph_nseg[g] = 0; font_parse(g, k_font[g].path); }
    s_font_ready = 1;
}

static int font_index(char c)
{
    for (int g = 0; g < FONT_N; g++) if (k_font[g].c == c) return g;
    return -1;
}

/* ---- layers -------------------------------------------------------------------------------------------- */

enum { LY_RRECT, LY_DISC, LY_STROKES };

#define MAX_LAYERS 12
#define MAX_TEXT_SEGS 640

typedef struct {
    int kind;
    float col[4];                 /* straight RGBA, 0..1 */
    float cx, cy, hw, hh;         /* centre and half sizes (a disc: hw = radius) */
    float rt, rb;                 /* rounded rectangle: corner radius at the top and at the bottom */
    float band;                   /* > 0: only this wide a band just inside the edge */
    const seg* segs; int nseg;    /* strokes, in pixels */
    float sw;                     /* stroke half width */
    float bx0, by0, bx1, by1;     /* bounds */
} layer;

typedef struct {
    layer l[MAX_LAYERS];
    int n;
    seg text[MAX_TEXT_SEGS];
    int ntext;
} scene;

static void rgb(float out[4], unsigned c, float a)
{
    out[0] = (float)((c >> 16) & 255) / 255.0f;
    out[1] = (float)((c >> 8) & 255) / 255.0f;
    out[2] = (float)(c & 255) / 255.0f;
    out[3] = a;
}

static layer* add(scene* s, int kind, unsigned colour)
{
    if (s->n >= MAX_LAYERS) return NULL;
    layer* l = &s->l[s->n++];
    memset(l, 0, sizeof *l);
    l->kind = kind;
    rgb(l->col, colour, 1.0f);
    return l;
}

static void add_rrect(scene* s, unsigned colour, float cx, float cy, float hw, float hh, float rt, float rb, float band)
{
    layer* l = add(s, LY_RRECT, colour);
    if (!l) return;
    const float m = hw < hh ? hw : hh;
    if (rt > m) rt = m;
    if (rb > m) rb = m;
    l->cx = cx; l->cy = cy; l->hw = hw; l->hh = hh; l->rt = rt; l->rb = rb; l->band = band;
    l->bx0 = cx - hw; l->bx1 = cx + hw; l->by0 = cy - hh; l->by1 = cy + hh;
}

static void add_disc(scene* s, unsigned colour, float cx, float cy, float r, float band)
{
    layer* l = add(s, LY_DISC, colour);
    if (!l) return;
    l->cx = cx; l->cy = cy; l->hw = r; l->band = band;
    l->bx0 = cx - r; l->bx1 = cx + r; l->by0 = cy - r; l->by1 = cy + r;
}

/* Width of a label in grid units (the last glyph without its gap). */
static float text_units(const char* t)
{
    const size_t n = strlen(t);
    return n ? FONT_ADVANCE * (float)(n - 1) + 4.0f : 0.0f;
}

/* The label's strokes, centred in the box, its capitals at most cap_px tall. */
static void add_text(scene* s, unsigned colour, const char* t, float cx, float cy, float max_w, float cap_px)
{
    font_init();
    const float units = text_units(t);
    if (units <= 0.0f) return;
    float k = cap_px / 6.0f;                                       /* pixels per grid unit */
    const float wpx = (units + FONT_STROKE) * k;
    if (wpx > max_w) k = max_w / (units + FONT_STROKE);
    layer* l = add(s, LY_STROKES, colour);
    if (!l) return;
    const float x0 = cx - units * k / 2.0f, y0 = cy - 3.0f * k;
    l->segs = s->text + s->ntext;
    l->sw = FONT_STROKE * k / 2.0f;
    l->bx0 = 1e9f; l->by0 = 1e9f; l->bx1 = -1e9f; l->by1 = -1e9f;
    for (size_t i = 0; t[i]; i++) {
        const int g = font_index(t[i]);
        if (g < 0) continue;
        const float gx = x0 + FONT_ADVANCE * k * (float)i;
        for (int j = 0; j < s_glyph_nseg[g] && s->ntext < MAX_TEXT_SEGS; j++) {
            const seg* a = &s_glyph_seg[g][j];
            seg* b = &s->text[s->ntext++];
            b->x0 = gx + a->x0 * k; b->y0 = y0 + a->y0 * k;
            b->x1 = gx + a->x1 * k; b->y1 = y0 + a->y1 * k;
            l->nseg++;
            const float xs[2] = { b->x0, b->x1 }, ys[2] = { b->y0, b->y1 };
            for (int q = 0; q < 2; q++) {
                if (xs[q] < l->bx0) l->bx0 = xs[q];
                if (xs[q] > l->bx1) l->bx1 = xs[q];
                if (ys[q] < l->by0) l->by0 = ys[q];
                if (ys[q] > l->by1) l->by1 = ys[q];
            }
        }
    }
    l->bx0 -= l->sw; l->by0 -= l->sw; l->bx1 += l->sw; l->by1 += l->sw;
}

/* Signed distance to a rounded rectangle whose top corners and bottom corners have their own radius. */
static float rrect_dist(const layer* l, float x, float y)
{
    const float r = y < l->cy ? l->rt : l->rb;
    const float qx = fabsf(x - l->cx) - (l->hw - r), qy = fabsf(y - l->cy) - (l->hh - r);
    const float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    const float in = (qx > qy ? qx : qy) < 0 ? (qx > qy ? qx : qy) : 0;
    return sqrtf(ox * ox + oy * oy) + in - r;
}

static int covers(const layer* l, float x, float y)
{
    if (x < l->bx0 || x > l->bx1 || y < l->by0 || y > l->by1) return 0;
    switch (l->kind) {
    case LY_RRECT: {
        const float d = rrect_dist(l, x, y);
        return d <= 0 && (l->band <= 0 || d >= -l->band);
    }
    case LY_DISC: {
        const float dx = x - l->cx, dy = y - l->cy, d = sqrtf(dx * dx + dy * dy) - l->hw;
        return d <= 0 && (l->band <= 0 || d >= -l->band);
    }
    case LY_STROKES: {
        const float w2 = l->sw * l->sw;
        for (int i = 0; i < l->nseg; i++) {
            const seg* s = &l->segs[i];
            const float vx = s->x1 - s->x0, vy = s->y1 - s->y0, wx = x - s->x0, wy = y - s->y0;
            const float vv = vx * vx + vy * vy;
            float t = vv > 0 ? (wx * vx + wy * vy) / vv : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            const float dx = wx - t * vx, dy = wy - t * vy;
            if (dx * dx + dy * dy <= w2) return 1;
        }
        return 0;
    }
    }
    return 0;
}

/* Paint the scene over the picture, 4 x 4 samples a pixel. */
static void render(gt5g_image* img, const scene* s)
{
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    for (int i = 0; i < s->n; i++) {
        if (s->l[i].bx0 < bx0) bx0 = s->l[i].bx0;
        if (s->l[i].by0 < by0) by0 = s->l[i].by0;
        if (s->l[i].bx1 > bx1) bx1 = s->l[i].bx1;
        if (s->l[i].by1 > by1) by1 = s->l[i].by1;
    }
    int x0 = (int)floorf(bx0), y0 = (int)floorf(by0), x1 = (int)ceilf(bx1), y1 = (int)ceilf(by1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > img->w) x1 = img->w;
    if (y1 > img->h) y1 = img->h;
    const int N = 4;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float acc[4] = {0, 0, 0, 0};   /* premultiplied */
            for (int j = 0; j < N; j++)
                for (int i = 0; i < N; i++) {
                    const float sx = (float)x + ((float)i + 0.5f) / (float)N, sy = (float)y + ((float)j + 0.5f) / (float)N;
                    float c[4] = {0, 0, 0, 0};
                    for (int k = 0; k < s->n; k++) {
                        const layer* l = &s->l[k];
                        if (!covers(l, sx, sy)) continue;
                        const float a = l->col[3];
                        for (int q = 0; q < 3; q++) c[q] = l->col[q] * a + c[q] * (1 - a);
                        c[3] = a + c[3] * (1 - a);
                    }
                    for (int q = 0; q < 4; q++) acc[q] += c[q];
                }
            for (int q = 0; q < 4; q++) acc[q] /= (float)(N * N);
            if (acc[3] <= 0.0f) continue;
            unsigned char* d = img->px + ((size_t)y * (size_t)img->w + (size_t)x) * 4;
            const float da = (float)d[3] / 255.0f, oa = acc[3] + da * (1 - acc[3]);
            for (int q = 0; q < 3; q++) {
                const float pc = acc[q] + ((float)d[q] / 255.0f) * da * (1 - acc[3]);
                const float v = oa > 0 ? pc / oa : 0;
                d[q] = (unsigned char)(v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255.0f + 0.5f));
            }
            d[3] = (unsigned char)(oa >= 1 ? 255 : (int)(oa * 255.0f + 0.5f));
        }
}

/* ---- the symbols --------------------------------------------------------------------------------------- */

/* Colours: the four face buttons, the dark body of the other buttons, its light rim, white. */
#define C_A      0x3E9E3Eu
#define C_B      0xD23A2Eu
#define C_X      0x2F6BD1u
#define C_Y      0xD19A06u
#define C_BODY   0x2A2C30u
#define C_RIM    0xD8DADEu
#define C_WHITE  0xFFFFFFu
#define C_KEY    0xF3F3F1u
#define C_KEYLIP 0xBDBEC2u
#define C_KEYRIM 0x55575Cu
#define C_KEYTXT 0x1E1F22u

static unsigned darker(unsigned c)
{
    const unsigned r = ((c >> 16) & 255) * 62 / 100, g = ((c >> 8) & 255) * 62 / 100, b = (c & 255) * 62 / 100;
    return r << 16 | g << 8 | b;
}

static void face(scene* s, float cx, float cy, float side, unsigned colour, const char* letter)
{
    const float r = side * 0.48f;
    add_disc(s, darker(colour), cx, cy, r, 0);
    add_disc(s, colour, cx, cy, r - side * 0.055f, 0);
    add_text(s, C_WHITE, letter, cx, cy, r * 1.3f, side * 0.46f);
}

/* LB / RB: a wide rounded bar. LT / RT: rounder on top, as a trigger is. */
static void shoulder(scene* s, float cx, float cy, float w, float h, int trigger, const char* label)
{
    float hw, hh;
    if (trigger) {
        hw = w * 0.45f; hh = h * 0.47f;
        if (hh > hw * 1.05f) hh = hw * 1.05f;
        if (hw > hh * 1.5f) hw = hh * 1.5f;
    } else {
        hw = w * 0.47f; hh = h * 0.47f;
        if (hh > hw * 0.62f) hh = hw * 0.62f;
        if (hw > hh * 2.3f) hw = hh * 2.3f;
    }
    const float m = hw < hh ? hw : hh, rim = m * 0.13f;
    const float rt = trigger ? m * 0.95f : m * 0.75f, rb = trigger ? m * 0.25f : m * 0.75f;
    add_rrect(s, C_RIM, cx, cy, hw, hh, rt, rb, 0);
    add_rrect(s, C_BODY, cx, cy, hw - rim, hh - rim, rt - rim, rb - rim, 0);
    add_text(s, C_WHITE, label, cx, cy + (trigger ? hh * 0.08f : 0), (hw - rim) * 1.6f, (hh - rim) * 0.95f);
}

static void small_round(scene* s, float cx, float cy, float side)
{
    const float r = side * 0.48f;
    add_disc(s, C_RIM, cx, cy, r, 0);
    add_disc(s, C_BODY, cx, cy, r - side * 0.07f, 0);
}

/* The keyboard's label for a key name (see the header). */
const char* gt5g_key_label(const char* key, char* buf, int cap)
{
    static const struct { const char* name; const char* label; } k_keys[] = {
        { "Enter", "\x01" }, { "Tab", "\x02" }, { "LShift", "\x03" }, { "RShift", "\x03" }, { "Shift", "\x03" },
        { "Space", "\x04" }, { "Backspace", "\x05" },
        { "Up", "\x06" }, { "Down", "\x07" }, { "Left", "\x08" }, { "Right", "\x09" },
        { "LCtrl", "CTRL" }, { "RCtrl", "CTRL" }, { "Ctrl", "CTRL" }, { "LAlt", "ALT" }, { "RAlt", "ALT" }, { "Alt", "ALT" },
        { "Insert", "INS" }, { "Delete", "DEL" }, { "Home", "HOME" }, { "End", "END" },
        { "PageUp", "PGUP" }, { "PageDown", "PGDN" }, { "CapsLock", "CAPS" }, { "Esc", "ESC" }, { "Escape", "ESC" },
        { "NumPlus", "N+" }, { "NumMinus", "N-" }, { "NumMultiply", "N*" }, { "NumDivide", "N/" }, { "NumDecimal", "N." },
        { "Comma", "," }, { "Period", "." }, { "Slash", "/" }, { "Semicolon", ";" }, { "Apostrophe", "'" },
        { "LBracket", "[" }, { "RBracket", "]" }, { "Backslash", "\\" }, { "IntlBackslash", "\\" },
        { "Minus", "-" }, { "Equals", "=" }, { "Grave", "`" }, { "Mouse4", "M4" }, { "Mouse5", "M5" },
    };
    if (cap < 2) return "";
    buf[0] = 0;
    if (!key || !*key) return buf;
    for (size_t i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++)
        if (!strcmp(key, k_keys[i].name)) { snprintf(buf, (size_t)cap, "%s", k_keys[i].label); return buf; }
    if (!strncmp(key, "Num", 3) && key[3] >= '0' && key[3] <= '9' && !key[4]) {   /* Num5: the number pad's 5 */
        snprintf(buf, (size_t)cap, "N%c", key[3]);
        return buf;
    }
    /* A-Z, 0-9, F1-F24 and anything else the font can write, in capitals */
    int n = 0;
    for (const char* p = key; *p && n < cap - 1 && n < 5; p++) {
        const char c = (char)toupper((unsigned char)*p);
        font_init();
        if (font_index(c) < 0) { buf[0] = 0; return buf; }
        buf[n++] = c;
    }
    buf[n] = 0;
    return buf;
}

static int keycap(scene* s, float cx, float cy, float w, float h, const char* key)
{
    char label[16];
    gt5g_key_label(key, label, sizeof label);
    if (!label[0]) return 0;
    const size_t n = strlen(label);
    /* A square cap for one or two signs; a long label in a wide box gets a wide cap. */
    float hh = h * 0.47f, hw = w * 0.47f;
    const float want = n <= 2 ? hh * 1.0f : hh * (0.55f + 0.32f * (float)n);
    if (hw > want) hw = want;
    if (hh > hw * 1.1f) hh = hw * 1.1f;
    const float m = hw < hh ? hw : hh, rim = m * 0.09f, lip = m * 0.16f, r = m * 0.28f;
    add_rrect(s, C_KEYRIM, cx, cy, hw, hh, r, r, 0);
    add_rrect(s, C_KEYLIP, cx, cy, hw - rim, hh - rim, r - rim, r - rim, 0);
    const float fhh = hh - rim - lip / 2.0f, fcy = cy - lip / 2.0f;
    add_rrect(s, C_KEY, cx, fcy, hw - rim * 1.6f, fhh - rim * 0.6f, r - rim, r - rim, 0);
    const float tw = (hw - rim * 2.2f) * 2.0f * 0.86f;
    const float th = (fhh - rim * 0.6f) * 2.0f * (n <= 1 ? 0.62f : 0.52f);
    add_text(s, C_KEYTXT, label, cx, fcy, tw, th);
    return 1;
}

int gt5g_paint(gt5g_image* img, float x, float y, float w, float h, int symbol, const char* key)
{
    if (!img || !img->px || w <= 1 || h <= 1) return 0;
    static scene sc;   /* one painter at a time (the renderer's thread); too big for some stacks */
    scene* s = &sc;
    s->n = 0; s->ntext = 0;
    const float cx = x + w / 2.0f, cy = y + h / 2.0f, side = w < h ? w : h;
    switch (symbol) {
    case GT5G_XA: face(s, cx, cy, side, C_A, "A"); break;
    case GT5G_XB: face(s, cx, cy, side, C_B, "B"); break;
    case GT5G_XX: face(s, cx, cy, side, C_X, "X"); break;
    case GT5G_XY: face(s, cx, cy, side, C_Y, "Y"); break;
    case GT5G_XLB: shoulder(s, cx, cy, w, h, 0, "LB"); break;
    case GT5G_XRB: shoulder(s, cx, cy, w, h, 0, "RB"); break;
    case GT5G_XLT: shoulder(s, cx, cy, w, h, 1, "LT"); break;
    case GT5G_XRT: shoulder(s, cx, cy, w, h, 1, "RT"); break;
    case GT5G_XLS:
    case GT5G_XRS:
        small_round(s, cx, cy, side);
        add_text(s, C_WHITE, symbol == GT5G_XLS ? "LS" : "RS", cx, cy, side * 0.62f, side * 0.34f);
        break;
    case GT5G_XMENU: {   /* three lines */
        small_round(s, cx, cy, side);
        static const seg lines[3] = { { -1, -1, 1, -1 }, { -1, 0, 1, 0 }, { -1, 1, 1, 1 } };
        layer* l = add(s, LY_STROKES, C_WHITE);
        if (!l) break;
        const float k = side * 0.15f;
        for (int i = 0; i < 3 && s->ntext < MAX_TEXT_SEGS; i++) {
            seg* b = &s->text[s->ntext++];
            b->x0 = cx + lines[i].x0 * k * 1.25f; b->x1 = cx + lines[i].x1 * k * 1.25f;
            b->y0 = cy + lines[i].y0 * k; b->y1 = cy + lines[i].y1 * k;
        }
        l->segs = s->text + s->ntext - 3; l->nseg = 3; l->sw = side * 0.042f;
        l->bx0 = cx - side * 0.3f; l->bx1 = cx + side * 0.3f; l->by0 = cy - side * 0.3f; l->by1 = cy + side * 0.3f;
        break;
    }
    case GT5G_XVIEW: {   /* two overlapping squares */
        small_round(s, cx, cy, side);
        const float q = side * 0.13f, o = side * 0.055f, band = side * 0.045f;
        add_rrect(s, C_WHITE, cx - o, cy - o, q, q, q * 0.3f, q * 0.3f, band);
        add_rrect(s, C_BODY, cx + o, cy + o, q, q, q * 0.3f, q * 0.3f, 0);
        add_rrect(s, C_WHITE, cx + o, cy + o, q, q, q * 0.3f, q * 0.3f, band);
        break;
    }
    case GT5G_KEY:
        if (!keycap(s, cx, cy, w, h, key)) return 0;
        break;
    default:
        return 0;
    }
    render(img, s);
    return 1;
}
