// gt5_menu_internal.h - shared between gt5_menu.cpp (window, input, Direct3D 12) and gt5_menu_ui.cpp
// (what is on screen). Not part of the menu's API.
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
    float scale = 1.0f;             // the size of everything, 1 = a 1080-line frame at normal size
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
void         need_fallback_fonts();          // text beyond Latin is about to be drawn
unsigned     frame_w();
unsigned     frame_h();
void         note_changed();                 // a setting changed: save soon

// gt5_menu_ui.cpp
void         ui_opened();
void         ui_build(const PadEdges& pad);  // the menu, between NewFrame and Render
void         ui_build_osd();                 // the corner readout alone
void         ui_key_taken(const char* key_name);   // a key arrived for the row that waits (NULL = cancelled)
void         ui_apply_style(float scale);

}  // namespace gt5m
