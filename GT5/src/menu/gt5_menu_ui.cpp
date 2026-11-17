// gt5_menu_ui.cpp - what the settings menu shows: the pages, their rows, the key and wheel maps, the
// readout, the help at the bottom. Everything on screen comes from the registry (gt5_settings.h); the
// texts here are only the menu's own furniture. UTF-8.
//
// Moving about, the same with keys and with a controller:
//   page list    Up/Down pick a page, Right or Enter/Cross goes into it, Esc/Circle closes the menu
//   a page       Up/Down move, Left/Right change the value, Enter/Cross opens the list or presses,
//                Esc/Circle goes back to the page list, Page Up/Down or L1/R1 change page
// The mouse works on everything as usual.
#include "gt5_menu_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "imgui_internal.h"   // NavMoveRequestCancel, NavMoveRequestTryWrapping

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
    default: return T("About", "关于");
    }
}

float S(float v) { return v * ui.scale; }
float base_px() { return ImGui::GetStyle().FontSizeBase; }

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

// ---- one row ------------------------------------------------------------------------------------------

struct RowState { bool first = true; bool any_focus = false; };
RowState g_rows;
float g_help_needs = 0.0f;      // the height the help text took in the last frame

// The focus goes to the widget that comes next when this row was asked for.
void take_focus_if_asked(const char* key)
{
    const bool by_key = ui.focus_key[0] && key && !strcmp(ui.focus_key, key);
    if (by_key || (ui.focus_content && g_rows.first && !ui.focus_key[0])) {
        ImGui::SetKeyboardFocusHere();
        ui.focus_content = false;
        ui.focus_key[0] = 0;
    }
    g_rows.first = false;
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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(*v ? kAccent : (*hovered ? kNavHot : kPanel)), r);
    dl->AddCircleFilled(ImVec2(p.x + (*v ? w - r : r), p.y + r), r - S(4), ImGui::GetColorU32(*v ? kText : kDim));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(*v ? kText : kDim, "%s", *v ? T("On", "开") : T("Off", "关"));   // (launcher) on / off
    return pressed;
}

// A list of choices. Left/Right step through the ones this build can do; Enter/Cross opens the list.
// Returns the index picked, or -1.
int choice_box(const char* preview, int count, int cur, const char* (*label)(int, void*), bool (*available)(int, void*), void* user,
               bool* focused, bool* hovered)
{
    int picked = -1;
    const bool open = ImGui::BeginCombo("##v", preview, ImGuiComboFlags_HeightLarge);
    *focused = ImGui::IsItemFocused();
    *hovered = ImGui::IsItemHovered();
    if (open) {
        for (int i = 0; i < count; i++) {
            const bool ok = available(i, user);
            char text[160];
            snprintf(text, sizeof text, "%s%s", label(i, user), ok ? "" : T("   (not in this version yet)", "   （此版本尚不可用）"));
            if (ImGui::Selectable(text, i == cur, ok ? 0 : ImGuiSelectableFlags_Disabled)) picked = i;
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

// Frame generation is one row over two settings: LD_FRAMEGEN (on) and LD_FRAMEGEN_MULT (2x to 4x).
const char* fg_label(int i, void*)
{
    switch (i) {
    case 0: return T("Off", "关");
    case 1: return T("Fill the frames left out", "补上省略的帧");
    case 2: return "2x";
    case 3: return "3x";
    default: return "4x";
    }
}
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
        gt5_setting_stored("LD_FRAMEGEN", v, sizeof v);
        if (!v[0] && gt5_setting_int("LD_FRAMEGEN_MULT", 1) < 2)
            return T("turn frame generation on above first", "请先在上方开启帧生成");
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

void badge(const gt5_setting_def* d, bool unavailable, const char* idle)
{
    ImGui::PushFont(nullptr, base_px() * 0.72f);
    // The anti-aliasing row says what the renderer says: why DLSS can't be used here (no plug-in, the
    // graphics card, the driver), or what it is doing right now.
    gt5_menu_stats aa;
    const bool aa_row = !strcmp(d->key, "LD_AA") && stats_get(&aa) && aa.aa_state[0];
    if (unavailable && aa_row) {
        ImGui::TextColored(kNext, "%s", aa.aa_state);
        ImGui::TextColored(kDim, "%s", T("add NVIDIA DLSS in the launcher: Display, Anti-aliasing", "请在启动器中添加 NVIDIA DLSS：显示 > 抗锯齿"));
    }
    else if (unavailable) ImGui::TextColored(kDim, "%s", T("not in this version yet", "此版本尚不可用"));
    else if (idle) ImGui::TextColored(kDim, "%s", idle);
    else if (!strcmp(d->key, "LD_FRAMEGEN") && stats_get(&aa) && aa.fg_state[0])
        ImGui::TextColored(kLive, "%s", aa.fg_state);   // what is being made and shown right now, or why not
    else if (aa_row) {
        char cur[32];
        gt5_setting_stored("LD_AA", cur, sizeof cur);
        if (cur[0]) ImGui::TextColored(kLive, "%s", aa.aa_state);   // "DLAA, 2304x972" / "not in this frame (menu or 2D)"
        else ImGui::TextColored(kLive, "%s", T("applies now", "立即生效"));
    }
    else if (gt5_setting_pending_restart(d->key)) ImGui::TextColored(kNext, "%s", T("restart the game to apply", "重启游戏后生效"));
    else if (gt5_setting_apply_of(d->key) == GT5_APPLY_LIVE) ImGui::TextColored(kLive, "%s", T("applies now", "立即生效"));
    else ImGui::TextColored(kNext, "%s", T("from the next start", "下次启动生效"));
    ImGui::PopFont();
}

void key_names(const char* stored, char* out, size_t cap)
{
    snprintf(out, cap, "%s", stored && *stored ? gt5_kb_pretty(gt5_kb_canon(stored) ? gt5_kb_canon(stored) : stored) : "F1");
}

void setting_row(const gt5_setting_def* d, const PadEdges& pad)
{
    ImGui::PushID(d->key);
    const unsigned flags = gt5_setting_flags(d->key);
    const bool unavailable = (flags & GT5_SF_NEEDS_HOST) && !(flags & GT5_SF_ENABLED);
    const char* idle = unavailable ? nullptr : row_idle_reason(d->key);
    const float full = ImGui::GetContentRegionAvail().x;
    const float label_w = floorf(full * 0.47f), widget_w = full - label_w;
    const ImVec2 at = ImGui::GetCursorPos();

    ImGui::AlignTextToFramePadding();
    ImGui::PushTextWrapPos(at.x + label_w - S(18));
    ImGui::TextUnformatted(label_of(d));
    ImGui::PopTextWrapPos();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y * 0.9f);
    badge(d, unavailable, idle);
    const float label_bottom = ImGui::GetCursorPosY();

    ImGui::SetCursorPos(ImVec2(at.x + label_w, at.y));
    ImGui::SetNextItemWidth(widget_w);
    take_focus_if_asked(d->key);
    bool focused = false, hovered = false;
    char cur[1200];
    gt5_setting_stored(d->key, cur, sizeof cur);

    if (unavailable) {
        // Still a place the focus can rest on, so its help can be read with a controller.
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::Button("—##v", ImVec2(widget_w, 0));
        ImGui::PopStyleColor();
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
            snprintf(shown, sizeof shown, "%s", T("Change the wheel's buttons and pedals...", "更改方向盘按钮和踏板..."));
        }
        char label[200];
        snprintf(label, sizeof label, "%s##v", shown);
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::Button(label, ImVec2(widget_w, 0));
        ImGui::PopStyleColor();
        focused = ImGui::IsItemFocused(); hovered = ImGui::IsItemHovered();
    } else if (!strcmp(d->key, "LD_FRAMEGEN")) {
        // A multiplier of 2 or more turns frame generation on by itself (the launcher's Frame rate
        // multiplier sets only that), so the row shows it as on, and Off clears both settings: with only
        // LD_FRAMEGEN cleared it stayed on, shown as "Off" (v0.1.99, v0.1.100).
        const int mult = gt5_setting_int("LD_FRAMEGEN_MULT", 1);
        const bool multiplied = mult >= 2 && mult <= 4;
        // LD_FRAMEGEN is 1 (frames made from the picture) or 2 (from the game's motion): the row below,
        // "Frame generation: how", says which, and this row writes the one that matches.
        const int now = multiplied ? mult : ((!strcmp(cur, "1") || !strcmp(cur, "2")) ? 1 : 0);
        const int pick = choice_box(fg_label(now, nullptr), 5, now, fg_label, fg_ok, nullptr, &focused, &hovered);
        if (pick >= 0 && pick != now) {
            char m[8]; snprintf(m, sizeof m, "%d", pick);
            gt5_setting_set("LD_FRAMEGEN", pick ? (gt5_setting_is("LD_FG_METHOD", "2") ? "2" : "1") : "");
            gt5_setting_set("LD_FRAMEGEN_MULT", pick >= 2 ? m : "");
            changed();
        }
    } else if (!strcmp(d->key, "PS3_KEYS")) {
        if (ImGui::Button(T("Change the keys...", "更改按键..."), ImVec2(widget_w, 0))) {
            gt5_kb_parse(cur, &ui.keys);
            ui.sub = SUB_KEYS; ui.focus_content = true;
        }
        focused = ImGui::IsItemFocused(); hovered = ImGui::IsItemHovered();
    } else if (!strcmp(d->key, "PS3_WHEEL_BUTTONS")) {
        if (ImGui::Button(T("Change the wheel's buttons and pedals...", "更改方向盘按钮和踏板..."), ImVec2(widget_w, 0))) {
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
        if (ImGui::Button(waiting ? T("Press a key...", "请按一个键...") : name, ImVec2(widget_w, 0)) && !waiting)   // (launcher) kb.press
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
        for (int i = 0; i < d->n_choices; i++) if (!_stricmp(d->choices[i].value, cur)) now = i;
        DefChoices dc = { d };
        const char* preview = now >= 0 ? choice_label(&d->choices[now]) : cur[0] ? cur : T("(not set)", "（未设置）");
        const int pick = choice_box(preview, d->n_choices, now, def_choice_label, def_choice_ok, &dc, &focused, &hovered);
        if (pick >= 0 && pick != now) {
            gt5_setting_set(d->key, d->choices[pick].value);
            // The generator is picked by LD_FRAMEGEN's own value (1 or 2): keep it in step while frame
            // generation is on (also when only the multiplier switched it on).
            if (!strcmp(d->key, "LD_FG_METHOD")) {
                char on[8];
                gt5_setting_stored("LD_FRAMEGEN", on, sizeof on);
                if (on[0] || gt5_setting_int("LD_FRAMEGEN_MULT", 1) >= 2)
                    gt5_setting_set("LD_FRAMEGEN", !strcmp(d->choices[pick].value, "2") ? "2" : "1");
            }
            changed();
        }
        break;
    }
    case GT5_SET_INT:
    case GT5_SET_FLOAT: {
        float v = (float)gt5_setting_float(d->key, d->num_def);
        char fmt[32];
        snprintf(fmt, sizeof fmt, d->type == GT5_SET_INT ? "%%.0f%s" : "%%.1f%s", d->unit ? (!strcmp(d->unit, "%") ? "%%" : d->unit) : "");
        const bool wide = d->num_max / (d->num_min > 0 ? d->num_min : 1) >= 100;
        bool edited = ImGui::SliderFloat("##v", &v, (float)d->num_min, (float)d->num_max, fmt,
                                         ImGuiSliderFlags_AlwaysClamp | (wide ? ImGuiSliderFlags_Logarithmic : 0));
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
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::Button(cur[0] ? cur : T("(not set)", "（未设置）"), ImVec2(widget_w, 0));
        ImGui::PopStyleColor();
        focused = ImGui::IsItemFocused(); hovered = ImGui::IsItemHovered();
        break;
    }
    }
    if (focused) { g_rows.any_focus = true; set_help_setting(d); }
    else if (hovered && !ui.keyboard_last) set_help_setting(d);

    if (ImGui::GetCursorPosY() < label_bottom) ImGui::SetCursorPosY(label_bottom);
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
    ImGui::BeginChild("##readout", ImVec2(0, base_px() * 5.4f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
    if (!have) {
        ImGui::TextColored(kDim, "%s", T("No numbers from the renderer yet.", "渲染器尚未提供数据。"));
    } else {
        const float plot_w = S(300);
        const float left_w = ImGui::GetContentRegionAvail().x - plot_w - S(16);
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + left_w);
        stats_lines(st, false);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        if (ui.fps_n > 1) {
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - plot_w);
            float top = 60.0f;
            for (int i = 0; i < ui.fps_n; i++) if (ui.fps_hist[i] > top) top = ui.fps_hist[i];
            ImGui::PushStyleColor(ImGuiCol_PlotLines, kLive);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, kNav);
            ImGui::PlotLines("##fps", ui.fps_hist, ui.fps_n, 0, T("game frames a second", "游戏帧率"), 0.0f, top * 1.1f, ImVec2(plot_w, base_px() * 3.9f));
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::EndChild();
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
    ImGui::Dummy(ImVec2(0, S(6)));
}

void pending_note()
{
    char list[600];
    size_t n = 0;
    list[0] = 0;
    for (int i = 0; i < gt5_settings_count(); i++) {
        const gt5_setting_def* d = gt5_settings_at(i);
        if (d->page == GT5_PAGE_NONE || !gt5_setting_pending_restart(d->key)) continue;
        n += (size_t)snprintf(list + n, sizeof list - n, "%s%s", n ? ", " : "", label_of(d));
        if (n >= sizeof list) { n = sizeof list - 1; break; }
    }
    if (!n) return;
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kNext, "%s %s", T("Waiting for the next start of the game:", "将在下次启动游戏时生效："), list);
    ImGui::PopTextWrapPos();
}

void settings_page(int page, const PadEdges& pad)
{
    heading(page_name(page));
    if (page == GT5_PAGE_PERFORMANCE) readout_block();
    for (int i = 0; i < gt5_settings_count(); i++) {
        const gt5_setting_def* d = gt5_settings_at(i);
        if (d->page != page || (d->flags & GT5_SF_NO_ROW)) continue;
        setting_row(d, pad);
    }
    pending_note();
}

void link_button(const char* text, const char* url, float w)
{
    if (g_rows.first && ui.focus_content) { ImGui::SetKeyboardFocusHere(); ui.focus_content = false; }
    g_rows.first = false;
    if (ImGui::Button(text, ImVec2(w, 0))) { if (url) open_url(url); else open_logs_folder(); }
    if (ImGui::IsItemFocused()) {
        g_rows.any_focus = true;
        set_help_text(url ? url : T("GT5\\logs: play.err is what a problem report needs.", "GT5\\logs：报告问题时需要其中的 play.err。"));
    }
}

void about_page()
{
    heading(page_name(GT5_PAGE_ABOUT));
    ImGui::PushFont(nullptr, base_px() * 1.3f);
    ImGui::TextUnformatted(title_text());
    ImGui::PopFont();
    ImGui::TextColored(kDim, "%s", T("PC recompilation", "PC 重编译版"));                               // (launcher) subtitle
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::Text("%s:  %s", T("Version", "版本"), version_text()[0] ? version_text() : T("development build", "开发版本"));   // (launcher) version, devbuild
    gt5_menu_stats st;
    if (stats_get(&st) && st.gpu_name[0]) ImGui::Text("%s:  %s", T("Graphics card", "显卡"), st.gpu_name);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("%s:  %s", T("Settings file", "设置文件"), gt5_settings_path());
    ImGui::Dummy(ImVec2(0, S(10)));
    ImGui::TextUnformatted(T("An unofficial PC port of Gran Turismo 5 made by static recompilation. It needs your own copy of the game; nothing from the game is distributed.",
                             "通过静态重编译制作的非官方《GT赛车5》PC 移植版。需要你自己拥有的游戏副本；不分发任何游戏内容。"));   // (launcher) help.about
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, S(10)));
    const float w = S(380);
    link_button(T("Report a problem", "报告问题"), "https://github.com/MormonFTP-hub/gt5-recomp/issues/new/choose", w);   // (launcher) help.*
    link_button(T("Read the FAQ", "常见问题"), "https://github.com/MormonFTP-hub/gt5-recomp#faq", w);
    link_button(T("Version history", "版本历史"), "https://github.com/MormonFTP-hub/gt5-recomp/blob/main/VERSIONS.md", w);
    link_button(T("Changelog", "更新日志"), "https://github.com/MormonFTP-hub/gt5-recomp/blob/main/CHANGELOG.md", w);
    link_button(T("Open the logs folder", "打开日志文件夹"), nullptr, w);
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::TextColored(kDim, "%s", T("Discord: mormonftp", "Discord：mormonftp"));
    ImGui::TextColored(kDim, "%s Dear ImGui %s (MIT).", T("The menu is drawn with", "菜单绘制使用"), ImGui::GetVersion());
}

// ---- the key map ---------------------------------------------------------------------------------------

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

// A row of a map: three columns of text over one thing the focus can rest on. Returns true when
// it was pressed; *focused and *right_click say the rest.
bool map_row(int id, int row_index, const char* a, const char* b, const char* c, const ImVec4& c_col, bool waiting, bool* focused, bool* right_click)
{
    ImGui::PushID(id);
    if (ui.focus_row == row_index || (ui.focus_content && g_rows.first && ui.focus_row < 0)) {
        ImGui::SetKeyboardFocusHere();
        ui.focus_content = false; ui.focus_row = -1;
    }
    g_rows.first = false;
    const float full = ImGui::GetContentRegionAvail().x;
    const ImVec2 at = ImGui::GetCursorPos();
    const float h = ImGui::GetFrameHeight();
    if (waiting) ImGui::PushStyleColor(ImGuiCol_Header, kAccent);
    const bool pressed = ImGui::Selectable("##row", waiting, ImGuiSelectableFlags_AllowOverlap, ImVec2(full, h));
    if (waiting) ImGui::PopStyleColor();
    *focused = ImGui::IsItemFocused();
    *right_click = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    const float pad_y = (h - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPos(ImVec2(at.x + S(10), at.y + pad_y));
    ImGui::TextUnformatted(a);
    ImGui::SetCursorPos(ImVec2(at.x + full * 0.42f, at.y + pad_y));
    ImGui::TextColored(kDim, "%s", b);
    ImGui::SetCursorPos(ImVec2(at.x + full * 0.68f, at.y + pad_y));
    ImGui::TextColored(c_col, "%s", c);
    ImGui::SetCursorPos(ImVec2(at.x, at.y + h + ImGui::GetStyle().ItemSpacing.y * 0.4f));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
    if (*focused) g_rows.any_focus = true;
    return pressed;
}

void keys_screen(const PadEdges& pad)
{
    heading(T("Keyboard keys", "键盘按键"));                                                       // (launcher) kb.title
    const char* help = T("Enter or Cross on a row, then press the key you want (Esc cancels). Insert or Square adds a second key, Delete or Triangle clears the row. A key that's already used moves to the new row. The keyboard works next to a controller. The actions are GT5's own pad layout (Options > Hardware > Change Button Assignments in the game); change them there and these rows follow the button, not the action.",
                         "在一行上按 Enter 或 ✕，然后按下想要的键（Esc 取消）。Insert 或 □ 添加第二个键，Delete 或 △ 清除该行。已被使用的键会移到新的一行。键盘可与手柄同时使用。动作为 GT5 自带的手柄布局（游戏中：选项 > 硬件 > 更改按键分配）；在那里更改后，这里的行对应的是按钮而不是动作。");
    if (g_rows.first && ui.focus_content && ui.focus_row < 0) { /* the first row below takes it */ }
    if (ImGui::Button(T("Reset to defaults", "恢复默认"), ImVec2(S(300), 0))) { bind_cancel(); gt5_kb_defaults(&ui.keys); save_keys(); }   // (launcher) kb.reset
    if (ImGui::IsItemFocused()) { g_rows.any_focus = true; set_help_text(help); }
    ImGui::Dummy(ImVec2(0, S(4)));
    for (int b = 0; b < GT5_KB_BUTTONS; b++) {
        if (b == GT5_KB_FIRST_STICK) {
            ImGui::Dummy(ImVec2(0, S(6)));
            ImGui::TextColored(kDim, "%s", T("Sticks (full tilt)", "摇杆（推到底）"));                    // (launcher) kb.stick
        }
        char keys[160];
        size_t n = 0;
        keys[0] = 0;
        for (int k = 0; k < gt5_kb_count(&ui.keys, b); k++)
            n += (size_t)snprintf(keys + n, sizeof keys - n, "%s%s", k ? "  /  " : "", gt5_kb_pretty(ui.keys.key[b][k]));
        const bool waiting = ui.bind == BIND_KEY_ROW && ui.bind_row == b;
        bool focused = false, rclick = false;
        const bool pressed = map_row(b, b, gt5_kb_action_label(b, ui.zh), gt5_kb_button_label(b, ui.zh),
                                     waiting ? T("Press a key...", "请按一个键...") : n ? keys : T("(no key)", "（无按键）"),   // (launcher) kb.press, kb.none
                                     waiting || n ? kText : kDim, waiting, &focused, &rclick);
        if (ui.bind == BIND_NONE) {
            if (pressed) bind_begin(BIND_KEY_ROW, b, false);
            else if (rclick || (focused && (pad.square || ImGui::IsKeyPressed(ImGuiKey_Insert, false)))) bind_begin(BIND_KEY_ROW, b, true);
            else if (focused && (pad.triangle || ImGui::IsKeyPressed(ImGuiKey_Delete, false))) { gt5_kb_clear(&ui.keys, b); save_keys(); }
        }
        if (focused) set_help_text(help);
    }
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDim, "%s", T("Left View, Right View and Flash Lights have no pad button in GT5's default layout: give them one in the game (for example the right stick's left and right), then bind a key to that button here.",
                                     "在 GT5 默认布局中，“左视角”“右视角”和“闪灯”没有手柄按钮：先在游戏中为它们分配一个按钮（例如右摇杆的左和右），再在这里为该按钮绑定按键。"));   // (launcher) kb.views
    ImGui::PopTextWrapPos();
}

// ---- the wheel map -------------------------------------------------------------------------------------

void wheel_screen(const PadEdges& pad)
{
    heading(T("Wheel buttons and pedals", "方向盘按钮和踏板"));
    gt5_menu_wheel w;
    const bool have = wheel_get(&w);
    const char* help = T("Enter or Cross on a row, then press the button on the wheel, or push the pedal all the way down (Esc cancels). Insert or Square: no button (for a pedal: it reads the other way round). Delete or Triangle: back to what the wheel's model has. The map counts from the next start of the game.",
                         "在一行上按 Enter 或 ✕，然后按下方向盘上的按钮，或把踏板踩到底（Esc 取消）。Insert 或 □：不使用按钮（踏板：反向读取）。Delete 或 △：恢复为该型号方向盘的默认值。映射在下次启动游戏时生效。");
    ImGui::PushTextWrapPos(0.0f);
    if (have) ImGui::TextColored(kLive, "%s  %s", T("Reading:", "正在读取："), w.name[0] ? w.name : T("a steering wheel", "方向盘"));
    else ImGui::TextColored(kNext, "%s", T("No steering wheel is being read, so a button can't be picked by pressing it. Plug the wheel in before starting the game.",
                                           "当前没有读取到方向盘，无法通过按下按钮来选择。请在启动游戏前连接方向盘。"));
    ImGui::PopTextWrapPos();
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

    ImGui::TextColored(kDim, "%s", T("Pedals", "踏板"));
    for (int p = 0; p < 3; p++) {
        char val[64];
        const int a = ui.wheel.pedal_axis[p];
        if (a == -2) snprintf(val, sizeof val, "%s", T("As the wheel has it", "按方向盘默认"));
        else if (a == -1) snprintf(val, sizeof val, "%s", T("None", "无"));
        else snprintf(val, sizeof val, "%s %s%s", T("Axis", "轴"), gt5_wheel_axis(a), ui.wheel.pedal_inv[p] ? T(", the other way round", "，反向") : "");
        const bool waiting = ui.bind == BIND_WHEEL_PEDAL && ui.bind_row == p;
        bool focused = false, rclick = false;
        const bool pressed = map_row(1000 + p, GT5_WHEEL_BUTTONS + p, gt5_wheel_pedal_label(p, ui.zh), "",
                                     waiting ? T("Push the pedal down...", "请踩下踏板...") : val, waiting || a != -2 ? kText : kDim, waiting, &focused, &rclick);
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
        char val[64];
        const int n = ui.wheel.btn[i];
        if (n < 0) snprintf(val, sizeof val, "%s", T("As the wheel has it", "按方向盘默认"));
        else if (n == 0) snprintf(val, sizeof val, "%s", T("None", "无"));
        else snprintf(val, sizeof val, T("Button %d", "按钮 %d"), n);
        const bool waiting = ui.bind == BIND_WHEEL_BUTTON && ui.bind_row == i;
        bool focused = false, rclick = false;
        const bool pressed = map_row(i, i, gt5_wheel_button_label(i, ui.zh), gt5_wheel_button(i),
                                     waiting ? T("Press a button on the wheel...", "请按下方向盘上的按钮...") : val, waiting || n >= 0 ? kText : kDim, waiting, &focused, &rclick);
        if (ui.bind == BIND_NONE) {
            if (pressed && have) bind_begin(BIND_WHEEL_BUTTON, i, false);
            else if (rclick || (focused && (pad.square || ImGui::IsKeyPressed(ImGuiKey_Insert, false)))) { ui.wheel.btn[i] = 0; save_wheel(); }
            else if (focused && (pad.triangle || ImGui::IsKeyPressed(ImGuiKey_Delete, false))) { ui.wheel.btn[i] = -1; save_wheel(); }
        }
        if (focused) set_help_text(help);
    }
}

// ---- the bottom: help for what the focus is on -----------------------------------------------------------

void footer(float height)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kNav);
    ImGui::BeginChild("##help", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
    const gt5_setting_def* d = ui.help_key[0] ? gt5_settings_find(ui.help_key) : nullptr;
    ImGui::PushTextWrapPos(0.0f);
    if (d) {
        ImGui::TextColored(kText, "%s", label_of(d));
        ImGui::SameLine();
        ImGui::PushFont(nullptr, base_px() * 0.72f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kDim, "   %s", d->key);
        ImGui::PopFont();
        const char* h = help_of(d);
        const unsigned flags = gt5_setting_flags(d->key);
        ImGui::PushFont(nullptr, base_px() * 0.86f);
        if (h) ImGui::TextColored(kDim, "%s", h);
        if (!strcmp(d->key, "LD_FRAMEGEN")) {
            const gt5_setting_def* m = gt5_settings_find("LD_FRAMEGEN_MULT");
            if (m && help_of(m)) ImGui::TextColored(kDim, "%s", help_of(m));
        }
        if ((flags & GT5_SF_NEEDS_HOST) && !(flags & GT5_SF_ENABLED))
            ImGui::TextColored(kNext, "%s", T("This version of the port doesn't have it yet.", "此版本的移植尚未包含该功能。"));
        else if (gt5_setting_apply_of(d->key) == GT5_APPLY_RESTART)
            ImGui::TextColored(kNext, "%s", T("A change counts the next time the game starts.", "更改将在下次启动游戏时生效。"));
        ImGui::PopFont();
    } else if (ui.help_text[0]) {
        ImGui::PushFont(nullptr, base_px() * 0.86f);
        ImGui::TextColored(kDim, "%s", ui.help_text);
        ImGui::PopFont();
    }
    ImGui::PopTextWrapPos();
    g_help_needs = ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y;   // what this text takes, for the next frame
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}  // namespace

// ---- style ---------------------------------------------------------------------------------------------

void ui_apply_style(float scale)
{
    ImGuiStyle st;
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 12; st.ChildRounding = 8; st.FrameRounding = 6; st.PopupRounding = 8; st.GrabRounding = 6; st.ScrollbarRounding = 8;
    st.WindowPadding = ImVec2(24, 20); st.FramePadding = ImVec2(12, 8); st.ItemSpacing = ImVec2(12, 9); st.ItemInnerSpacing = ImVec2(8, 6);
    st.ScrollbarSize = 14; st.GrabMinSize = 18;
    st.WindowBorderSize = 1; st.ChildBorderSize = 0; st.FrameBorderSize = 0; st.PopupBorderSize = 1;
    st.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    st.Colors[ImGuiCol_Text] = kText;
    st.Colors[ImGuiCol_TextDisabled] = ImVec4(kDim.x, kDim.y, kDim.z, 0.55f);
    st.Colors[ImGuiCol_WindowBg] = kBg;
    st.Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_PopupBg] = kPanel;
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
    ImGui::PushFont(nullptr, base_px() * 0.8f);
    stats_lines(st, true);
    ImGui::PopFont();
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

    float pw = S(1500), ph = H * 0.88f;
    if (pw > W - S(40)) pw = W - S(40);
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(pw, ph));
    ImGui::Begin("##gt5menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // the top line
    ImGui::PushFont(nullptr, base_px() * 1.3f);
    ImGui::TextUnformatted(T("Settings", "设置"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + base_px() * 0.36f);
    ImGui::TextColored(kDim, "   %s", title_text());
    {
        const double t = now_s();
        const char* note = ui.save_failed ? T("settings.bat couldn't be written", "无法写入 settings.bat")
                         : (ui.saved_at != 0 && t - ui.saved_at < 2.5) ? T("Saved", "已保存") : nullptr;
        if (note) {
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(note).x - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextColored(ui.save_failed ? kAccentHot : kLive, "%s", note);
        }
    }
    ImGui::Dummy(ImVec2(0, S(4)));

    // sizes: page list | page, the help under them, one line of keys at the bottom
    const float hint_h = base_px() * 0.8f + S(10);
    // The help is as tall as its text needs (a long one at a large size takes five lines), within reason.
    float help_h = g_help_needs;
    if (help_h < base_px() * 4.6f) help_h = base_px() * 4.6f;
    if (help_h > ph * 0.36f) help_h = ph * 0.36f;
    const float body_h = ImGui::GetContentRegionAvail().y - help_h - hint_h - ImGui::GetStyle().ItemSpacing.y * 2;
    const float nav_w = S(250);

    // the page list
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kNav);
    ImGui::BeginChild("##nav", ImVec2(nav_w, body_h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    bool nav_focused = false;
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.08f, 0.5f));
    for (int p = 0; p < GT5_PAGE_COUNT; p++) {
        if (ui.focus_nav && p == ui.page) { ImGui::SetKeyboardFocusHere(); ui.focus_nav = false; }
        const bool sel = p == ui.page;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Header, kAccent);
        const bool pressed = ImGui::Selectable(page_name(p), sel, 0, ImVec2(0, base_px() * 2.0f));
        if (sel) ImGui::PopStyleColor();
        const bool foc = ImGui::IsItemFocused();
        if (foc) nav_focused = true;
        // The page follows the focus while it moves in the page list (not when the focus only passes
        // through on its way into a page).
        if (foc && !sel && ui.keyboard_last && !ui.focus_nav && !ui.in_content) { ui.page = p; ui.sub = SUB_NONE; bind_cancel(); }
        if (pressed) { ui.page = p; ui.sub = SUB_NONE; bind_cancel(); ui.in_content = true; ui.focus_content = true; }
    }
    ImGui::PopStyleVar();
    if (nav_focused) {
        ui.in_content = false;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false)) {
            ImGui::NavMoveRequestCancel();
            ui.in_content = true; ui.focus_content = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, false)) {
            ImGui::NavMoveRequestCancel();
        }
        set_help_text(T("Up and Down pick a page; Right, Enter or Cross goes into it.", "上下选择页面；按右、Enter 或 ✕ 进入。"));
    }
    ImGui::NavMoveRequestTryWrapping(ImGui::GetCurrentWindow(), ImGuiNavMoveFlags_LoopY);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // the page
    ImGui::SameLine();
    ImGui::BeginChild("##page", ImVec2(0, body_h), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    g_rows = RowState();
    if (ui.sub == SUB_KEYS) keys_screen(pad);
    else if (ui.sub == SUB_WHEEL) wheel_screen(pad);
    else if (ui.page == GT5_PAGE_ABOUT) about_page();
    else settings_page(ui.page, pad);
    if (g_rows.any_focus) ui.in_content = true;
    ui.focus_content = false;          // asked for and nothing took it (an empty page): don't carry it over
    ui.focus_row = -1;
    if (pad.ry != 0.0f) ImGui::SetScrollY(ImGui::GetScrollY() + pad.ry * S(1100) * io.DeltaTime);
    ImGui::NavMoveRequestTryWrapping(ImGui::GetCurrentWindow(), ImGuiNavMoveFlags_LoopY);
    ImGui::EndChild();

    footer(help_h);

    // the keys, in one line
    {
        char key[32], name[48];
        gt5_setting_stored("GT5_MENU_KEY", key, sizeof key);
        key_names(key, name, sizeof name);
        char hint[420];
        snprintf(hint, sizeof hint, T("Up / Down: move      Left / Right: change      Enter or Cross: choose      Esc or Circle: back      Page Up / Down or L1 / R1: page      %s: close",
                                      "上 / 下：移动      左 / 右：更改      Enter 或 ✕：选择      Esc 或 ○：返回      Page Up / Down 或 L1 / R1：翻页      %s：关闭"), name);
        ImGui::PushFont(nullptr, base_px() * 0.74f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(2));
        ImGui::TextColored(kDim, "%s", hint);
        ImGui::PopFont();
    }
    ImGui::End();

    ui.popup_was_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) || ImGui::IsAnyItemActive();
}

}  // namespace gt5m
