// gt5_menu_internal.h - shared between gt5_menu.cpp (window, input, sizes), gt5_menu_ui.cpp (what is
// on screen) and the renderers' files (gt5_menu_dx12.cpp, gt5_menu_vk.cpp). Not part of the menu's API.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include "imgui.h"
#include "gt5_binds.h"
#include "gt5_menu.h"
#include "gt5_settings.h"

namespace gt5m {

enum Sub { SUB_NONE = 0, SUB_KEYS = 1, SUB_WHEEL = 2 };
enum BindKind { BIND_NONE = 0, BIND_KEY_ROW, BIND_MENU_KEY, BIND_WHEEL_BUTTON, BIND_WHEEL_PEDAL };

// What the pad did this frame, beyond what Dear ImGui's own navigation takes.
struct PadEdges {
    bool square = false, triangle = false, start = false;
    float ry = 0.0f;          // right stick, -1 up .. +1 down, past its dead zone
    bool fast = false;        // L2 or R2 held: bigger steps
};

struct Ui {
    bool open = false;
    bool quit = false;              // [win] "Quit the game?" is asking (over the game, or over the menu)
    bool quit_focus = false;        // ... put the focus on No
    double hint_until = 0;          // [win] the first-start hint shows until then (now_s())
    int  page = 0;
    int  sub = SUB_NONE;
    bool in_content = false;        // the focus is in the rows (not in the page list)
    bool focus_nav = false;         // put the focus on the current page's entry
    bool focus_content = false;     // put the focus on the first row
    char focus_key[64] = "";        // ... or on this setting's row
    int  focus_row = -1;            // ... or on this row of a map screen
    bool popup_was_open = false;
    bool keyboard_last = true;      // the last input was keys or the pad (not the mouse)
    char help_key[64] = "";         // the setting whose help is shown
    char help_text[640] = "";       // or this text (map screens)
    bool zh = false;
    float scale = 1.0f;             // the size of everything in the window's pixels: 1 = 100 % scaling, Normal size
    char scrolled_to[64] = "";      // the row the page last scrolled to show whole (once per focus change)
    int  shown_page = -1, shown_sub = -1;   // what the page area showed last frame: another starts at its top
    // waiting for a key or a wheel button
    int  bind = BIND_NONE;
    int  bind_row = 0;
    bool bind_add = false;
    gt5_menu_wheel bind_wheel0;     // the wheel when the wait began
    double bind_t0 = 0;
    // maps being edited
    gt5_keymap keys;
    gt5_wheelmap wheel;
    // saving
    double save_at = 0;             // 0 = nothing waiting
    double saved_at = 0;
    bool   save_failed = false;
    // the readout
    float  fps_hist[120] = {};
    int    fps_n = 0;
    double fps_t = 0;
    // what the last frame laid out, in the window's pixels (gt5_menu_test_info_get)
    struct {
        float panel[4], page[4], focus[4], nav[GT5_PAGE_COUNT][4];
        float cap_px, small_px, density;
        bool  stacked, help_cut, help_inline;
        char  focus_key[64];
    } tv = {};
};

extern Ui ui;

// gt5_menu.cpp
double       now_s();
const char*  version_text();
const char*  title_text();
bool         stats_get(gt5_menu_stats* out);
bool         wheel_get(gt5_menu_wheel* out);
void         bind_begin(int kind, int row, bool add);
void         bind_cancel();
void         open_url(const char* url);
void         open_logs_folder();
void         request_close();
void         quit_ask();                     // [win] the About page's "Quit the game"
void         quit_answer(bool yes);          // [win] the quit question's Yes or No
bool         pad_connected();                // [win] a controller is there (for the hint's words)
void         need_fallback_fonts();          // text beyond Latin is about to be drawn
unsigned     frame_w();
unsigned     frame_h();
void         note_changed();                 // a setting changed: save soon
int          notices_get(char out[][192], int max);   // the notices to show now, in the menu's language

// gt5_menu.cpp, for the renderers' files. Starting: core_begin (the window, the size of the picture the
// menu is drawn into, the About page's texts), the renderer's own objects, core_context (Dear ImGui's
// context and the first font), the renderer's Dear ImGui backend (core_context_abort when it fails),
// core_finish. A frame: core_build_frame (false: nothing to show; called only when the renderer has
// something to draw the menu into: it also tells the quit question the picture still comes), then the renderer draws
// ImGui::GetDrawData(). Stopping: core_shutdown_begin, the renderer's GPU idle and its backend shut down,
// core_shutdown_end, the renderer's own objects. All on the window's thread.
void         core_begin(HWND hwnd, unsigned w, unsigned h, const char* version, const char* title);
void         core_context();
void         core_context_abort();
void         core_finish(void (*renderer_new_frame)(), const char* what);   // what: "frame", "swap chain" (the log line)
bool         core_inited();
bool         core_build_frame();
void         core_set_size(unsigned w, unsigned h);   // the picture the menu is drawn into has a new size
void         core_shutdown_begin();
void         core_shutdown_end();

// gt5_menu_ui.cpp
void         ui_opened();
void         ui_build(const PadEdges& pad);  // the menu, between NewFrame and Render
void         ui_build_osd();                 // the corner readout alone
void         ui_build_quit();                // [win] "Quit the game?" alone (over the game or the menu)
void         ui_build_hint(float alpha);     // [win] the first-start hint in the corner
void         ui_build_notices();             // the notices (gt5_menu_notice), top right
void         ui_key_taken(const char* key_name);   // a key arrived for the row that waits (NULL = cancelled)
void         ui_apply_style(float scale);

}  // namespace gt5m
