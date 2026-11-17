/*
 * gt5_binds - the text of the key and wheel maps, for the menu's "press a button to bind" screens.
 *
 *   PS3_KEYS           "button:key[/key...]" pairs split by commas, as cellPad.c (kb_init) reads it and
 *                      the launcher's KeyBinds class writes it: only the buttons that differ from the
 *                      defaults are written, so with nothing changed it stays empty.
 *   PS3_WHEEL_BUTTONS  "name=N" pairs split by commas, N the wheel's button counted from 1 (0 = none),
 *                      as wheel_host_win.c (build_map) reads it.
 *   PS3_WHEEL_PEDALS   "throttle=AXIS,brake=AXIS,clutch=AXIS", AXIS one of X Y Z RX RY RZ S0 S1 or none,
 *                      with "inv" after it for a pedal that reads backwards.
 *
 * Plain C, no dependencies. Key names are the game's own spellings.
 */
#ifndef GT5_BINDS_H
#define GT5_BINDS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- keyboard ---------------------------------------------------------------------------------------- */

#define GT5_KB_BUTTONS 24   /* the launcher's order: cross square r2 l2 circle triangle l1 r1 l3 r3 select
                               start up down left right, then the sticks' eight directions */
#define GT5_KB_KEYS    4    /* keys per button, as in cellPad.c */
#define GT5_KB_FIRST_STICK 16

typedef struct gt5_keymap {
    char key[GT5_KB_BUTTONS][GT5_KB_KEYS][16];   /* "" = empty slot; filled from slot 0 */
} gt5_keymap;

const char* gt5_kb_button(int b);                  /* "cross" ... as PS3_KEYS spells it */
const char* gt5_kb_action_label(int b, int zh);    /* "Accelerate, OK" (what it does in GT5's default layout) */
const char* gt5_kb_button_label(int b, int zh);    /* "Cross" */
void   gt5_kb_defaults(gt5_keymap* m);
void   gt5_kb_parse(const char* ps3_keys, gt5_keymap* m);
size_t gt5_kb_format(const gt5_keymap* m, char* buf, size_t cap);
/* key goes to button b: alone (add = 0) or next to the ones there; a key used elsewhere moves here. */
void   gt5_kb_bind(gt5_keymap* m, int b, const char* key, int add);
void   gt5_kb_clear(gt5_keymap* m, int b);
int    gt5_kb_count(const gt5_keymap* m, int b);

/* The game's name of a key pressed (WM_KEYDOWN / WM_SYSKEYDOWN wParam and lParam), or NULL for one
 * it doesn't know. Esc gives NULL too: it stays the quit key. */
const char* gt5_kb_key_from_vk(int vk, long long lparam);
/* A name in the game's spelling ("lalt" -> "LAlt"), or NULL. */
const char* gt5_kb_canon(const char* name);
/* For the screen: "LAlt" -> "Left Alt". */
const char* gt5_kb_pretty(const char* name);
/* The virtual-key code of a name (for the key that opens the menu), 0 = unknown. */
int gt5_kb_vk(const char* name);

/* ---- wheel ------------------------------------------------------------------------------------------- */

#define GT5_WHEEL_BUTTONS 30   /* the G27's 23 buttons, the d-pad's 4 directions, 3 pedals on buttons */
#define GT5_WHEEL_AXES    8

typedef struct gt5_wheelmap {
    int btn[GT5_WHEEL_BUTTONS];   /* -1 = as the wheel's model has it (not written), 0 = none, N = button N */
    int pedal_axis[3];            /* throttle, brake, clutch: -2 = as the model has it, -1 = none, 0..7 */
    int pedal_inv[3];             /* 1 = "inv" */
} gt5_wheelmap;

const char* gt5_wheel_button(int i);               /* "cross" ... "throttle" as PS3_WHEEL_BUTTONS spells it */
const char* gt5_wheel_button_label(int i, int zh);
const char* gt5_wheel_axis(int a);                 /* "X" ... "S1" */
const char* gt5_wheel_pedal_label(int p, int zh);
void   gt5_wheel_parse(const char* buttons, const char* pedals, gt5_wheelmap* m);
size_t gt5_wheel_format_buttons(const gt5_wheelmap* m, char* buf, size_t cap);
size_t gt5_wheel_format_pedals(const gt5_wheelmap* m, char* buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* GT5_BINDS_H */
