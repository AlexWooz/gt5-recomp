// gt5_menu_ui.cpp - what the settings menu shows: the pages, their rows, the key and wheel maps, the
// readout, the help at the bottom. Everything on screen comes from the registry (gt5_settings.h); the
// texts here are only the menu's own furniture. UTF-8.
//
// Moving about, the same with keys and with a controller:
//   page list    Up/Down pick a page, Right or Enter/Cross goes into it, Esc/Circle closes the menu
//   a page       Up/Down move, Left/Right change the value, Enter/Cross opens the list or presses,
//                Esc/Circle goes back to the page list, Page Up/Down or L1/R1 change page
// The mouse works on everything as usual.
//
// Sizes are the window's pixels (gt5_menu.cpp lays the menu out in the client area and draws it into
// the frame scaled): S(v) is v at 100 % scaling and the Normal size, base_px() the body text's size.
// The layout follows the room it has: a narrower page list and the value under its label in a narrow
// window, a panel of at most about 1500 x 1000 (at 100 %) in a large one, pages that scroll.
#include "gt5_menu_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "imgui_internal.h"   // NavMoveRequestCancel, NavMoveRequestTryWrapping, ScrollToRect, ImTextCharFromUtf8

namespace gt5m {

namespace {

// The launcher's colours (GT5Launcher.cs, class Look), so the two look like one program.
const ImVec4 kBg(14 / 255.f, 16 / 255.f, 22 / 255.f, 0.97f);
const ImVec4 kPanel(24 / 255.f, 27 / 255.f, 36 / 255.f, 1.0f);
const ImVec4 kNav(19 / 255.f, 21 / 255.f, 29 / 255.f, 1.0f);
const ImVec4 kNavHot(34 / 255.f, 38 / 255.f, 50 / 255.f, 1.0f);
const ImVec4 kText(232 / 255.f, 234 / 255.f, 240 / 255.f, 1.0f);
const ImVec4 kDim(150 / 255.f, 156 / 255.f, 172 / 255.f, 1.0f);
const ImVec4 kAccent(214 / 255.f, 32 / 255.f, 40 / 255.f, 1.0f);
const ImVec4 kAccentHot(236 / 255.f, 56 / 255.f, 62 / 255.f, 1.0f);
const ImVec4 kBlue(64 / 255.f, 140 / 255.f, 230 / 255.f, 1.0f);
const ImVec4 kLine(44 / 255.f, 48 / 255.f, 62 / 255.f, 1.0f);
const ImVec4 kLive(110 / 255.f, 205 / 255.f, 130 / 255.f, 1.0f);
const ImVec4 kNext(240 / 255.f, 180 / 255.f, 70 / 255.f, 1.0f);
const ImVec4 kRowHot(1.0f, 1.0f, 1.0f, 0.045f);      // the row with the focus, behind its label and value
const ImVec4 kRowLine(44 / 255.f, 48 / 255.f, 62 / 255.f, 0.55f);

const char* T(const char* en, const char* zh) { return ui.zh && zh ? zh : en; }
const char* label_of(const gt5_setting_def* d) { return ui.zh && d->label_zh ? d->label_zh : d->label_en; }
const char* help_of(const gt5_setting_def* d) { return ui.zh && d->help_zh ? d->help_zh : d->help_en; }
const char* choice_label(const gt5_choice* c) { return ui.zh && c->label_zh ? c->label_zh : c->label_en; }

const char* page_name(int p)
{
    switch (p) {
    case GT5_PAGE_DISPLAY: return T("Display", "画面");                 // (launcher) nav.*
    case GT5_PAGE_PERFORMANCE: return T("Performance", "性能");
    case GT5_PAGE_CONTROLS: return T("Controls", "操控");
    case GT5_PAGE_GAME: return T("Game", "游戏");
    case GT5_PAGE_SOUND: return T("Sound", "声音");
    default: return T("About", "关于");
    }
}

const char* row_idle_reason(const char* key);

// The one test for "is there a row for this setting" (the page, the help box's size, the help under a
// row). A row of its own on a page: not one the launcher keeps for itself, not part of another row
// (GT5_SF_NO_ROW), and not one the renderer in use does nothing with (gt5_setting_hide: Vulkan, until
// it has it). With now, also not one that can't be used right now, which is simply not shown, nothing
// greyed out: the renderer didn't enable it (GT5_SF_NEEDS_HOST), or another setting leaves it idle
// (row_idle_reason). Without now: every row that may come and go (the help box is sized once for all).
bool has_row(const gt5_setting_def* d, bool now = false)
{
    if (d->page == GT5_PAGE_NONE || (d->flags & GT5_SF_NO_ROW)) return false;
    const unsigned flags = gt5_setting_flags(d->key);
    if (flags & GT5_SF_HIDDEN) return false;
    if (!now) return true;
    if ((flags & GT5_SF_NEEDS_HOST) && !(flags & GT5_SF_ENABLED)) return false;
    return row_idle_reason(d->key) == nullptr;
}

float S(float v) { return v * ui.scale; }
float base_px() { return ImGui::GetStyle().FontSizeBase; }
// The three text sizes besides the body's: help under the page, and the small one (a row's status,
// the keys at the bottom, the corner readout). Nothing is smaller than 0.82 of the body text.
float help_px() { return base_px() * 0.9f; }
float small_px() { return base_px() * 0.82f; }
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void rect_out(float* out, const ImVec2& a, const ImVec2& b) { out[0] = a.x; out[1] = a.y; out[2] = b.x; out[3] = b.y; }

// The text as it fits in max_w at the current size: cut after a whole character, with "…". A value
// that doesn't fit its box is shortened rather than cut through a letter; the list itself shows it whole.
const char* fit_text(const char* text, float max_w, char* buf, size_t cap)
{
    if (!text || max_w <= 0.0f || ImGui::CalcTextSize(text).x <= max_w) return text;
    static const char ell[] = "\xE2\x80\xA6";
    const float room = max_w - ImGui::CalcTextSize(ell).x;
    const char* end = text + strlen(text);
    const char* p = text;
    size_t keep = 0;
    while (p < end) {
        unsigned int c = 0;
        const int n = ImTextCharFromUtf8(&c, p, end);
        if (n <= 0 || ImGui::CalcTextSize(text, p + n).x > room) break;
        p += n;
        if (c != ' ' && c != '(' && c != 0x3000 && c != 0xFF08) keep = (size_t)(p - text);
    }
    if (keep + sizeof ell > cap) keep = cap > sizeof ell ? cap - sizeof ell : 0;
    snprintf(buf, cap, "%.*s%s", (int)keep, text, ell);
    return buf;
}

// Left / right on the focused row: -1, +1 or 0. Taken away from Dear ImGui's own navigation, which
// would leave the row for whatever is beside it.
int step_keys()
{
    int dir = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true)) dir = -1;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true)) dir = 1;
    if (dir) ImGui::NavMoveRequestCancel();
    return dir;
}

void set_help_setting(const gt5_setting_def* d)
{
    snprintf(ui.help_key, sizeof ui.help_key, "%s", d->key);
    ui.help_text[0] = 0;
}
void set_help_text(const char* text)
{
    ui.help_key[0] = 0;
    snprintf(ui.help_text, sizeof ui.help_text, "%s", text);
}

void changed() { note_changed(); }

// The menu's own help texts (the settings' are in the registry).
const char* help_nav() { return T("Up and Down pick a page; Right, Enter or Cross goes into it.", "上下选择页面；按右、Enter 或 ✕ 进入。"); }
const char* help_keys()
{
    return T("Enter or Cross on a row, then press the key you want (Esc cancels). Insert or Square adds a second key; Delete or Triangle clears the row.",
             "在一行上按 Enter 或 ✕，然后按下想要的键（Esc 取消）。Insert 或 □ 添加第二个键；Delete 或 △ 清除该行。");
}
const char* help_wheel()
{
    return T("Enter or Cross on a row, then press the wheel's button or push the pedal down (Esc cancels). Insert or Square: none (a pedal: reversed). Delete or Triangle: as the wheel has it.",
             "在一行上按 Enter 或 ✕，然后按下方向盘上的按钮或把踏板踩到底（Esc 取消）。Insert 或 □：无（踏板：反向）。Delete 或 △：按方向盘默认。");
}
const char* help_link(bool url)
{
    return url ? T("Opens the page in your web browser.", "在浏览器中打开该页面。")
               : T("The game's log files. Add them when you report a problem.", "游戏的日志文件。报告问题时请附上。");
}
const char* note_unavailable() { return T("Not in this version yet.", "此版本尚不可用。"); }
const char* note_restart() { return T("A change applies after the game restarts.", "更改在重启游戏后生效。"); }

// The help box's height: what the longest help any row or screen can show needs at this width, so the
// box keeps one size while the focus moves (a box that grew and shrank would move the page under it).
// Measured again only when the width, the text size or the language changes.
float help_box_need(float wrap_w, float pad_y)
{
    static float last_w = -1.0f, last_px = -1.0f, need = 0.0f;
    static bool last_zh = false;
    if (wrap_w == last_w && base_px() == last_px && ui.zh == last_zh) return need;
    last_w = wrap_w; last_px = base_px(); last_zh = ui.zh;
    const float gap = ImGui::GetStyle().ItemSpacing.y;
    float most = 0.0f;
    ImGui::PushFont(nullptr, help_px());
    const float note_h = ImGui::CalcTextSize(note_restart(), nullptr, false, wrap_w).y;
    const char* own[] = { help_nav(), help_keys(), help_wheel(), help_link(true), help_link(false) };
    for (const char* t : own) {
        const float h = ImGui::CalcTextSize(t, nullptr, false, wrap_w).y;
        if (h > most) most = h;
    }
    for (int i = 0; i < gt5_settings_count(); i++) {
        const gt5_setting_def* d = gt5_settings_at(i);
        if (!has_row(d)) continue;
        float h = 0.0f;
        if (help_of(d)) h += ImGui::CalcTextSize(help_of(d), nullptr, false, wrap_w).y + gap;
        if (!strcmp(d->key, "LD_FRAMEGEN")) {
            const gt5_setting_def* m = gt5_settings_find("LD_FRAMEGEN_MULT");
            if (m && help_of(m) && has_row(m)) h += ImGui::CalcTextSize(help_of(m), nullptr, false, wrap_w).y + gap;
        }
        h += note_h + gap;          // "applies after a restart" or "not in this version yet": the game may say either at start-up
        ImGui::PopFont();
        h += ImGui::CalcTextSize(label_of(d), nullptr, false, wrap_w).y + gap;   // the label, in the body's size
        ImGui::PushFont(nullptr, help_px());
        if (h - gap > most) most = h - gap;
    }
    ImGui::PopFont();
    need = floorf(most + pad_y * 2.0f + 1.0f);
    return need;
}

// The focused item, for the tests.
void note_focus(const char* key)
{
    rect_out(ui.tv.focus, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    snprintf(ui.tv.focus_key, sizeof ui.tv.focus_key, "%s", key ? key : "");
}

// A row that has just taken the focus is scrolled wholly into view (Dear ImGui shows the widget; in a
// narrow window the label sits above it). Once per change of focus, so the right stick can still scroll.
void show_whole_row(const char* id, const ImVec2& a, const ImVec2& b)
{
    if (!ui.keyboard_last || !strcmp(ui.scrolled_to, id)) return;
    snprintf(ui.scrolled_to, sizeof ui.scrolled_to, "%s", id);
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (a.y < w->InnerRect.Min.y || b.y > w->InnerRect.Max.y)
        ImGui::ScrollToRect(w, ImRect(a, b), ImGuiScrollFlags_KeepVisibleEdgeY);
}

// ---- one row ------------------------------------------------------------------------------------------

struct RowState { bool first = true; bool any_focus = false; int count = 0; };
RowState g_rows;
float g_help_needs = 0.0f;      // the height the help text took in the last frame
bool g_inline_help = false;     // the focused row shows its help itself, there is no box
bool g_show_heading = true;     // room for the page's name above its rows

// How a page's rows are laid out, from its width: the label on the left and the value beside it, or
// in a narrow window the value under its label, the full width.
struct RowLayout { bool stacked = false; float full = 0, label_w = 0, widget_w = 0; };
RowLayout g_row;

void plan_rows()
{
    const float full = ImGui::GetContentRegionAvail().x, b = base_px();
    g_row.full = full;
    g_row.stacked = full < b * 24.0f;
    if (g_row.stacked) { g_row.label_w = g_row.widget_w = full; }
    else {
        g_row.label_w = floorf(clampf(full * 0.44f, b * 11.0f, b * 21.0f));
        g_row.widget_w = full - g_row.label_w;
        if (g_row.widget_w > b * 24.0f) g_row.widget_w = b * 24.0f;
    }
    ui.tv.stacked = g_row.stacked;
}

// The focus goes to the widget that comes next when this row was asked for. True when it does (a slider
// then takes the focus without turning into a text box: see the slider in setting_row).
bool take_focus_if_asked(const char* key)
{
    const bool by_key = ui.focus_key[0] && key && !strcmp(ui.focus_key, key);
    const bool asked = by_key || (ui.focus_content && g_rows.first && !ui.focus_key[0]);
    if (asked) {
        ImGui::SetKeyboardFocusHere();
        ui.focus_content = false;
        ui.focus_key[0] = 0;
    }
    g_rows.first = false;
    return asked;
}

// A switch: Enter/Cross/click turns it over, Left is off, Right is on.
bool toggle(bool* v, bool* focused, bool* hovered)
{
    const float h = ImGui::GetFrameHeight(), w = h * 1.9f, r = h * 0.5f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##t", ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    *focused = ImGui::IsItemFocused();
    *hovered = ImGui::IsItemHovered();
    if (*focused) { const int dir = step_keys(); if (dir && (dir > 0) != *v) pressed = true; }
    if (pressed) *v = !*v;
    const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(*v ? kAccent : (*hovered ? kNavHot : kPanel)), r);
    dl->AddCircleFilled(ImVec2(p.x + (*v ? w - r : r), p.y + r), r - S(4), ImGui::GetColorU32(*v ? kText : kDim));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(*v ? kText : kDim, "%s", *v ? T("On", "开") : T("Off", "关"));   // (launcher) on / off
    // the switch is the row's item (for the focus and the tests), not the word beside it
    ImGui::GetCurrentContext()->LastItemData.Rect = ImRect(bmin, bmax);
    return pressed;
}

// A list of choices. Left/Right step through the ones this build can do; Enter/Cross opens the list.
// Returns the index picked, or -1. why (optional) gives the reason a choice is greyed out; without one
// the choice is one this version can't do yet.
int choice_box(const char* preview, int count, int cur, const char* (*label)(int, void*), bool (*available)(int, void*), void* user,
               bool* focused, bool* hovered, float width, const char* (*why)(int, void*) = nullptr)
{
    int picked = -1;
    char shown[320];
    const float room = width - ImGui::GetFrameHeight() - ImGui::GetStyle().FramePadding.x * 2.0f;
    // The open list: up to 20 choices, but never more than seven tenths of the window's height (it
    // scrolls), so a long one stays inside a small window.
    const ImGuiStyle& st = ImGui::GetStyle();
    float list_h = (ImGui::GetFontSize() + st.ItemSpacing.y) * 20.0f - st.ItemSpacing.y + st.WindowPadding.y * 2.0f;
    if (list_h > ImGui::GetIO().DisplaySize.y * 0.7f) list_h = floorf(ImGui::GetIO().DisplaySize.y * 0.7f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, list_h));
    const bool open = ImGui::BeginCombo("##v", fit_text(preview, room, shown, sizeof shown), 0);
    *focused = ImGui::IsItemFocused();
    *hovered = ImGui::IsItemHovered();
    if (open) {
        for (int i = 0; i < count; i++) {
            // A choice that can't be used on this PC right now is simply not in the list: no greyed entries, no
            // reasons to read. (The current value is always listed, so an old setting still shows.)
            if (!available(i, user) && i != cur) continue;
            if (ImGui::Selectable(label(i, user), i == cur)) picked = i;
            if (i == cur) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    } else if (*focused) {
        const int dir = step_keys();
        for (int i = cur + dir; dir && i >= 0 && i < count; i += dir)
            if (available(i, user)) { picked = i; break; }
        if (dir && cur < 0 && count > 0 && available(0, user)) picked = 0;
    }
    return picked;
}

struct DefChoices { const gt5_setting_def* d; };
const char* def_choice_label(int i, void* u) { return choice_label(&((DefChoices*)u)->d->choices[i]); }
bool def_choice_ok(int i, void* u) { return !(gt5_setting_choice_flags(((DefChoices*)u)->d->key, i) & GT5_CF_UNAVAILABLE); }
// What the renderer said about a greyed choice ("not added yet: launcher, Display, Frame generation").
const char* def_choice_why(int i, void* u)
{
    static char note[128];
    return gt5_setting_choice_note_of(((DefChoices*)u)->d->key, i, note, sizeof note) ? note : nullptr;
}

// Frame generation: one switch (LD_FRAMEGEN; on also sets the multiplier to 2x when it has none), the
// "Generator" row (LD_FG_METHOD, empty = automatic) and the "Multiplier" row (LD_FRAMEGEN_MULT).
bool fg_is_on()
{
    char v[24];
    gt5_setting_stored("LD_FRAMEGEN", v, sizeof v);
    return (v[0] && strcmp(v, "0") && _stricmp(v, "off") && _stricmp(v, "no")) || gt5_setting_int("LD_FRAMEGEN_MULT", 1) >= 2;
}
// AMD's and NVIDIA's generators make one frame between two: 2x. True when one of them is named, or is
// the one the automatic choice is running right now (the renderer's state line starts with its name).
bool fg_one_frame()
{
    char m[16];
    gt5_setting_stored("LD_FG_METHOD", m, sizeof m);
    if (!_stricmp(m, "fsr") || !_stricmp(m, "dlss")) return true;
    if (m[0]) return false;
    gt5_menu_stats st;
    return stats_get(&st) && (!strncmp(st.fg_state, "NVIDIA", 6) || !strncmp(st.fg_state, "AMD", 3));
}
const char* fg_label(int i, void*) { return i ? T("On", "开") : T("Off", "关"); }
bool fg_ok(int, void*) { return true; }

// A row that only means something while another setting has a certain value is shown greyed out, with
// its value and the reason, and can't be changed: the DLSS model with no DLSS mode picked, the window's
// size outside window mode, mouse steering's numbers with mouse steering off. Returns the reason, or
// nullptr when the row is in use.
const char* row_idle_reason(const char* key)
{
    char v[64];
    if (!strcmp(key, "LD_DLSS_PRESET")) {
        gt5_setting_stored("LD_AA", v, sizeof v);
        if (_strnicmp(v, "dlaa", 4) && _strnicmp(v, "dlss", 4))
            return T("pick DLAA or a DLSS mode above first", "请先在上方选择 DLAA 或 DLSS 模式");
    } else if (!strcmp(key, "LD_DLSS_SCALE")) {
        gt5_setting_stored("LD_AA", v, sizeof v);
        if (_stricmp(v, "dlss_c"))
            return T("only for \"DLSS: custom scale\" above", "仅用于上方的“DLSS：自定义比例”");
    } else if (!strcmp(key, "LD_FG_METHOD")) {
        if (!fg_is_on()) return T("turn frame generation on first", "请先开启帧生成");
    } else if (!strcmp(key, "LD_FRAMEGEN_MULT")) {
        if (!fg_is_on()) return T("turn frame generation on first", "请先开启帧生成");
        if (fg_one_frame()) return T("2x with this generator", "此生成器为 2x");
    } else if (!strcmp(key, "PS3_WINDOW_SIZE")) {
        gt5_setting_stored("GT5_LAUNCH_WINDOW", v, sizeof v);
        if (v[0] && _stricmp(v, "windowed"))
            return T("only in window mode", "仅在窗口模式下有效");
    } else if (!strcmp(key, "PS3_MOUSE_STEER_SENS") || !strcmp(key, "PS3_MOUSE_STEER_CENTER")) {
        if (!gt5_setting_bool("PS3_MOUSE_STEER", 0))
            return T("turn \"Steer with the mouse\" on first", "请先开启“用鼠标转向”");
    } else if (!strcmp(key, "PS3_WHEEL_RANGE") || !strcmp(key, "PS3_WHEEL_FFB") || !strcmp(key, "PS3_WHEEL_FFB_INVERT") ||
               !strcmp(key, "PS3_WHEEL_BUTTONS")) {
        gt5_setting_stored("PS3_WHEEL", v, sizeof v);
        if (!_stricmp(v, "off") || !_stricmp(v, "0") || !_stricmp(v, "no"))
            return T("the steering wheel is switched off above", "上方已关闭方向盘");
        if (gt5_setting_bool("PS3_MOUSE_STEER", 0))
            return T("not used while the mouse steers", "鼠标转向时不使用");
    }
    return nullptr;
}

// What is worth knowing about a row, in small text under its label: why it can't be used, what the
// renderer is doing with it, or that it counts after a restart. Nothing for a row that simply works.
void status_line(const gt5_setting_def* d, bool unavailable, const char* idle, float wrap_x)
{
    gt5_menu_stats st;
    const bool have = stats_get(&st);
    // The anti-aliasing row says what the renderer says: why DLSS can't be used here (no plug-in, the
    // graphics card, the driver), or what it is doing right now.
    const bool aa_row = !strcmp(d->key, "LD_AA") && have && st.aa_state[0];
    const char* text = nullptr;
    const char* text2 = nullptr;
    ImVec4 col = kDim;
    if (unavailable && aa_row) { text = st.aa_state; col = kNext; text2 = T("add NVIDIA DLSS in the launcher first", "请先在启动器中添加 NVIDIA DLSS"); }
    else if (unavailable) text = T("not in this version yet", "此版本尚不可用");
    else if (idle) text = idle;
    else if (!strcmp(d->key, "LD_FRAMEGEN") && have && st.fg_state[0]) { text = st.fg_state; col = kLive; }   // what is made and shown now, or why not
    else if (aa_row) {
        char cur[32];
        gt5_setting_stored("LD_AA", cur, sizeof cur);
        if (cur[0]) { text = st.aa_state; col = kLive; }    // "DLAA, 2304x972" / "not in this frame (menu or 2D)"
    }
    else if (gt5_setting_pending_restart(d->key)) { text = T("restart the game to apply", "请重启游戏以应用"); col = kNext; }
    else if (gt5_setting_apply_of(d->key) != GT5_APPLY_LIVE) text = T("applies after a restart", "重启游戏后生效");
    if (!text) return;
    ImGui::PushFont(nullptr, small_px());
    ImGui::PushTextWrapPos(wrap_x);
    ImGui::TextColored(col, "%s", text);
    if (text2) ImGui::TextColored(kDim, "%s", text2);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
}

void key_names(const char* stored, char* out, size_t cap)
{
    snprintf(out, cap, "%s", stored && *stored ? gt5_kb_pretty(gt5_kb_canon(stored) ? gt5_kb_canon(stored) : stored) : "F1");
}

// A button showing a value: the text shortened to fit, the same id whatever it says, and on the left
// like the lists' values beside it (centred for a plain action: centre = true).
bool value_button(const char* text, float w, const ImVec4* colour = nullptr, bool centre = false)
{
    char fitted[320], label[340];
    snprintf(label, sizeof label, "%s###v", fit_text(text, w - ImGui::GetStyle().FramePadding.x * 2.0f, fitted, sizeof fitted));
    if (colour) ImGui::PushStyleColor(ImGuiCol_Text, *colour);
    if (!centre) ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    const bool pressed = ImGui::Button(label, ImVec2(w, 0));
    if (!centre) ImGui::PopStyleVar();
    if (colour) ImGui::PopStyleColor();
    return pressed;
}

void setting_row(const gt5_setting_def* d, const PadEdges& pad)
{
    ImGui::PushID(d->key);
    const unsigned flags = gt5_setting_flags(d->key);
    const bool unavailable = (flags & GT5_SF_NEEDS_HOST) && !(flags & GT5_SF_ENABLED);
    const char* idle = unavailable ? nullptr : row_idle_reason(d->key);
    // A row that can't be used right now never gets here: the page asks has_row(d, true) first.
    const float label_w = g_row.label_w, widget_w = g_row.widget_w;
    const float spacing_y = ImGui::GetStyle().ItemSpacing.y;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // a faint line between rows
    if (g_rows.count++ > 0) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float y = floorf(p.y - (spacing_y + S(6)) * 0.5f) + 0.5f;
        dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + g_row.full, y), ImGui::GetColorU32(kRowLine), 1.0f);
    }
    const ImVec2 at = ImGui::GetCursorPos();
    const ImVec2 at_screen = ImGui::GetCursorScreenPos();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    // the label, and under it what is worth knowing about the row
    if (!g_row.stacked) ImGui::AlignTextToFramePadding();
    ImGui::PushTextWrapPos(at.x + label_w - (g_row.stacked ? 0.0f : S(20)));
    ImGui::TextColored(unavailable || idle ? kDim : kText, "%s", label_of(d));
    ImGui::PopTextWrapPos();
    float label_bottom = ImGui::GetItemRectMax().y;
    ImGui::SetCursorScreenPos(ImVec2(at_screen.x, label_bottom + S(2)));
    const float before_status = ImGui::GetCursorPosY();
    status_line(d, unavailable, idle, at.x + label_w - (g_row.stacked ? 0.0f : S(20)));
    if (ImGui::GetCursorPosY() > before_status) label_bottom = ImGui::GetItemRectMax().y;

    if (g_row.stacked) ImGui::SetCursorScreenPos(ImVec2(at_screen.x, label_bottom + S(6)));
    else ImGui::SetCursorPos(ImVec2(at.x + label_w, at.y));
    ImGui::SetNextItemWidth(widget_w);
    const bool focus_given = take_focus_if_asked(d->key);
    bool focused = false, hovered = false;
    char cur[1200];
    gt5_setting_stored(d->key, cur, sizeof cur);

    if (unavailable) {
        // Still a place the focus can rest on, so its help can be read with a controller.
        value_button("—", widget_w, &kDim);
        focused = ImGui::IsItemFocused(); hovered = ImGui::IsItemHovered();
    } else if (idle) {
        // Not in use right now (row_idle_reason): its value, greyed out, and nothing to change. Still a
        // place the focus can rest on, so its help can be read.
        char shown[160];
        snprintf(shown, sizeof shown, "%s", cur[0] ? cur : T("(not set)", "（未设置）"));
        if (d->type == GT5_SET_CHOICE) {
            for (int i = 0; i < d->n_choices; i++)
                if (!_stricmp(d->choices[i].value, cur)) snprintf(shown, sizeof shown, "%s", choice_label(&d->choices[i]));
        } else if (d->type == GT5_SET_BOOL) {
            snprintf(shown, sizeof shown, "%s", !_stricmp(cur, d->on_value ? d->on_value : "1") ? T("On", "开") : T("Off", "关"));
        } else if (d->type == GT5_SET_INT || d->type == GT5_SET_FLOAT) {
            snprintf(shown, sizeof shown, d->type == GT5_SET_INT ? "%.0f%s" : "%.1f%s", gt5_setting_float(d->key, d->num_def),
                     d->unit ? d->unit : "");
        } else if (!strcmp(d->key, "PS3_WHEEL_BUTTONS")) {
            snprintf(shown, sizeof shown, "%s", T("Change...", "更改..."));
        }
        value_button(shown, widget_w, &kDim);
        focused = ImGui::IsItemFocused(); hovered = ImGui::IsItemHovered();
    } else if (!strcmp(d->key, "LD_FRAMEGEN")) {
        // One switch. On gives 2x unless a multiplier is already chosen; Off clears both (a multiplier
        // alone turns frame generation on, which is how the launcher's older setting wrote it).
        const int now = fg_is_on() ? 1 : 0;
        const int pick = choice_box(fg_label(now, nullptr), 2, now, fg_label, fg_ok, nullptr, &focused, &hovered, widget_w);
        if (pick >= 0 && pick != now) {
            gt5_setting_set("LD_FRAMEGEN", pick ? "1" : "");
            if (!pick) gt5_setting_set("LD_FRAMEGEN_MULT", "");
            else if (gt5_setting_int("LD_FRAMEGEN_MULT", 1) < 2) gt5_setting_set("LD_FRAMEGEN_MULT", "2");
            changed();
        }
    } else if (!strcmp(d->key, "PS3_KEYS")) {
        if (value_button(T("Change...", "更改..."), widget_w)) {
            gt5_kb_parse(cur, &ui.keys);
            ui.sub = SUB_KEYS; ui.focus_content = true;
        }
        focused = ImGui::IsItemFocused(); hovered = ImGui::IsItemHovered();
    } else if (!strcmp(d->key, "PS3_WHEEL_BUTTONS")) {
        if (value_button(T("Change...", "更改..."), widget_w)) {
            char p[200];
            gt5_setting_stored("PS3_WHEEL_PEDALS", p, sizeof p);
            gt5_wheel_parse(cur, p, &ui.wheel);
            ui.sub = SUB_WHEEL; ui.focus_content = true;
        }
        focused = ImGui::IsItemFocused(); hovered = ImGui::IsItemHovered();
    } else if (!strcmp(d->key, "GT5_MENU_KEY")) {
        char name[48];
        key_names(cur, name, sizeof name);
        const bool waiting = ui.bind == BIND_MENU_KEY;
        if (waiting) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        if (value_button(waiting ? T("Press a key...", "请按一个键...") : name, widget_w) && !waiting)   // (launcher) kb.press
            bind_begin(BIND_MENU_KEY, 0, false);
        if (waiting) ImGui::PopStyleColor();
        focused = ImGui::IsItemFocused(); hovered = ImGui::IsItemHovered();
    } else switch (d->type) {
    case GT5_SET_BOOL: {
        bool v = !_stricmp(cur, d->on_value ? d->on_value : "1");
        if (toggle(&v, &focused, &hovered)) { gt5_setting_set(d->key, v ? (d->on_value ? d->on_value : "1") : (d->off_value ? d->off_value : "")); changed(); }
        break;
    }
    case GT5_SET_CHOICE: {
        int now = -1;
        // Resolution with nothing chosen is the game's own 1080p picture: shown as 1080p.
        const char* cur_shown = (!strcmp(d->key, "LD_RES_TARGET") && !cur[0]) ? "1080" : cur;
        for (int i = 0; i < d->n_choices; i++) if (!_stricmp(d->choices[i].value, cur_shown)) now = i;
        DefChoices dc = { d };
        const char* preview = now >= 0 ? choice_label(&d->choices[now]) : cur[0] ? cur : T("(not set)", "（未设置）");
        const int pick = choice_box(preview, d->n_choices, now, def_choice_label, def_choice_ok, &dc, &focused, &hovered, widget_w, def_choice_why);
        if (pick >= 0 && pick != now) {
            gt5_setting_set(d->key, d->choices[pick].value);
            // The generator is picked by LD_FRAMEGEN's own value (1 or 2): keep it in step while frame
            // generation is on (also when only the multiplier switched it on).
            // AMD's and NVIDIA's generators make one frame: a 3x or 4x multiplier goes back to 2x.
            if (!strcmp(d->key, "LD_FG_METHOD") && fg_one_frame() && gt5_setting_int("LD_FRAMEGEN_MULT", 1) > 2)
                gt5_setting_set("LD_FRAMEGEN_MULT", "2");
            changed();
        }
        break;
    }
    case GT5_SET_INT:
    case GT5_SET_FLOAT: {
        float v = (float)gt5_setting_float(d->key, d->num_def);
        char fmt[32];
        snprintf(fmt, sizeof fmt, d->type == GT5_SET_INT ? "%%.0f%s" : "%%.1f%s", d->unit ? (!strcmp(d->unit, "%") ? "%%" : d->unit) : "");
        // A range over two decades steps evenly by ratio (mouse speed 1-1000); one from 0 (volume) can't.
        const bool wide = d->num_min > 0 && d->num_max / d->num_min >= 100;
        // A slider given the focus (the first row of a page, Volume on Sound) would take it as a text box
        // (Dear ImGui activates a focused slider for typing): not in that frame, so Left/Right move it.
        bool edited = ImGui::SliderFloat("##v", &v, (float)d->num_min, (float)d->num_max, fmt,
                                         ImGuiSliderFlags_AlwaysClamp | (wide ? ImGuiSliderFlags_Logarithmic : 0) |
                                         (focus_given ? ImGuiSliderFlags_NoInput : 0));
        focused = ImGui::IsItemFocused(); hovered = ImGui::IsItemHovered();
        if (focused && !ImGui::IsItemActive()) {
            const int dir = step_keys();
            const bool fast = pad.fast || ImGui::GetIO().KeyShift;
            if (dir) { v += (float)(dir * d->num_step * (fast ? 20.0 : 1.0)); edited = true; }
        }
        if (edited) {
            const double step = d->num_step > 0 ? d->num_step : 1;
            gt5_setting_set_number(d->key, floor((double)v / step + 0.5) * step);
            changed();
        }
        break;
    }
    default: {
        value_button(cur[0] ? cur : T("(not set)", "（未设置）"), widget_w, &kDim);
        focused = ImGui::IsItemFocused(); hovered = ImGui::IsItemHovered();
        break;
    }
    }
    const ImVec2 item_max = ImGui::GetItemRectMax();
    if (focused) { g_rows.any_focus = true; set_help_setting(d); note_focus(d->key); }
    else if (hovered && !ui.keyboard_last) set_help_setting(d);
    float bottom = label_bottom > item_max.y ? label_bottom : item_max.y;

    // In a low window there is no box for the help under the page: the row with the focus shows its
    // help itself, under its label and value.
    // (Whether it waits for a restart, or isn't there yet, the small line under the label says.)
    if (g_inline_help && focused && help_of(d)) {
        ImGui::SetCursorScreenPos(ImVec2(at_screen.x, bottom + S(6)));
        ImGui::PushFont(nullptr, help_px());
        ImGui::PushTextWrapPos(at.x + g_row.full);
        ImGui::TextColored(kDim, "%s", help_of(d));
        if (!strcmp(d->key, "LD_FRAMEGEN")) {
            const gt5_setting_def* m = gt5_settings_find("LD_FRAMEGEN_MULT");
            if (m && help_of(m) && has_row(m)) ImGui::TextColored(kDim, "%s", help_of(m));
        }
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        if (ImGui::GetItemRectMax().y > bottom) bottom = ImGui::GetItemRectMax().y;
    }

    // The whole row: lit behind its label and value while it has the focus or the mouse; then the
    // layout goes on below the taller of the two.
    const ImVec2 row_a(at_screen.x - S(8), at_screen.y - S(5)), row_b(at_screen.x + g_row.full + S(4), bottom + S(5));
    dl->ChannelsSetCurrent(0);
    if (focused || (hovered && !ui.keyboard_last)) dl->AddRectFilled(row_a, row_b, ImGui::GetColorU32(kRowHot), S(6));
    dl->ChannelsMerge();
    if (focused) show_whole_row(d->key, row_a, row_b);
    ImGui::SetCursorScreenPos(at_screen);
    ImGui::Dummy(ImVec2(g_row.full, bottom - at_screen.y + S(6)));
    ImGui::PopID();
}

// ---- the readout ---------------------------------------------------------------------------------------

void stats_lines(const gt5_menu_stats& st, bool compact)
{
    char line[256];
    size_t n = 0;
    line[0] = 0;
    #define ADD(...) do { n += (size_t)snprintf(line + n, sizeof line - n, "%s", n ? "    " : ""); n += (size_t)snprintf(line + n, sizeof line - n, __VA_ARGS__); if (n >= sizeof line) n = sizeof line - 1; } while (0)
    if (st.game_fps >= 0) ADD(T("Game %.0f fps", "游戏 %.0f 帧"), st.game_fps);
    if (st.shown_fps >= 0) ADD(T("Shown %.0f", "显示 %.0f"), st.shown_fps);
    if (st.drawn_fps >= 0 && !compact) ADD(T("Drawn %.0f", "绘制 %.0f"), st.drawn_fps);
    if (st.made_fps >= 0 && (st.made_fps > 0.5f || !compact)) ADD(T("Made %.0f", "生成 %.0f"), st.made_fps);
    if (n) ImGui::TextUnformatted(line);
    n = 0; line[0] = 0;
    if (st.cpu_cores >= 0) ADD(T("CPU %.1f cores busy", "CPU 占用 %.1f 核"), st.cpu_cores);
    if (st.gpu_ms >= 0) ADD(T("GPU %.1f ms a frame", "GPU 每帧 %.1f 毫秒"), st.gpu_ms);
    if (n) ImGui::TextUnformatted(line);
    if (compact) return;
    n = 0; line[0] = 0;
    if (st.frame_w > 0 && st.frame_h > 0) ADD(T("Frame %d x %d", "画面 %d x %d"), st.frame_w, st.frame_h);
    if (st.display_hz > 0) ADD(T("Screen %d Hz", "屏幕 %d Hz"), st.display_hz);
    if (st.draws >= 0) ADD(T("%d draws", "%d 次绘制"), st.draws);
    if (n) ImGui::TextColored(kDim, "%s", line);
    #undef ADD
}

void readout_block()
{
    gt5_menu_stats st;
    const bool have = stats_get(&st);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(12)));
    ImGui::BeginChild("##readout", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
    if (!have) {
        ImGui::TextColored(kDim, "%s", T("No numbers from the game yet.", "游戏尚未提供数据。"));
    } else {
        // The graph beside the numbers when there is room for both, else the numbers alone.
        const float avail = ImGui::GetContentRegionAvail().x;
        const float plot_w = floorf(clampf(avail * 0.34f, S(160), S(300)));
        const bool plot = ui.fps_n > 1 && avail - plot_w - S(16) >= base_px() * 15.0f;
        const float text_w = plot ? avail - plot_w - S(16) : avail;
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + text_w);
        stats_lines(st, false);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        if (plot) {
            const float h = ImGui::GetItemRectSize().y;
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - plot_w);
            float top = 60.0f;
            for (int i = 0; i < ui.fps_n; i++) if (ui.fps_hist[i] > top) top = ui.fps_hist[i];
            ImGui::PushStyleColor(ImGuiCol_PlotLines, kLive);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, kNav);
            ImGui::PushFont(nullptr, small_px());
            ImGui::PlotLines("##fps", ui.fps_hist, ui.fps_n, 0, T("game frames a second", "游戏帧率"), 0.0f, top * 1.1f,
                             ImVec2(plot_w, h > base_px() * 2.5f ? h : base_px() * 2.5f));
            ImGui::PopFont();
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, S(6)));
}

void sample_fps()
{
    gt5_menu_stats st;
    if (!stats_get(&st) || st.game_fps < 0) return;
    const double t = now_s();
    if (t - ui.fps_t < 0.5) return;
    ui.fps_t = t;
    const int cap = (int)(sizeof ui.fps_hist / sizeof ui.fps_hist[0]);
    if (ui.fps_n == cap) { memmove(ui.fps_hist, ui.fps_hist + 1, (size_t)(cap - 1) * sizeof(float)); ui.fps_n--; }
    ui.fps_hist[ui.fps_n++] = st.game_fps;
}

// ---- pages ---------------------------------------------------------------------------------------------

void heading(const char* text)
{
    ImGui::PushFont(nullptr, base_px() * 1.18f);
    ImGui::TextColored(kText, "%s", text);
    ImGui::PopFont();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y - S(2)), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y - S(2)), ImGui::GetColorU32(kLine), S(1));
    ImGui::Dummy(ImVec2(0, S(8)));
}

void pending_note()
{
    char list[600];
    size_t n = 0;
    list[0] = 0;
    for (int i = 0; i < gt5_settings_count(); i++) {
        const gt5_setting_def* d = gt5_settings_at(i);
        if (d->page == GT5_PAGE_NONE || (gt5_setting_flags(d->key) & GT5_SF_HIDDEN) || !gt5_setting_pending_restart(d->key)) continue;
        n += (size_t)snprintf(list + n, sizeof list - n, "%s%s", n ? T(", ", "、") : "", label_of(d));
        if (n >= sizeof list) { n = sizeof list - 1; break; }
    }
    if (!n) return;
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kNext, "%s %s", T("Restart the game to apply:", "重启游戏后生效："), list);
    ImGui::PopTextWrapPos();
}

void settings_page(int page, const PadEdges& pad)
{
    if (g_show_heading) heading(page_name(page));     // in a low window the page list says which page it is
    if (page == GT5_PAGE_PERFORMANCE) readout_block();
    plan_rows();
    for (int i = 0; i < gt5_settings_count(); i++) {
        const gt5_setting_def* d = gt5_settings_at(i);
        if (d->page != page || !has_row(d, true)) continue;
        setting_row(d, pad);
    }
    pending_note();
}

void link_button(const char* text, const char* url, float w)
{
    if (g_rows.first && ui.focus_content) { ImGui::SetKeyboardFocusHere(); ui.focus_content = false; }
    g_rows.first = false;
    ImGui::PushID(text);
    const bool pressed = value_button(text, w, nullptr, true);
    ImGui::PopID();
    if (pressed) { if (url) open_url(url); else open_logs_folder(); }
    if (ImGui::IsItemFocused()) {
        g_rows.any_focus = true;
        note_focus(text);
        show_whole_row(text, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        set_help_text(help_link(url != nullptr));
    }
}

void about_page()
{
    if (g_show_heading) heading(page_name(GT5_PAGE_ABOUT));
    ImGui::PushFont(nullptr, base_px() * 1.3f);
    ImGui::TextUnformatted(title_text());
    ImGui::PopFont();
    ImGui::TextColored(kDim, "%s", T("PC recompilation", "PC 重编译版"));                               // (launcher) subtitle
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("%s:  %s", T("Version", "版本"), version_text()[0] ? version_text() : T("development build", "开发版本"));   // (launcher) version, devbuild
    gt5_menu_stats st;
    if (stats_get(&st) && st.gpu_name[0]) ImGui::Text("%s:  %s", T("Graphics card", "显卡"), st.gpu_name);
    ImGui::Dummy(ImVec2(0, S(10)));
    ImGui::TextUnformatted(T("An unofficial PC port of Gran Turismo 5 made by static recompilation. It needs your own copy of the game; nothing from the game is distributed.",
                             "通过静态重编译制作的非官方《GT赛车5》PC 移植版。需要你自己拥有的游戏副本；不分发任何游戏内容。"));   // (launcher) help.about
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, S(10)));
    float w = S(380);
    if (w > ImGui::GetContentRegionAvail().x) w = ImGui::GetContentRegionAvail().x;
    link_button(T("Report a problem", "报告问题"), "https://github.com/MormonFTP-hub/gt5-recomp/issues/new/choose", w);   // (launcher) help.*
    link_button(T("Read the FAQ", "常见问题"), "https://github.com/MormonFTP-hub/gt5-recomp#faq", w);
    link_button(T("Version history", "版本历史"), "https://github.com/MormonFTP-hub/gt5-recomp/blob/main/VERSIONS.md", w);
    link_button(T("Changelog", "更新日志"), "https://github.com/MormonFTP-hub/gt5-recomp/blob/main/CHANGELOG.md", w);
    link_button(T("Open the logs folder", "打开日志文件夹"), nullptr, w);
    {   // [win] the way to quit with a controller alone
        ImGui::PushID("##quitgame");
        const char* qt = T("Quit the game", "退出游戏");
        const bool pressed = value_button(qt, w, nullptr, true);
        ImGui::PopID();
        if (pressed) quit_ask();
        if (ImGui::IsItemFocused()) {
            g_rows.any_focus = true;
            note_focus(qt);
            show_whole_row(qt, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            set_help_text("");
        }
    }
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "%s", T("Discord: mormonftp", "Discord：mormonftp"));
    ImGui::TextColored(kDim, "%s Dear ImGui %s (MIT).", T("The menu is drawn with", "菜单绘制使用"), ImGui::GetVersion());
    ImGui::PopTextWrapPos();
}

// ---- the key and wheel maps ----------------------------------------------------------------------------

void save_keys()
{
    char b[1200];
    gt5_kb_format(&ui.keys, b, sizeof b);
    gt5_setting_set("PS3_KEYS", b);
    changed();
}
void save_wheel()
{
    char b[600], p[200];
    gt5_wheel_format_buttons(&ui.wheel, b, sizeof b);
    gt5_wheel_format_pedals(&ui.wheel, p, sizeof p);
    gt5_setting_set("PS3_WHEEL_BUTTONS", b);
    gt5_setting_set("PS3_WHEEL_PEDALS", p);
    changed();
}

// Where a map's columns go: the action, its pad button (keys only) and the value on one line when they
// fit, else the value on a second line under the name.
struct MapCols { bool two_lines = false; float b_x = 0, c_x = 0; };
MapCols plan_map(float max_a, float max_b, float max_c)
{
    MapCols m;
    const float full = ImGui::GetContentRegionAvail().x, pad = S(10), gap = S(24);
    const float want_c = max_c < base_px() * 8.0f ? max_c : base_px() * 8.0f;
    m.b_x = floorf(full * 0.40f);
    if (m.b_x < pad + max_a + gap) m.b_x = pad + max_a + gap;
    if (max_b <= 0.0f) m.c_x = m.b_x;
    else {
        m.c_x = floorf(full * 0.66f);
        if (m.c_x < m.b_x + max_b + gap) m.c_x = m.b_x + max_b + gap;
    }
    if (m.c_x + want_c > full - pad) {
        m.two_lines = true;
        m.b_x = pad;
        m.c_x = max_b > 0.0f ? floorf(full * 0.45f) : pad + S(20);
    }
    return m;
}

// A row of a map: its texts over one thing the focus can rest on. Returns true when it was pressed;
// *focused and *right_click say the rest.
bool map_row(int id, int row_index, const char* a, const char* b, const char* c, const ImVec4& c_col, bool waiting, const MapCols& cols,
             bool* focused, bool* right_click)
{
    ImGui::PushID(id);
    if (ui.focus_row == row_index || (ui.focus_content && g_rows.first && ui.focus_row < 0)) {
        ImGui::SetKeyboardFocusHere();
        ui.focus_content = false; ui.focus_row = -1;
    }
    g_rows.first = false;
    const float full = ImGui::GetContentRegionAvail().x;
    const ImVec2 at = ImGui::GetCursorPos();
    const float line = ImGui::GetTextLineHeight(), pad_x = S(10);
    const float fp = ImGui::GetStyle().FramePadding.y;
    const float h = cols.two_lines ? fp * 2.0f + line * 2.0f + S(4) : ImGui::GetFrameHeight();
    if (waiting) ImGui::PushStyleColor(ImGuiCol_Header, kAccent);
    const bool pressed = ImGui::Selectable("##row", waiting, ImGuiSelectableFlags_AllowOverlap, ImVec2(full, h));
    if (waiting) ImGui::PopStyleColor();
    *focused = ImGui::IsItemFocused();
    *right_click = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (*focused) { char id[16]; snprintf(id, sizeof id, "row%d", row_index); note_focus(id); show_whole_row(id, ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); }
    const ImVec2 end = ImGui::GetCursorPos();
    char fa[200], fb[200], fc[200];
    if (!cols.two_lines) {
        const float y = at.y + (h - line) * 0.5f;
        ImGui::SetCursorPos(ImVec2(at.x + pad_x, y));
        ImGui::TextUnformatted(fit_text(a, (b && *b ? cols.b_x : cols.c_x) - pad_x - S(12), fa, sizeof fa));
        if (b && *b) {
            ImGui::SetCursorPos(ImVec2(at.x + cols.b_x, y));
            ImGui::TextColored(kDim, "%s", fit_text(b, cols.c_x - cols.b_x - S(12), fb, sizeof fb));
        }
        ImGui::SetCursorPos(ImVec2(at.x + cols.c_x, y));
        ImGui::TextColored(c_col, "%s", fit_text(c, full - cols.c_x - pad_x, fc, sizeof fc));
    } else {
        // the name on the first line; under it the pad button (dim) and the value
        const float y1 = at.y + fp, y2 = at.y + fp + line + S(4);
        ImGui::SetCursorPos(ImVec2(at.x + pad_x, y1));
        ImGui::TextUnformatted(fit_text(a, full - pad_x * 2.0f, fa, sizeof fa));
        if (b && *b) {
            ImGui::SetCursorPos(ImVec2(at.x + pad_x, y2));
            ImGui::TextColored(kDim, "%s", fit_text(b, cols.c_x - pad_x - S(12), fb, sizeof fb));
        }
        ImGui::SetCursorPos(ImVec2(at.x + cols.c_x, y2));
        ImGui::TextColored(c_col, "%s", fit_text(c, full - cols.c_x - pad_x, fc, sizeof fc));
    }
    ImGui::SetCursorPos(ImVec2(at.x, end.y - ImGui::GetStyle().ItemSpacing.y * 0.6f));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
    if (*focused) g_rows.any_focus = true;
    return pressed;
}

void note_text(const char* text)
{
    ImGui::PushFont(nullptr, help_px());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "%s", text);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
}

void keys_screen(const PadEdges& pad)
{
    heading(T("Keyboard keys", "键盘按键"));                                                       // (launcher) kb.title
    const char* help = help_keys();
    if (g_inline_help) note_text(help);
    float rw = S(300);
    if (rw > ImGui::GetContentRegionAvail().x) rw = ImGui::GetContentRegionAvail().x;
    if (value_button(T("Reset to defaults", "恢复默认"), rw, nullptr, true)) { bind_cancel(); gt5_kb_defaults(&ui.keys); save_keys(); }   // (launcher) kb.reset
    if (ImGui::IsItemFocused()) { g_rows.any_focus = true; set_help_text(help); note_focus("reset"); }
    ImGui::Dummy(ImVec2(0, S(4)));

    char keys[GT5_KB_BUTTONS][160];
    float max_a = 0, max_b = 0, max_c = ImGui::CalcTextSize(T("Press a key...", "请按一个键...")).x;
    for (int b = 0; b < GT5_KB_BUTTONS; b++) {
        size_t n = 0;
        keys[b][0] = 0;
        for (int k = 0; k < gt5_kb_count(&ui.keys, b); k++)
            n += (size_t)snprintf(keys[b] + n, sizeof keys[b] - n, "%s%s", k ? "  /  " : "", gt5_kb_pretty(ui.keys.key[b][k]));
        if (!n) snprintf(keys[b], sizeof keys[b], "%s", T("(no key)", "（无按键）"));   // (launcher) kb.none
        const float wa = ImGui::CalcTextSize(gt5_kb_action_label(b, ui.zh)).x, wb = ImGui::CalcTextSize(gt5_kb_button_label(b, ui.zh)).x;
        const float wc = ImGui::CalcTextSize(keys[b]).x;
        if (wa > max_a) max_a = wa;
        if (wb > max_b) max_b = wb;
        if (wc > max_c) max_c = wc;
    }
    const MapCols cols = plan_map(max_a, max_b, max_c);
    for (int b = 0; b < GT5_KB_BUTTONS; b++) {
        if (b == GT5_KB_FIRST_STICK) {
            ImGui::Dummy(ImVec2(0, S(6)));
            ImGui::TextColored(kDim, "%s", T("Sticks (full tilt)", "摇杆（推到底）"));                    // (launcher) kb.stick
        }
        const bool waiting = ui.bind == BIND_KEY_ROW && ui.bind_row == b;
        const bool has_key = gt5_kb_count(&ui.keys, b) > 0;
        bool focused = false, rclick = false;
        const bool pressed = map_row(b, b, gt5_kb_action_label(b, ui.zh), gt5_kb_button_label(b, ui.zh),
                                     waiting ? T("Press a key...", "请按一个键...") : keys[b],   // (launcher) kb.press
                                     waiting || has_key ? kText : kDim, waiting, cols, &focused, &rclick);
        if (ui.bind == BIND_NONE) {
            if (pressed) bind_begin(BIND_KEY_ROW, b, false);
            else if (rclick || (focused && (pad.square || ImGui::IsKeyPressed(ImGuiKey_Insert, false)))) bind_begin(BIND_KEY_ROW, b, true);
            else if (focused && (pad.triangle || ImGui::IsKeyPressed(ImGuiKey_Delete, false))) { gt5_kb_clear(&ui.keys, b); save_keys(); }
        }
        if (focused) set_help_text(help);
    }
    ImGui::Dummy(ImVec2(0, S(6)));
    note_text(T("The rows are GT5's own pad buttons: change a button's job in the game's options and its key follows. Left View, Right View and Flash Lights have no button at first: give them one in the game, then a key here.",
                "这些行是 GT5 自己的手柄按钮：在游戏选项中更改按钮的功能后，对应的按键也随之改变。“左视角”“右视角”和“闪灯”默认没有按钮：先在游戏中分配一个，再在这里设置按键。"));   // (launcher) kb.views, shortened
}

void wheel_screen(const PadEdges& pad)
{
    heading(T("Wheel buttons and pedals", "方向盘按钮和踏板"));
    gt5_menu_wheel w;
    const bool have = wheel_get(&w);
    const char* help = help_wheel();
    if (g_inline_help) note_text(help);
    ImGui::PushTextWrapPos(0.0f);
    if (have) ImGui::TextColored(kLive, "%s  %s", T("Reading:", "正在读取："), w.name[0] ? w.name : T("a steering wheel", "方向盘"));
    else ImGui::TextColored(kNext, "%s", T("No steering wheel found, so a button can't be picked by pressing it. Plug the wheel in before starting the game.",
                                           "未找到方向盘，因此无法通过按下按钮来选择。请在启动游戏前连接方向盘。"));
    ImGui::PopTextWrapPos();
    note_text(T("Changes here apply after a restart.", "此处的更改在重启游戏后生效。"));
    ImGui::Dummy(ImVec2(0, S(4)));

    // a wait ends by itself
    if ((ui.bind == BIND_WHEEL_BUTTON || ui.bind == BIND_WHEEL_PEDAL) && now_s() - ui.bind_t0 > 10.0) bind_cancel();
    if (ui.bind == BIND_WHEEL_BUTTON && have) {
        for (int i = 0; i < (int)sizeof w.button; i++)
            if (w.button[i] && !ui.bind_wheel0.button[i]) { ui.wheel.btn[ui.bind_row] = i + 1; bind_cancel(); save_wheel(); break; }
    }
    if (ui.bind == BIND_WHEEL_PEDAL && have) {
        for (int a = 0; a < GT5_WHEEL_AXES; a++)
            if (fabsf(w.axis[a] - ui.bind_wheel0.axis[a]) > 0.5f) { ui.wheel.pedal_axis[ui.bind_row] = a; bind_cancel(); save_wheel(); break; }
    }

    // the values, for the columns
    char pedal_val[3][64], btn_val[GT5_WHEEL_BUTTONS][64];
    float max_a = 0, max_c = ImGui::CalcTextSize(T("Press a button on the wheel...", "请按下方向盘上的按钮...")).x;
    for (int p = 0; p < 3; p++) {
        const int a = ui.wheel.pedal_axis[p];
        if (a == -2) snprintf(pedal_val[p], sizeof pedal_val[p], "%s", T("As the wheel has it", "按方向盘默认"));
        else if (a == -1) snprintf(pedal_val[p], sizeof pedal_val[p], "%s", T("None", "无"));
        else snprintf(pedal_val[p], sizeof pedal_val[p], "%s %s%s", T("Axis", "轴"), gt5_wheel_axis(a), ui.wheel.pedal_inv[p] ? T(", reversed", "，反向") : "");
        const float wa = ImGui::CalcTextSize(gt5_wheel_pedal_label(p, ui.zh)).x, wc = ImGui::CalcTextSize(pedal_val[p]).x;
        if (wa > max_a) max_a = wa;
        if (wc > max_c) max_c = wc;
    }
    for (int i = 0; i < GT5_WHEEL_BUTTONS; i++) {
        const int n = ui.wheel.btn[i];
        if (n < 0) snprintf(btn_val[i], sizeof btn_val[i], "%s", T("As the wheel has it", "按方向盘默认"));
        else if (n == 0) snprintf(btn_val[i], sizeof btn_val[i], "%s", T("None", "无"));
        else snprintf(btn_val[i], sizeof btn_val[i], T("Button %d", "按钮 %d"), n);
        const float wa = ImGui::CalcTextSize(gt5_wheel_button_label(i, ui.zh)).x, wc = ImGui::CalcTextSize(btn_val[i]).x;
        if (wa > max_a) max_a = wa;
        if (wc > max_c) max_c = wc;
    }
    const MapCols cols = plan_map(max_a, 0.0f, max_c);

    ImGui::TextColored(kDim, "%s", T("Pedals", "踏板"));
    for (int p = 0; p < 3; p++) {
        const int a = ui.wheel.pedal_axis[p];
        const bool waiting = ui.bind == BIND_WHEEL_PEDAL && ui.bind_row == p;
        bool focused = false, rclick = false;
        const bool pressed = map_row(1000 + p, GT5_WHEEL_BUTTONS + p, gt5_wheel_pedal_label(p, ui.zh), "",
                                     waiting ? T("Push the pedal down...", "请踩下踏板...") : pedal_val[p], waiting || a != -2 ? kText : kDim, waiting, cols,
                                     &focused, &rclick);
        if (ui.bind == BIND_NONE) {
            if (pressed && have) bind_begin(BIND_WHEEL_PEDAL, p, false);
            else if (rclick || (focused && (pad.square || ImGui::IsKeyPressed(ImGuiKey_Insert, false)))) { if (a >= 0) { ui.wheel.pedal_inv[p] = !ui.wheel.pedal_inv[p]; save_wheel(); } }
            else if (focused && (pad.triangle || ImGui::IsKeyPressed(ImGuiKey_Delete, false))) { ui.wheel.pedal_axis[p] = -2; ui.wheel.pedal_inv[p] = 0; save_wheel(); }
        }
        if (focused) set_help_text(help);
    }
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::TextColored(kDim, "%s", T("Buttons", "按钮"));
    for (int i = 0; i < GT5_WHEEL_BUTTONS; i++) {
        const int n = ui.wheel.btn[i];
        const bool waiting = ui.bind == BIND_WHEEL_BUTTON && ui.bind_row == i;
        bool focused = false, rclick = false;
        const bool pressed = map_row(i, i, gt5_wheel_button_label(i, ui.zh), "",
                                     waiting ? T("Press a button on the wheel...", "请按下方向盘上的按钮...") : btn_val[i], waiting || n >= 0 ? kText : kDim, waiting, cols,
                                     &focused, &rclick);
        if (ui.bind == BIND_NONE) {
            if (pressed && have) bind_begin(BIND_WHEEL_BUTTON, i, false);
            else if (rclick || (focused && (pad.square || ImGui::IsKeyPressed(ImGuiKey_Insert, false)))) { ui.wheel.btn[i] = 0; save_wheel(); }
            else if (focused && (pad.triangle || ImGui::IsKeyPressed(ImGuiKey_Delete, false))) { ui.wheel.btn[i] = -1; save_wheel(); }
        }
        if (focused) set_help_text(help);
    }
}

// ---- the bottom: help for what the focus is on, and the keys -----------------------------------------------

void footer(float height)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kNav);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(12)));
    ImGui::BeginChild("##help", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
    const gt5_setting_def* d = ui.help_key[0] ? gt5_settings_find(ui.help_key) : nullptr;
    ImGui::PushTextWrapPos(0.0f);
    if (d) {
        ImGui::TextColored(kText, "%s", label_of(d));
        const char* h = help_of(d);
        const unsigned flags = gt5_setting_flags(d->key);
        ImGui::PushFont(nullptr, help_px());
        if (h) ImGui::TextColored(kDim, "%s", h);
        if (!strcmp(d->key, "LD_FRAMEGEN")) {
            const gt5_setting_def* m = gt5_settings_find("LD_FRAMEGEN_MULT");
            if (m && help_of(m) && has_row(m)) ImGui::TextColored(kDim, "%s", help_of(m));
        }
        if ((flags & GT5_SF_NEEDS_HOST) && !(flags & GT5_SF_ENABLED))
            ImGui::TextColored(kNext, "%s", note_unavailable());
        else if (gt5_setting_apply_of(d->key) == GT5_APPLY_RESTART)
            ImGui::TextColored(kNext, "%s", note_restart());
        ImGui::PopFont();
    } else if (ui.help_text[0]) {
        ImGui::PushFont(nullptr, help_px());
        ImGui::TextColored(kDim, "%s", ui.help_text);
        ImGui::PopFont();
    }
    ImGui::PopTextWrapPos();
    g_help_needs = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + ImGui::GetStyle().WindowPadding.y;   // for the next frame
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// The keys, in small text at the bottom: as many on a line as fit, on as many lines as it takes.
struct Hints { const char* part[6]; int n; float sep; };
Hints hints_now(char* close, size_t cap)
{
    char key[32], name[48];
    gt5_setting_stored("GT5_MENU_KEY", key, sizeof key);
    key_names(key, name, sizeof name);
    snprintf(close, cap, T("%s  close", "%s  关闭"), name);
    Hints h;
    h.part[0] = T("Up / Down  move", "上 / 下  移动");
    h.part[1] = T("Left / Right  change", "左 / 右  更改");
    h.part[2] = T("Enter or Cross  choose", "Enter 或 ✕  选择");
    h.part[3] = T("Esc or Circle  back", "Esc 或 ○  返回");
    h.part[4] = T("L1 / R1 or Page Up / Down  page", "L1 / R1 或 Page Up / Down  翻页");
    h.part[5] = close;
    h.n = 6;
    h.sep = S(28);
    return h;
}
// A low window keeps the keys to one line: what doesn't fit goes, the page keys first, then Left /
// Right, then the closing key (Up / Down, Enter and Esc always stay).
void hints_one_line(Hints* h, float width)
{
    static const int drop_order[3] = { 4, 1, 5 };
    for (int k = 0; k < 3; k++) {
        float total = 0.0f;
        for (int i = 0; i < h->n; i++) total += (i ? h->sep : 0.0f) + ImGui::CalcTextSize(h->part[i]).x;
        if (total <= width) return;
        const char* drop = nullptr;
        switch (drop_order[k]) {                 // the parts as hints_now() made them
        case 4: drop = T("L1 / R1 or Page Up / Down  page", "L1 / R1 或 Page Up / Down  翻页"); break;
        case 1: drop = T("Left / Right  change", "左 / 右  更改"); break;
        default: drop = h->part[h->n - 1]; break;
        }
        int j = 0;
        for (int i = 0; i < h->n; i++) if (h->part[i] != drop) h->part[j++] = h->part[i];
        h->n = j;
    }
}
int hint_lines(const Hints& h, float width)
{
    int lines = 1;
    float x = 0.0f;
    for (int i = 0; i < h.n; i++) {
        const float w = ImGui::CalcTextSize(h.part[i]).x;
        if (x > 0.0f && x + h.sep + w > width) { lines++; x = 0.0f; }
        x += (x > 0.0f ? h.sep : 0.0f) + w;
    }
    return lines;
}
void hint_draw(const Hints& h, float width)
{
    float x = 0.0f;
    for (int i = 0; i < h.n; i++) {
        const float w = ImGui::CalcTextSize(h.part[i]).x;
        if (x > 0.0f && x + h.sep + w <= width) { ImGui::SameLine(0.0f, h.sep); x += h.sep + w; }
        else x = w;
        ImGui::TextColored(kDim, "%s", h.part[i]);
    }
}

}  // namespace

// ---- style ---------------------------------------------------------------------------------------------

void ui_apply_style(float scale)
{
    ImGuiStyle st;
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 12; st.ChildRounding = 8; st.FrameRounding = 6; st.PopupRounding = 8; st.GrabRounding = 6; st.ScrollbarRounding = 8;
    st.WindowPadding = ImVec2(24, 20); st.FramePadding = ImVec2(12, 7); st.ItemSpacing = ImVec2(12, 8); st.ItemInnerSpacing = ImVec2(8, 6);
    st.ScrollbarSize = 14; st.GrabMinSize = 18;
    st.WindowBorderSize = 1; st.ChildBorderSize = 0; st.FrameBorderSize = 0; st.PopupBorderSize = 1;
    st.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    st.Colors[ImGuiCol_Text] = kText;
    st.Colors[ImGuiCol_TextDisabled] = ImVec4(kDim.x, kDim.y, kDim.z, 0.55f);
    st.Colors[ImGuiCol_WindowBg] = kBg;
    st.Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_PopupBg] = ImVec4(0.105f, 0.115f, 0.150f, 1.0f);   // an open list is solid: the rows behind it must not show through
    st.Colors[ImGuiCol_Border] = kLine;
    st.Colors[ImGuiCol_FrameBg] = kPanel;
    st.Colors[ImGuiCol_FrameBgHovered] = kNavHot;
    st.Colors[ImGuiCol_FrameBgActive] = kLine;
    st.Colors[ImGuiCol_Button] = kPanel;
    st.Colors[ImGuiCol_ButtonHovered] = kNavHot;
    st.Colors[ImGuiCol_ButtonActive] = kAccent;
    st.Colors[ImGuiCol_Header] = kNavHot;
    st.Colors[ImGuiCol_HeaderHovered] = kNavHot;
    st.Colors[ImGuiCol_HeaderActive] = kAccent;
    st.Colors[ImGuiCol_CheckMark] = kAccentHot;
    st.Colors[ImGuiCol_SliderGrab] = kAccent;
    st.Colors[ImGuiCol_SliderGrabActive] = kAccentHot;
    st.Colors[ImGuiCol_Separator] = kLine;
    st.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_ScrollbarGrab] = kNavHot;
    st.Colors[ImGuiCol_ScrollbarGrabHovered] = kLine;
    st.Colors[ImGuiCol_ScrollbarGrabActive] = kDim;
    st.Colors[ImGuiCol_NavCursor] = kBlue;
    st.Colors[ImGuiCol_PlotLines] = kLive;
    st.ScaleAllSizes(scale);
    st.FontSizeBase = 22.0f * scale;
    ImGui::GetStyle() = st;
}

void ui_opened()
{
    ui.sub = SUB_NONE;
    ui.bind = BIND_NONE;
    ui.in_content = false;
    ui.focus_nav = true;
    ui.focus_content = false;
    ui.focus_key[0] = 0;
    ui.focus_row = -1;
    ui.popup_was_open = false;
    ui.help_key[0] = 0;
    ui.help_text[0] = 0;
    ui.scrolled_to[0] = 0;
    ui.shown_page = -1;
    // Vulkan can only be picked once the launcher has built it into gt5.exe (then SDL2.dll sits next to it).
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n && n < MAX_PATH - 12) {
        wchar_t* cut = wcsrchr(exe, L'\\');
        if (cut) { wcscpy_s(cut + 1, (size_t)(MAX_PATH - (cut + 1 - exe)), L"SDL2.dll");
                   gt5_setting_choice_enable("GT5_RENDERER", "vulkan", GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES); }
    }
}

void ui_key_taken(const char* name)
{
    if (ui.bind == BIND_KEY_ROW) {
        ui.focus_row = ui.bind_row;
        if (name) { gt5_kb_bind(&ui.keys, ui.bind_row, name, ui.bind_add); save_keys(); }
    } else if (ui.bind == BIND_MENU_KEY) {
        if (name && strncmp(name, "Mouse", 5)) { gt5_setting_set("GT5_MENU_KEY", !strcmp(name, "F1") ? "" : name); note_changed(); }
        snprintf(ui.focus_key, sizeof ui.focus_key, "GT5_MENU_KEY");
    }
    ui.bind = BIND_NONE;
}

// ---- the corner readout ---------------------------------------------------------------------------------

void ui_build_osd()
{
    sample_fps();
    gt5_menu_stats st;
    if (!stats_get(&st)) return;
    ImGui::SetNextWindowPos(ImVec2(S(16), S(16)));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12), S(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##osd", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(nullptr, small_px());
    stats_lines(st, true);
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ---- [win] "Quit the game?" ------------------------------------------------------------------------------

// Asked over the game (Esc, the window's X, Alt+F4) or over the menu (its About page). Two buttons, No
// with the focus; Enter or Cross takes the one with the focus, Esc or Circle is No, the mouse clicks.
void ui_build_quit()
{
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImGui::SetNavCursorVisible(ui.keyboard_last);
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 6, 10, 150));
    const float b = base_px();
    const char* yes = T("Yes", "是");
    const char* no = T("No", "否");
    const char* q = T("Quit the game?", "退出游戏？");
    float bw = ImGui::CalcTextSize(yes).x > ImGui::CalcTextSize(no).x ? ImGui::CalcTextSize(yes).x : ImGui::CalcTextSize(no).x;
    bw = floorf(bw + S(72));
    ImGui::SetNextWindowPos(ImVec2(floorf(W * 0.5f), floorf(H * 0.5f)), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(36), S(28)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kNavHot);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kBlue);       // not the red: nothing is wrong
    ImGui::Begin("##quit", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(nullptr, b * 1.3f);
    const float qw = ImGui::CalcTextSize(q).x;
    const float row = bw * 2.0f + S(16);
    if (row > qw) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + floorf((row - qw) * 0.5f));
    ImGui::TextUnformatted(q);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(14)));
    if (qw > row) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + floorf((qw - row) * 0.5f));
    bool said_yes = ImGui::Button(yes, ImVec2(bw, b * 2.0f));
    ImGui::SameLine(0.0f, S(16));
    if (ui.quit_focus) { ImGui::SetKeyboardFocusHere(); ui.quit_focus = false; }
    bool said_no = ImGui::Button(no, ImVec2(bw, b * 2.0f));
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) said_no = true;
    if (said_yes) quit_answer(true);
    else if (said_no) quit_answer(false);
}

// ---- [win] the first-start hint ---------------------------------------------------------------------------

void ui_build_hint(float alpha)
{
    ImGuiIO& io = ImGui::GetIO();
    char key[32], name[48], text[160];
    gt5_setting_stored("GT5_MENU_KEY", key, sizeof key);
    key_names(key, name, sizeof name);
    char pad_mode[16];
    gt5_setting_stored("GT5_MENU_PAD", pad_mode, sizeof pad_mode);
    const char* chord = !_stricmp(pad_mode, "off") ? nullptr : !_stricmp(pad_mode, "l3r3") ? "L3+R3" : "Select+Start";
    if (chord && pad_connected()) snprintf(text, sizeof text, T("%s / %s: settings", "%s / %s：设置"), name, chord);
    else snprintf(text, sizeof text, T("%s: settings", "%s：设置"), name);
    ImGui::PushFont(nullptr, base_px());
    const ImVec2 ts = ImGui::CalcTextSize(text);
    ImGui::PopFont();
    const ImVec2 pad(S(16), S(10));
    const float m = S(24);
    ImGui::SetNextWindowPos(ImVec2(floorf(io.DisplaySize.x - m - ts.x - pad.x * 2.0f), floorf(io.DisplaySize.y - m - ts.y - pad.y * 2.0f)));
    ImGui::SetNextWindowBgAlpha(0.72f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::Begin("##hint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(nullptr, base_px());
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ---- notices: a few plain words, top right, while the menu is closed ----------------------------------

void ui_build_notices()
{
    char lines[GT5_NOTICE_COUNT][192];
    const int n = notices_get(lines, GT5_NOTICE_COUNT);
    if (!n) return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - S(16), S(16)), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##notices", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    for (int i = 0; i < n; i++) ImGui::TextUnformatted(lines[i]);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ---- the menu -------------------------------------------------------------------------------------------

void ui_build(const PadEdges& pad)
{
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    sample_fps();
    ImGui::SetNavCursorVisible(ui.keyboard_last);
    memset(&ui.tv, 0, sizeof ui.tv);
    ui.tv.small_px = small_px();

    // Back: out of a wait, out of a map, out of a page, out of the menu. Not when the press closed a
    // list or let go of a slider (Dear ImGui has dealt with it then).
    const bool waiting = ui.bind != BIND_NONE;
    bool back = !ui.popup_was_open && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false));
    if (waiting) {
        if (back) { if (ui.sub != SUB_NONE) ui.focus_row = ui.bind == BIND_WHEEL_PEDAL ? GT5_WHEEL_BUTTONS + ui.bind_row : ui.bind_row; bind_cancel(); }
        back = false;
    }
    if (back) {
        if (ui.sub != SUB_NONE) {
            snprintf(ui.focus_key, sizeof ui.focus_key, "%s", ui.sub == SUB_KEYS ? "PS3_KEYS" : "PS3_WHEEL_BUTTONS");
            ui.sub = SUB_NONE;
        } else if (ui.in_content) {
            ui.in_content = false; ui.focus_nav = true;
            ui.focus_content = false; ui.focus_key[0] = 0;   // a focus still on its way into the page is called off
        } else {
            request_close();
        }
    }
    if (!waiting && !ui.popup_was_open && ui.sub == SUB_NONE) {
        int turn = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) turn = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) turn = 1;
        if (turn) {
            ui.page = (ui.page + turn + GT5_PAGE_COUNT) % GT5_PAGE_COUNT;
            if (ui.in_content) ui.focus_content = true; else ui.focus_nav = true;
        }
    }

    // the game behind, dimmed
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 6, 10, 150));

    // The panel: the window less a margin, at most about 1500 x 1010 at 100 % (68 x 46 lines of text),
    // in the middle.
    const float b = base_px();
    float pw = W - 2.0f * clampf(W * 0.03f, S(8), S(48)), ph = H - 2.0f * clampf(H * 0.04f, S(8), S(56));
    if (pw > b * 68.0f) pw = b * 68.0f;
    if (ph > b * 46.0f) ph = b * 46.0f;
    pw = floorf(pw); ph = floorf(ph);
    const bool tight = pw < b * 40.0f;           // a narrow window: less padding, a narrower page list
    ImGui::SetNextWindowPos(ImVec2(floorf(W * 0.5f), floorf(H * 0.5f)), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(pw, ph));
    if (tight) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(14)));
    ImGui::Begin("##gt5menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (tight) ImGui::PopStyleVar();
    rect_out(ui.tv.panel, ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + pw, ImGui::GetWindowPos().y + ph));
    {
        ImFontBaked* baked = ImGui::GetFontBaked();
        const ImFontGlyph* g = baked->FindGlyph((ImWchar)'H');
        ui.tv.cap_px = g ? g->Y1 - g->Y0 : 0.0f;
        ui.tv.density = baked->RasterizerDensity;
    }
    const float inner_w = ImGui::GetContentRegionAvail().x;

    // the top line: "Settings", the game's name when there is room, "Saved" on the right
    {
        const double t = now_s();
        const char* note = ui.save_failed ? T("Settings couldn't be saved", "无法保存设置")
                         : (ui.saved_at != 0 && t - ui.saved_at < 2.5) ? T("Saved", "已保存") : nullptr;
        ImGui::PushFont(nullptr, b * 1.3f);
        const float title_w = ImGui::CalcTextSize(T("Settings", "设置")).x;
        ImGui::TextUnformatted(T("Settings", "设置"));
        ImGui::PopFont();
        const float note_w = note ? ImGui::CalcTextSize(note).x : 0.0f;
        const float name_w = ImGui::CalcTextSize(title_text()).x + S(20);
        if (title_w + name_w + note_w + S(40) < inner_w) {
            ImGui::SameLine(0.0f, S(20));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + b * 0.36f);
            ImGui::TextColored(kDim, "%s", title_text());
        }
        if (note) {
            ImGui::SameLine(ImGui::GetWindowWidth() - note_w - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextColored(ui.save_failed ? kAccentHot : kLive, "%s", note);
        }
    }
    ImGui::Dummy(ImVec2(0, S(2)));

    // sizes: page list | page, the help under them, the keys at the bottom (one line, or two in a
    // narrow window that has the height)
    const bool low = H < b * 30.0f;
    char close[64];
    Hints hints = hints_now(close, sizeof close);
    ImGui::PushFont(nullptr, small_px());
    if (low) hints_one_line(&hints, inner_w);
    const float hint_h = (float)hint_lines(hints, inner_w) * ImGui::GetTextLineHeightWithSpacing();
    ImGui::PopFont();
    const float avail_h = ImGui::GetContentRegionAvail().y - hint_h - ImGui::GetStyle().ItemSpacing.y * 2.0f;
    // The help box: as tall as the longest help needs at this width (one size while the focus moves),
    // never under three lines and never more than two fifths of the room. When that would leave the
    // page less than about four rows, there is no box: the focused row shows its help itself.
    float help_h = help_box_need(inner_w - S(18) * 2.0f, S(12));
    if (help_h < b * 3.4f) help_h = b * 3.4f;
    if (help_h > avail_h * 0.42f) help_h = avail_h * 0.42f;
    help_h = floorf(help_h);
    (void)help_h;
    g_inline_help = true;                       // no box under the page: a row's few words show under it when it has the focus
    g_show_heading = avail_h >= b * 14.0f;
    ui.tv.help_inline = g_inline_help;
    const float body_h = floorf(g_inline_help ? avail_h + ImGui::GetStyle().ItemSpacing.y : avail_h - help_h);
    float nav_w = 0.0f;
    for (int p = 0; p < GT5_PAGE_COUNT; p++) { const float w = ImGui::CalcTextSize(page_name(p)).x; if (w > nav_w) nav_w = w; }
    nav_w = floorf(nav_w + S(tight ? 20 : 28) * 2.0f + S(8) * 2.0f);
    if (nav_w < S(110)) nav_w = S(110);
    // the page list's entries share its height, two lines of text each at most (1.4 at least: in a
    // very low window the list scrolls, with the wheel or as the focus moves)
    const float entry_h = floorf(clampf((body_h - S(8) * 2.0f) / (float)GT5_PAGE_COUNT - ImGui::GetStyle().ItemSpacing.y, b * 1.4f, b * 2.0f));

    // the page list
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kNav);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8), S(8)));
    ImGui::BeginChild("##nav", ImVec2(nav_w, body_h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    bool nav_focused = false;
    for (int p = 0; p < GT5_PAGE_COUNT; p++) {
        if (ui.focus_nav && p == ui.page) { ImGui::SetKeyboardFocusHere(); ui.focus_nav = false; }
        const bool sel = p == ui.page;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Header, kAccent);
        ImGui::PushID(p);
        const bool pressed = ImGui::Selectable("##page", sel, 0, ImVec2(0, entry_h));
        ImGui::PopID();
        if (sel) ImGui::PopStyleColor();
        const ImVec2 a = ImGui::GetItemRectMin(), z = ImGui::GetItemRectMax();
        rect_out(ui.tv.nav[p], a, z);
        ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + S(tight ? 12 : 18), floorf((a.y + z.y - ImGui::GetTextLineHeight()) * 0.5f)),
                                            ImGui::GetColorU32(sel ? kText : kDim), page_name(p));
        const bool foc = ImGui::IsItemFocused();
        if (foc) nav_focused = true;
        // The page follows the focus while it moves in the page list (not when the focus only passes
        // through on its way into a page).
        if (foc && !sel && ui.keyboard_last && !ui.focus_nav && !ui.in_content) { ui.page = p; ui.sub = SUB_NONE; bind_cancel(); }
        if (pressed) { ui.page = p; ui.sub = SUB_NONE; bind_cancel(); ui.in_content = true; ui.focus_content = true; }
    }
    if (nav_focused) {
        ui.in_content = false;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false)) {
            ImGui::NavMoveRequestCancel();
            ui.in_content = true; ui.focus_content = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, false)) {
            ImGui::NavMoveRequestCancel();
        }
        if (!g_inline_help) set_help_text(help_nav());
    }
    ImGui::NavMoveRequestTryWrapping(ImGui::GetCurrentWindow(), ImGuiNavMoveFlags_LoopY);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // the page
    ImGui::SameLine(0.0f, S(tight ? 8 : 12));
    if (ui.page != ui.shown_page || ui.sub != ui.shown_sub) {      // another page or map: from its top
        ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
        ui.shown_page = ui.page; ui.shown_sub = ui.sub;
        ui.scrolled_to[0] = 0;
    }
    if (tight) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(12)));
    ImGui::BeginChild("##page", ImVec2(0, body_h), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    if (tight) ImGui::PopStyleVar();
    {
        const ImRect& r = ImGui::GetCurrentWindow()->InnerRect;
        rect_out(ui.tv.page, r.Min, r.Max);
    }
    g_rows = RowState();
    if (ui.sub == SUB_KEYS) keys_screen(pad);
    else if (ui.sub == SUB_WHEEL) wheel_screen(pad);
    else if (ui.page == GT5_PAGE_ABOUT) about_page();
    else settings_page(ui.page, pad);
    if (g_rows.any_focus) ui.in_content = true;
    else ui.scrolled_to[0] = 0;
    ui.focus_content = false;          // asked for and nothing took it (an empty page): don't carry it over
    ui.focus_row = -1;
    if (pad.ry != 0.0f) ImGui::SetScrollY(ImGui::GetScrollY() + pad.ry * S(1100) * io.DeltaTime);
    ImGui::NavMoveRequestTryWrapping(ImGui::GetCurrentWindow(), ImGuiNavMoveFlags_LoopY);
    ImGui::EndChild();

    if (!g_inline_help) {
        footer(help_h);
        ui.tv.help_cut = g_help_needs > help_h + 1.0f;
    }

    // the keys
    ImGui::PushFont(nullptr, small_px());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(2));
    hint_draw(hints, inner_w);
    ImGui::PopFont();
    ImGui::End();

    ui.popup_was_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) || ImGui::IsAnyItemActive();
}

}  // namespace gt5m
