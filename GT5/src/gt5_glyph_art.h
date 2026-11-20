/* Button symbols drawn in code (gt5_glyphs.cpp): an Xbox-style controller's buttons and keyboard key caps,
 * painted into an RGBA picture at any size. No pictures from anywhere, no font files: discs, rounded
 * shapes and a small stroke font of our own (capitals, digits, a few signs and key symbols).
 *
 * Plain C, no dependencies beyond the C library. */
#ifndef GT5_GLYPH_ART_H
#define GT5_GLYPH_ART_H

#ifdef __cplusplus
extern "C" {
#endif

/* What a symbol shows. */
enum {
    GT5G_NONE = 0,
    /* an Xbox-style controller */
    GT5G_XA, GT5G_XB, GT5G_XX, GT5G_XY,          /* face buttons: a letter in a coloured disc */
    GT5G_XLB, GT5G_XRB, GT5G_XLT, GT5G_XRT,      /* bumpers and triggers */
    GT5G_XLS, GT5G_XRS,                          /* stick presses */
    GT5G_XVIEW, GT5G_XMENU,                      /* the two small middle buttons */
    /* (the d-pad has the same shape on both pads: the game's own is kept) */
    /* a keyboard key cap, its label from a key name (gt5g_paint's key argument) */
    GT5G_KEY,
    GT5G_COUNT
};

typedef struct {
    unsigned char* px;   /* RGBA, straight alpha, rows of w * 4 bytes */
    int w, h;
} gt5g_image;

/* Paints a symbol over img (alpha-blended over what is there) inside the box x, y, w, h (pixels, may be
 * fractional): centred, as large as fits, its shape following the box's (a bumper in a wide box is
 * wider). key: for GT5G_KEY, the key's name as the port writes it (Z, Enter, LShift, Num5, F3, Up,
 * Comma ...). Returns 1 when it drew something, 0 for GT5G_NONE or a key it has no label for. */
int gt5g_paint(gt5g_image* img, float x, float y, float w, float h, int symbol, const char* key);

/* The label a key cap shows for a key name ("" when none), for logs and tests. Symbols for Enter, Tab,
 * Shift, Space, Backspace and the arrows are control characters 1-9 of the stroke font. */
const char* gt5g_key_label(const char* key, char* buf, int cap);

#ifdef __cplusplus
}
#endif
#endif /* GT5_GLYPH_ART_H */
