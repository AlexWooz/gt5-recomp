// gt5_menu.cpp - the in-game settings menu, the part every renderer shares: window messages, the
// controller, fonts, sizes, the window's mode, saving. What is drawn is in gt5_menu_ui.cpp; the drawing
// itself in one file per renderer (gt5_menu_dx12.cpp, gt5_menu_vk.cpp); the API is in gt5_menu.h and
// gt5_menu_vk.h. UTF-8.
//
// Sizes. The player sees the picture the menu is drawn into (the D3D12 frame, the Vulkan swap chain's
// image) stretched to the window, whatever the two sizes are. So the menu is laid out in the window's own
// pixels (Dear ImGui's display is the client area, from WM_SIZE, and its sizes follow the monitor's
// scaling, from WM_DPICHANGED) and drawn into that picture with its pixels per window pixel as Dear
// ImGui's framebuffer scale. Once the picture is stretched back, the text is the size it was laid out
// at, on any window, frame or monitor; and Dear ImGui 1.92 rasterises the glyphs at that framebuffer
// density, so they are as sharp as the picture can hold them.
//
// Dear ImGui's Win32 platform backend isn't used: it reads XInput itself, while here the controller
// comes from the port's own pad code (which also reads DualSense and DualShock 4). The few messages it
// needs are handled below. Both renderers' windows have an HWND on Windows (the Vulkan one is SDL's,
// its window procedure subclassed by the runtime, rsx_vulkan_win32.c), so this code serves both; a
// platform without Win32 messages would feed the same handlers from its own events.
#include "gt5_menu_internal.h"

#include <shellapi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

volatile long g_gt5_menu_captured;   // gt5_menu.h; `long` there so that plain C on any platform can read it
volatile long g_gt5_menu_pause_want; // gt5_menu.h: the menu or the quit question is open ([win] GT5_MENU_PAUSE)

namespace gt5m {

Ui ui;

namespace {

// ---- state -----------------------------------------------------------------------------------------

struct FontFile { HANDLE file = INVALID_HANDLE_VALUE; HANDLE map = nullptr; void* data = nullptr; size_t size = 0; };

struct State {
    bool inited = false;
    HWND hwnd = nullptr;
    char version[96] = "";
    char title[160] = "";
    // the renderer: the size of the picture the menu is drawn into, and its backend's NewFrame
    unsigned w = 0, h = 0;
    void (*new_frame)() = nullptr;
    // fonts
    ImFont* font = nullptr;
    FontFile font_main, font_sym, font_cjk, font_kr;
    bool fallback_fonts = false;
    // input
    SRWLOCK feed_lock = SRWLOCK_INIT;
    gt5_menu_pad pad = {};
    ULONGLONG pad_at = 0;
    gt5_menu_wheel wheel = {};
    unsigned short pad_prev = 0;
    volatile LONG toggle_req = 0, chord_hold = 0, release_wait = 0;
    ULONGLONG chord_t0 = 0, release_until = 0;
    bool chord_fired = false;
    int buttons_down = 0;
    int swallow_up_vk = 0;
    // settings read through the registry
    unsigned gen = ~0u;
    int menu_vk = VK_F1;
    unsigned short chord_mask = 0;
    bool osd = false;
    float user_scale = 1.0f;
    // window mode
    volatile LONG apply_mode = 0, apply_size = 0;
    volatile LONG64 save_due = 0;             // GetTickCount64 at which the changes are written, 0 = none waiting
    WINDOWPLACEMENT windowed_place = {};
    bool have_windowed_place = false;
    // readout
    SRWLOCK stats_lock = SRWLOCK_INIT;
    gt5_menu_stats stats = {};
    bool have_stats = false;
    // notices (gt5_menu_notice)
    SRWLOCK notice_lock = SRWLOCK_INIT;
    struct Notice { char en[96]; char zh[96]; unsigned ms; ULONGLONG until; bool on; } notice[GT5_NOTICE_COUNT] = {};
    volatile LONG notices_on = 0;          // any notice is waiting or showing
    bool notices_allowed = true;           // GT5_NOTICES
    // time
    LARGE_INTEGER qpf = {}, last = {};
    float applied_scale = 0.0f;
    // what the player sees: the window's client area in its own pixels, and the monitor's scaling
    int client_w = 0, client_h = 0;
    UINT dpi = 96;
    bool have_frame = false;              // a frame has been laid out (for the tests)
    // [win]
    ULONGLONG last_frame_tick = 0;        // GetTickCount64 at the last frame a renderer could draw the menu into (core_build_frame): is the picture still coming?
    int hint_state = 0;                   // the first-start hint: 0 not decided yet, 1 to show, -1 not
    double hint_first = 0;                // now_s() at the first frame it could be shown in
};
State s;

void update_captured()
{
    InterlockedExchange(&g_gt5_menu_captured, (ui.open || ui.quit || s.chord_hold || s.release_wait) ? 1 : 0);
    // [win] the game holds still while the menu or the quit question is open (cellPad.c, GT5_MENU_PAUSE)
    InterlockedExchange(&g_gt5_menu_pause_want, (ui.open || ui.quit) ? 1 : 0);
}

// ---- settings the menu itself goes by ------------------------------------------------------------------

void refresh_settings()
{
    if (s.gen == gt5_settings_gen()) return;
    s.gen = gt5_settings_gen();
    char b[64];
    gt5_setting_str("GT5_MENU_KEY", b, sizeof b);
    const int vk = b[0] ? gt5_kb_vk(b) : 0;
    s.menu_vk = vk ? vk : VK_F1;
    gt5_setting_str("GT5_MENU_PAD", b, sizeof b);
    s.chord_mask = !_stricmp(b, "off") ? 0 : !_stricmp(b, "l3r3") ? (unsigned short)((1 << 1) | (1 << 2))
                                                                : (unsigned short)((1 << 0) | (1 << 3));
    s.osd = gt5_setting_bool("GT5_OSD", 0) != 0;
    s.notices_allowed = gt5_setting_bool("GT5_NOTICES", 1) != 0;
    // Menu size: Normal is no value at all. The first menu had an "Automatic" (no value) next to a
    // "Normal" of 100: that 100 is Normal now, and is written as such.
    gt5_setting_stored("GT5_MENU_SCALE", b, sizeof b);
    if (!strcmp(b, "100")) gt5_setting_set("GT5_MENU_SCALE", "");
    const int sc = gt5_setting_int("GT5_MENU_SCALE", 100);
    s.user_scale = (sc >= 50 && sc <= 300) ? (float)sc / 100.0f : 1.0f;
    gt5_setting_str("GT5_MENU_LANG", b, sizeof b);
    if (!b[0]) gt5_setting_str("GT5_LAUNCHER_LANG", b, sizeof b);
    ui.zh = !_stricmp(b, "zh");
}

// ---- fonts -------------------------------------------------------------------------------------------
// Dear ImGui's own font is a 13-pixel bitmap: unreadable from a sofa and no Chinese. The menu uses
// Segoe UI from Windows' Fonts folder, and further fonts for what it lacks (symbols, Chinese and
// Japanese, Korean) only once such text is about to be drawn. The files are mapped, not read: Windows
// pages in the parts the rasteriser touches, so a 20 MB font collection costs what its used glyphs cost.
// Glyphs are rasterised as they are first drawn (Dear ImGui 1.92's dynamic atlas), at whatever size
// the menu has and at the frame's pixels per window pixel (the framebuffer scale set in build_frame),
// so no glyph ranges are set and a change of size, window or frame needs no rebuild here.

bool map_font(const wchar_t* name, FontFile* f)
{
    wchar_t path[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(path, MAX_PATH);
    if (!n || n > MAX_PATH - 40) return false;
    wcscat_s(path, MAX_PATH, L"\\Fonts\\");
    wcscat_s(path, MAX_PATH, name);
    f->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f->file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f->file, &size) || size.QuadPart <= 0 || size.QuadPart > 0x7FFFFFFF) { CloseHandle(f->file); f->file = INVALID_HANDLE_VALUE; return false; }
    f->map = CreateFileMappingW(f->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    f->data = f->map ? MapViewOfFile(f->map, FILE_MAP_READ, 0, 0, 0) : nullptr;
    if (!f->data) {
        if (f->map) CloseHandle(f->map);
        CloseHandle(f->file);
        f->map = nullptr; f->file = INVALID_HANDLE_VALUE;
        return false;
    }
    f->size = (size_t)size.QuadPart;
    return true;
}
void unmap_font(FontFile* f)
{
    if (f->data) UnmapViewOfFile(f->data);
    if (f->map) CloseHandle(f->map);
    if (f->file != INVALID_HANDLE_VALUE) CloseHandle(f->file);
    *f = FontFile();
}
bool map_first(FontFile* f, const wchar_t* const* names, int n, int* which)
{
    for (int i = 0; i < n; i++) if (map_font(names[i], f)) { if (which) *which = i; return true; }
    return false;
}
ImFont* add_font(const FontFile& f, const char* name, ImFont* merge_into)
{
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;      // the mapping is ours, and stays for as long as the font
    cfg.MergeMode = merge_into != nullptr;
    cfg.DstFont = merge_into;
    snprintf(cfg.Name, sizeof cfg.Name, "%s", name);
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(f.data, (int)f.size, 22.0f, &cfg);
}
void fonts_init()
{
    static const wchar_t* const mains[] = { L"segoeui.ttf", L"tahoma.ttf", L"arial.ttf" };
    if (map_first(&s.font_main, mains, 3, nullptr)) s.font = add_font(s.font_main, "main", nullptr);
    if (!s.font) s.font = ImGui::GetIO().Fonts->AddFontDefaultVector();   // no Windows font at all: Dear ImGui's own
    ImGui::GetIO().FontDefault = s.font;
}
void fonts_fallback()
{
    if (s.fallback_fonts || !s.font) return;
    s.fallback_fonts = true;
    static const wchar_t* const syms[] = { L"seguisym.ttf" };
    static const wchar_t* const cjks[] = { L"msyh.ttc", L"msyh.ttf", L"simsun.ttc", L"meiryo.ttc" };
    static const wchar_t* const krs[] = { L"malgun.ttf", L"gulim.ttc" };
    if (map_first(&s.font_sym, syms, 1, nullptr)) add_font(s.font_sym, "symbols", s.font);
    if (map_first(&s.font_cjk, cjks, 4, nullptr)) add_font(s.font_cjk, "cjk", s.font);
    if (map_first(&s.font_kr, krs, 2, nullptr)) add_font(s.font_kr, "korean", s.font);
}

// ---- the window ---------------------------------------------------------------------------------------

ImGuiKey vk_to_key(WPARAM vk, LPARAM lp)
{
    if (vk >= '0' && vk <= '9') return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
    if (vk >= 'A' && vk <= 'Z') return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
    if (vk >= VK_F1 && vk <= VK_F12) return (ImGuiKey)(ImGuiKey_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (ImGuiKey)(ImGuiKey_Keypad0 + (vk - VK_NUMPAD0));
    switch (vk) {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return (HIWORD(lp) & KF_EXTENDED) ? ImGuiKey_KeypadEnter : ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_SHIFT: case VK_LSHIFT: return ImGuiKey_LeftShift;
    case VK_RSHIFT: return ImGuiKey_RightShift;
    case VK_CONTROL: case VK_LCONTROL: return ImGuiKey_LeftCtrl;
    case VK_RCONTROL: return ImGuiKey_RightCtrl;
    case VK_MENU: case VK_LMENU: return ImGuiKey_LeftAlt;
    case VK_RMENU: return ImGuiKey_RightAlt;
    case VK_ADD: return ImGuiKey_KeypadAdd;
    case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
    default: return ImGuiKey_None;
    }
}

void mods_event(ImGuiIO& io)
{
    io.AddKeyEvent(ImGuiMod_Ctrl, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (GetKeyState(VK_MENU) & 0x8000) != 0);
}

// The menu is laid out in the window's pixels: the mouse's position is the menu's.
void mouse_pos_event(ImGuiIO& io, LPARAM lp)
{
    io.AddMousePosEvent((float)(short)LOWORD(lp), (float)(short)HIWORD(lp));
    ui.keyboard_last = false;
}

// [win] No key and no mouse button held: the game reads the keyboard by polling (cellPad.c), so a key
// that answered the menu (Enter on No) must be up before the game gets the input back. Waited for 1.5 s
// at most (a key some driver reports held for good must not keep the game from its input).
bool keys_up()
{
    for (int vk = 1; vk < 0xFF; vk++) {
        if (vk == VK_F1 || vk == VK_F11) continue;     // never game keys (and the menu's own key goes up swallowed)
        if (GetAsyncKeyState(vk) & 0x8000) return false;
    }
    return true;
}

void set_open(bool open)
{
    if (!s.inited || ui.open == open) return;
    ImGuiIO& io = ImGui::GetIO();
    ui.open = open;
    if (open) {
        ui_opened();
        ClipCursor(nullptr);                    // whatever held the mouse in a race lets go
        s.last.QuadPart = 0;
    } else {
        bind_cancel();
        if (gt5_settings_dirty()) { ui.save_failed = !gt5_settings_save(); InterlockedExchange64(&s.save_due, 0); }
        // The press that closed the menu must not reach the game: it stays shut out until the
        // controller's buttons are up again.
        s.release_until = GetTickCount64() + 150;
        InterlockedExchange(&s.release_wait, 1);
        if (GetCapture() == s.hwnd) ReleaseCapture();
        s.buttons_down = 0;
    }
    io.ClearInputKeys();
    io.ClearInputMouse();
    update_captured();
    // The cursor over the window now, not at the next mouse movement.
    PostMessageW(s.hwnd, WM_SETCURSOR, (WPARAM)s.hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    fprintf(stderr, "[menu] %s\n", open ? "opened" : "closed");
}

// ---- window mode and size, applied from here (the launcher does the same from outside at start) ------

// [win] The frame round a window at its monitor's scaling (the process is per-monitor DPI aware).
void adjust_rect_dpi(RECT* r, DWORD style, DWORD ex, UINT dpi)
{
    typedef BOOL (WINAPI* fn_t)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static fn_t f = (fn_t)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
    if (f) f(r, style, FALSE, ex, dpi); else AdjustWindowRectEx(r, style, FALSE, ex);
}

void window_apply(bool mode_changed)
{
    char mode[32], size[32];
    gt5_setting_stored("GT5_LAUNCH_WINDOW", mode, sizeof mode);
    gt5_setting_stored("PS3_WINDOW_SIZE", size, sizeof size);
    const LONG_PTR style = GetWindowLongPtrW(s.hwnd, GWL_STYLE);
    const bool borderless_now = (style & WS_POPUP) != 0 && (style & WS_CAPTION) == 0;
    HMONITOR mon = MonitorFromWindow(s.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);

    if (!_stricmp(mode, "borderless")) {
        if (borderless_now) return;
        s.windowed_place.length = sizeof s.windowed_place;
        s.have_windowed_place = GetWindowPlacement(s.hwnd, &s.windowed_place) != 0;
        SetWindowLongPtrW(s.hwnd, GWL_STYLE, (style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(s.hwnd, nullptr, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    if (borderless_now) {
        SetWindowLongPtrW(s.hwnd, GWL_STYLE, (style & ~(LONG_PTR)WS_POPUP) | WS_OVERLAPPEDWINDOW);
        // Back where the window was (rcNormalPosition is in workspace coordinates: the same as the
         // screen's unless a taskbar sits at the top or the left of the main monitor).
        const RECT& n = s.windowed_place.rcNormalPosition;
        if (s.have_windowed_place && n.right > n.left && n.bottom > n.top) {
            MONITORINFO pm = {};
            pm.cbSize = sizeof pm;
            GetMonitorInfoW(MonitorFromRect(&n, MONITOR_DEFAULTTONEAREST), &pm);
            SetWindowPos(s.hwnd, nullptr, n.left + (pm.rcWork.left - pm.rcMonitor.left), n.top + (pm.rcWork.top - pm.rcMonitor.top),
                         n.right - n.left, n.bottom - n.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            SetWindowPos(s.hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    // Maximizing and restoring are for a window that is on the screen (ShowWindow would show a hidden one).
    const bool shown = IsWindowVisible(s.hwnd) != 0;
    if (!_stricmp(mode, "max")) { if (shown) ShowWindow(s.hwnd, SW_MAXIMIZE); return; }
    if (mode_changed && shown && IsZoomed(s.hwnd)) ShowWindow(s.hwnd, SW_RESTORE);
    // A window: the size asked for, else (coming out of full screen with nothing to go back to) the
    // frame's shape at 720 lines. Kept on the monitor.
    unsigned cw = 0, ch = 0;
    const bool from_full = borderless_now && !s.have_windowed_place;   // [win] nothing to go back to: centred
    if (sscanf(size, "%ux%u", &cw, &ch) != 2 || cw < 64 || ch < 64) {
        if (!from_full) return;
        // [win] 720 lines at 100 % scaling, at this monitor's scaling (the game is DPI aware), in the shape of
        // the picture the game draws: 16:9, or its wide mode's (PS3_ASPECT / PS3_WINDOW_SIZE, display_aspect.c),
        // which the renderer's frame size says (the readout's frame_w x frame_h). Not the back buffer's
        // (s.w x s.h): that is the window's own, in borderless a 21:9 monitor's, and gave a 1707x720 window
        // with bars inside it when the game draws 16:9 there.
        double shape = 16.0 / 9.0;
        gt5_menu_stats st;
        if (stats_get(&st) && st.frame_w > 0 && st.frame_h > 0) shape = (double)st.frame_w / (double)st.frame_h;
        ch = (unsigned)(720u * s.dpi / 96u); cw = (unsigned)((double)ch * shape + 0.5);
    }
    if (shown && IsZoomed(s.hwnd)) ShowWindow(s.hwnd, SW_RESTORE);
    RECT wr = { 0, 0, (LONG)cw, (LONG)ch };
    adjust_rect_dpi(&wr, (DWORD)GetWindowLongPtrW(s.hwnd, GWL_STYLE), (DWORD)GetWindowLongPtrW(s.hwnd, GWL_EXSTYLE), s.dpi);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    const int aw = mi.rcWork.right - mi.rcWork.left, ah = mi.rcWork.bottom - mi.rcWork.top;
    if (ww > aw || wh > ah) {               // [win] larger than the screen: the same shape, as large as fits
        const double k = (double)aw / ww < (double)ah / wh ? (double)aw / ww : (double)ah / wh;
        ww = (int)(ww * k); wh = (int)(wh * k);
    }
    RECT cur;
    GetWindowRect(s.hwnd, &cur);
    int x = from_full ? mi.rcWork.left + (aw - ww) / 2 : cur.left, y = from_full ? mi.rcWork.top + (ah - wh) / 2 : cur.top;
    if (x + ww > mi.rcWork.right) x = mi.rcWork.right - ww;
    if (y + wh > mi.rcWork.bottom) y = mi.rcWork.bottom - wh;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    SetWindowPos(s.hwnd, nullptr, x, y, ww, wh, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

void on_window_setting(const char* key, const char*, const char*, void*)
{
    if (!_stricmp(key, "GT5_LAUNCH_WINDOW")) InterlockedExchange(&s.apply_mode, 1);
    else InterlockedExchange(&s.apply_size, 1);
}

// Any change to any setting, from the menu or from anywhere else, on whatever thread: written to
// settings.bat half a second after the last one, by the window's thread.
void on_any_setting(const char*, const char*, const char*, void*)
{
    InterlockedExchange64(&s.save_due, (LONG64)GetTickCount64() + 500);
}
void save_if_due()
{
    const LONG64 due = s.save_due;
    if (!due || (LONG64)GetTickCount64() < due) return;
    if (ui.open && ImGui::IsAnyItemActive()) return;      // not while a slider is being dragged
    InterlockedCompareExchange64(&s.save_due, 0, due);
    if (!gt5_settings_dirty()) return;
    ui.save_failed = !gt5_settings_save();
    if (!ui.save_failed) ui.saved_at = now_s();
}

// ---- a frame -----------------------------------------------------------------------------------------

// The monitor's scaling, as Windows tells the window (96 = 100 %). A window that isn't per-monitor DPI
// aware is told 96 and Windows enlarges it as a picture instead: the menu's size comes out the same.
UINT window_dpi()
{
    typedef UINT (WINAPI* get_dpi_fn)(HWND);
    static get_dpi_fn get_dpi = (get_dpi_fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    const UINT dpi = get_dpi ? get_dpi(s.hwnd) : 96;
    return dpi >= 48 ? dpi : 96;
}
// The client area, when nothing has said yet (start-up) or a message was missed.
void client_refresh()
{
    RECT rc;
    if (GetClientRect(s.hwnd, &rc) && rc.right > 0 && rc.bottom > 0) { s.client_w = rc.right; s.client_h = rc.bottom; }
}

// The body text's size in the window's pixels, which is what the player sees: 22 (Segoe UI with
// capitals about 11.5 px tall, like 16.5 px text on a web page) at 100 % scaling and the Normal menu
// size, times the monitor's scaling and the player's Menu size. Only a window too small to hold the
// menu at that size makes it smaller: it keeps room for 24 lines of text in its height (the page list,
// three rows, the help and the keys) and 28 widths of a line across. 960x540 holds 22.
float text_px()
{
    float px = 22.0f * (float)s.dpi / 96.0f * s.user_scale;
    if (s.client_w > 0 && s.client_h > 0) {
        float most = (float)s.client_h / 24.0f;
        if (most > (float)s.client_w / 28.0f) most = (float)s.client_w / 28.0f;
        if (px > most) px = most;
    }
    if (px < 9.0f) px = 9.0f;
    return floorf(px * 4.0f + 0.5f) / 4.0f;     // quarter pixels: a window being dragged doesn't make a font a frame
}

void pad_to_imgui(ImGuiIO& io, PadEdges* edges)
{
    gt5_menu_pad p;
    AcquireSRWLockShared(&s.feed_lock);
    p = s.pad;
    const ULONGLONG at = s.pad_at;
    ReleaseSRWLockShared(&s.feed_lock);
    if (!p.connected || GetTickCount64() - at > 500) { memset(&p, 0, sizeof p); p.lx = p.ly = p.rx = p.ry = 128; }
    const unsigned short b = p.buttons;
    const bool up = (b & (1 << 4)) || p.ly < 48, down = (b & (1 << 6)) || p.ly > 208;
    const bool left = (b & (1 << 7)) || p.lx < 48, right = (b & (1 << 5)) || p.lx > 208;
    if (p.connected) io.BackendFlags |= ImGuiBackendFlags_HasGamepad; else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, up);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, down);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, left);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, right);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (b & (1 << 14)) != 0);    // Cross: OK
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (b & (1 << 13)) != 0);   // Circle: back
    io.AddKeyEvent(ImGuiKey_GamepadL1, (b & (1 << 10)) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadR1, (b & (1 << 11)) != 0);
    // Square, Triangle and Start are the menu's own (Dear ImGui would open its window-moving mode on Square).
    const unsigned short pressed = (unsigned short)(b & ~s.pad_prev);
    edges->square = (pressed & (1 << 15)) != 0;
    edges->triangle = (pressed & (1 << 12)) != 0;
    edges->start = (pressed & (1 << 3)) != 0;
    edges->fast = p.l2 > 64 || p.r2 > 64;
    const int ry = (int)p.ry - 128;
    edges->ry = ry > 40 || ry < -40 ? (float)ry / 128.0f : 0.0f;
    if (pressed || up || down || left || right) ui.keyboard_last = true;
    s.pad_prev = b;
}

// [win] GT5_MENU_HINT: the first start of an install shows "F1: settings" in a corner for eight seconds,
// from three seconds after the first frame (past the boot screens' first black), and never again
// ("shown" is written to settings.bat when it appears). "0" = never; "1" = at every start (tests).
// Opening the menu ends it. Returns how opaque it is now (it fades in and out), 0 = not showing.
float hint_alpha()
{
    if (s.hint_state < 0) return 0.0f;
    const double t = now_s();
    if (s.hint_state == 0) {
        char b[16];
        gt5_setting_stored("GT5_MENU_HINT", b, sizeof b);
        s.hint_state = (!b[0] || !strcmp(b, "1")) ? 1 : -1;
        s.hint_first = t;
        if (s.hint_state < 0) return 0.0f;
    }
    if (ui.open) {                        // the player found the menu: done for good
        char b[16];
        gt5_setting_stored("GT5_MENU_HINT", b, sizeof b);
        if (strcmp(b, "1")) gt5_setting_set("GT5_MENU_HINT", "shown");
        s.hint_state = -1;
        return 0.0f;
    }
    if (ui.quit) return 0.0f;
    const double a = t - s.hint_first - 3.0, len = 8.0;
    if (a < 0.0) return 0.0f;
    if (ui.hint_until == 0.0) {
        ui.hint_until = t + len - (a > 0.0 ? a : 0.0);
        char b[16];
        gt5_setting_stored("GT5_MENU_HINT", b, sizeof b);
        if (strcmp(b, "1")) gt5_setting_set("GT5_MENU_HINT", "shown");
        fprintf(stderr, "[menu] first-start hint shown\n");
    }
    const double left = ui.hint_until - t;
    if (left <= 0.0) { s.hint_state = -1; return 0.0f; }
    float alpha = 1.0f;
    if (a < 0.4) alpha = (float)(a / 0.4);
    if (left < 0.6) alpha = (float)(left / 0.6);
    return alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
}

// Builds the frame's draw data. false: nothing to show.
bool build_frame()
{
    if (!s.inited) return false;
    refresh_settings();
    if (InterlockedExchange(&s.toggle_req, 0) && !ui.quit) set_open(!ui.open);   // [win] not while the quit question asks
    if (InterlockedExchange(&s.apply_mode, 0)) window_apply(true);
    if (InterlockedExchange(&s.apply_size, 0)) window_apply(false);
    if (s.release_wait && GetTickCount64() >= s.release_until) {
        AcquireSRWLockShared(&s.feed_lock);
        const bool idle = !s.pad.connected || s.pad.buttons == 0 || GetTickCount64() - s.pad_at > 500;
        ReleaseSRWLockShared(&s.feed_lock);
        if (idle && (keys_up() || GetTickCount64() >= s.release_until + 1500)) { InterlockedExchange(&s.release_wait, 0); update_captured(); }
    }
    save_if_due();
    const float hint = hint_alpha();      // [win] the first-start hint, 0 when not showing
    const bool notes = !ui.open && !ui.quit && s.notices_allowed && s.notices_on;   // [merge] not over the quit question
    if (!ui.open && !ui.quit && !s.osd && !notes && hint <= 0.0f) return false;

    ImGuiIO& io = ImGui::GetIO();
    // Text beyond Latin is on its way: the menu in Chinese, or the Game page's list of languages.
    if (ui.zh || (ui.open && ui.page == GT5_PAGE_GAME)) fonts_fallback();
    if (s.client_w <= 0 || s.client_h <= 0) client_refresh();
    const float px = text_px();
    if (px != s.applied_scale) {
        s.applied_scale = px;
        ui.scale = px / 22.0f;
        ui_apply_style(ui.scale);
        ui.scrolled_to[0] = 0;               // the focused row moved: bring it into view again
    }
    // Laid out in the window's pixels, drawn in the frame's: after the stretch to the window, one to one.
    const float cw = s.client_w > 0 ? (float)s.client_w : (float)s.w, ch = s.client_h > 0 ? (float)s.client_h : (float)s.h;
    io.DisplaySize = ImVec2(cw, ch);
    io.DisplayFramebufferScale = ImVec2((float)s.w / cw, (float)s.h / ch);
    s.have_frame = true;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    double dt = s.last.QuadPart ? (double)(t.QuadPart - s.last.QuadPart) / (double)s.qpf.QuadPart : 1.0 / 60.0;
    if (dt <= 0.0) dt = 1.0 / 1000.0;
    if (dt > 0.25) dt = 0.25;
    io.DeltaTime = (float)dt;
    s.last = t;
    PadEdges edges;
    pad_to_imgui(io, &edges);

    if (s.new_frame) s.new_frame();          // the renderer's backend (ImGui_ImplDX12_NewFrame, ImGui_ImplVulkan_NewFrame)
    ImGui::NewFrame();
    if (ui.quit) ui_build_quit();                    // [win] the question alone, the menu (if open) waits behind it
    else if (ui.open) ui_build(edges);
    else {
        if (s.osd) ui_build_osd();                   // top left
        if (notes) ui_build_notices();               // top right
        if (hint > 0.0f) ui_build_hint(hint);        // bottom right
    }
    ImGui::Render();

    return true;
}

}  // namespace

// ---- shared with gt5_menu_ui.cpp ----------------------------------------------------------------------

double now_s()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return s.qpf.QuadPart ? (double)t.QuadPart / (double)s.qpf.QuadPart : 0.0;
}
const char* version_text() { return s.version; }
const char* title_text() { return s.title; }
unsigned frame_w() { return s.w; }
unsigned frame_h() { return s.h; }
void need_fallback_fonts() { /* picked up before the next frame: build_frame() looks at the page and the language */ }
void note_changed() { /* every change is noticed through the registry: on_any_setting() */ }
void request_close() { InterlockedExchange(&s.toggle_req, ui.open ? 1 : 0); }

// [win] "Quit the game?": asked over the game or the menu; Yes posts GT5_MENU_QUIT_MSG to the window
// (rsx_null_backend.c ends the game cleanly), No goes back to where the player was.
void quit_ask()
{
    if (!s.inited || ui.quit) return;
    ImGuiIO& io = ImGui::GetIO();
    ui.quit = true;
    ui.quit_focus = true;
    io.ClearInputKeys();
    io.ClearInputMouse();
    ClipCursor(nullptr);
    if (GetCapture() == s.hwnd) ReleaseCapture();
    s.buttons_down = 0;
    update_captured();
    PostMessageW(s.hwnd, WM_SETCURSOR, (WPARAM)s.hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    fprintf(stderr, "[menu] quit? asked\n");
}
void quit_answer(bool yes)
{
    if (!ui.quit) return;
    ImGuiIO& io = ImGui::GetIO();
    ui.quit = false;
    io.ClearInputKeys();
    io.ClearInputMouse();
    if (GetCapture() == s.hwnd) ReleaseCapture();
    s.buttons_down = 0;
    fprintf(stderr, "[menu] quit? %s\n", yes ? "yes" : "no");
    if (yes) { PostMessageW(s.hwnd, GT5_MENU_QUIT_MSG, 0, 0); return; }
    if (!ui.open) {
        // back to the game: the key or button that said No must be up before the game reads input again
        s.release_until = GetTickCount64() + 150;
        InterlockedExchange(&s.release_wait, 1);
    } else {
        ui.in_content = false;                 // back in the menu: the focus on its page list
        ui.focus_nav = true;
    }
    update_captured();
}
bool pad_connected()
{
    AcquireSRWLockShared(&s.feed_lock);
    const bool c = s.pad.connected && GetTickCount64() - s.pad_at <= 2000;
    ReleaseSRWLockShared(&s.feed_lock);
    return c;
}

int notices_get(char out[][192], int max)
{
    int n = 0;
    const ULONGLONG now = GetTickCount64();
    bool any = false;
    AcquireSRWLockExclusive(&s.notice_lock);
    for (int i = 0; i < GT5_NOTICE_COUNT; i++) {
        State::Notice& nt = s.notice[i];
        if (!nt.on) continue;
        if (!nt.until && nt.ms) nt.until = now + nt.ms;     // its time runs from when it is first shown
        if (nt.until && now >= nt.until) { nt.on = false; continue; }
        any = true;
        if (n < max) snprintf(out[n++], 192, "%s", ui.zh && nt.zh[0] ? nt.zh : nt.en);
    }
    InterlockedExchange(&s.notices_on, any ? 1 : 0);
    ReleaseSRWLockExclusive(&s.notice_lock);
    return n;
}

bool stats_get(gt5_menu_stats* out)
{
    AcquireSRWLockShared(&s.stats_lock);
    *out = s.stats;
    const bool have = s.have_stats;
    ReleaseSRWLockShared(&s.stats_lock);
    return have;
}
bool wheel_get(gt5_menu_wheel* out)
{
    AcquireSRWLockShared(&s.feed_lock);
    *out = s.wheel;
    ReleaseSRWLockShared(&s.feed_lock);
    return out->connected != 0;
}
void bind_begin(int kind, int row, bool add)
{
    ui.bind = kind;
    ui.bind_row = row;
    ui.bind_add = add;
    ui.bind_t0 = now_s();
    wheel_get(&ui.bind_wheel0);
}
void bind_cancel() { ui.bind = BIND_NONE; }

void open_url(const char* url)
{
    wchar_t w[512];
    if (MultiByteToWideChar(CP_UTF8, 0, url, -1, w, 512)) ShellExecuteW(nullptr, L"open", w, nullptr, nullptr, SW_SHOWNORMAL);
}
void open_logs_folder()
{
    // GT5\logs, next to settings.bat (play.bat makes it).
    char path[1100];
    snprintf(path, sizeof path, "%s", gt5_settings_path());
    char* cut = strrchr(path, '\\');
    if (!cut) cut = strrchr(path, '/');
    if (cut) snprintf(cut + 1, sizeof path - (size_t)(cut + 1 - path), "logs"); else snprintf(path, sizeof path, "logs");
    wchar_t w[1100];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1100)) ShellExecuteW(nullptr, L"open", w, nullptr, nullptr, SW_SHOWNORMAL);
}

// ---- shared with the renderers' files (gt5_menu_dx12.cpp, gt5_menu_vk.cpp) ----------------------------
// A renderer starts the menu in this order: core_begin (the window, the picture's size, the texts),
// its own objects, core_context (Dear ImGui's context and the first font), its Dear ImGui backend
// (core_context_abort if that fails), core_finish. Each frame core_build_frame, then it draws Dear
// ImGui's draw data. Stopping: core_shutdown_begin, its GPU idle and its backend shut down,
// core_shutdown_end, its own objects. Everything on the window's thread.

void core_begin(HWND hwnd, unsigned w, unsigned h, const char* version, const char* title)
{
    if (!gt5_settings_path()[0]) gt5_settings_init(nullptr);
    s.hwnd = hwnd;
    s.w = w; s.h = h;
    client_refresh();                         // then WM_SIZE and WM_DPICHANGED keep these
    s.dpi = window_dpi();
    QueryPerformanceFrequency(&s.qpf);
    const char* v = version && *version ? version : getenv("GT5_VER");
    snprintf(s.version, sizeof s.version, "%s", v && *v && _stricmp(v, "none") ? v : "");
    const char* t = title && *title ? title : getenv("PS3_TITLE");
    snprintf(s.title, sizeof s.title, "%s", t && *t ? t : "Gran Turismo 5");
}

void core_context()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;                 // nothing of the menu's is written to disk but settings.bat
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigNavEscapeClearFocusItem = false;
    io.ConfigNavCursorVisibleAuto = true;
    io.BackendPlatformName = "gt5_menu";
    fonts_init();
}

void core_context_abort()
{
    ImGui::DestroyContext();
    unmap_font(&s.font_main);
    s = State();
}

void core_finish(void (*renderer_new_frame)(), const char* what)
{
    s.new_frame = renderer_new_frame;
    gt5_settings_on_change("GT5_LAUNCH_WINDOW", on_window_setting, nullptr);
    gt5_settings_on_change("PS3_WINDOW_SIZE", on_window_setting, nullptr);
    gt5_settings_on_change(nullptr, on_any_setting, nullptr);
    gt5_menu_stats_clear(&s.stats);
    s.inited = true;
    refresh_settings();
    char key[32];
    gt5_setting_str("GT5_MENU_KEY", key, sizeof key);
    fprintf(stderr, "[menu] settings menu ready: %s opens it (%s %ux%u, window %dx%d at %u dpi, settings in %s)\n", key[0] ? key : "F1",
            what, s.w, s.h, s.client_w, s.client_h, s.dpi, gt5_settings_path());
}

bool core_inited() { return s.inited; }
bool core_build_frame()
{
    // [win] The picture is still coming (gt5_menu_ask_quit). Each renderer calls this only when it has
    // something to draw the menu into (D3D12: its overlay texture; Vulkan: its overlay image).
    s.last_frame_tick = GetTickCount64();
    return build_frame();
}

void core_set_size(unsigned w, unsigned h)
{
    s.w = w; s.h = h;
    s.applied_scale = 0.0f;
}

void core_shutdown_begin()
{
    s.inited = false;
    InterlockedExchange(&g_gt5_menu_captured, 0);
    InterlockedExchange(&g_gt5_menu_pause_want, 0);
    if (gt5_settings_dirty()) gt5_settings_save();
}

void core_shutdown_end()
{
    ImGui::DestroyContext();
    unmap_font(&s.font_main); unmap_font(&s.font_sym); unmap_font(&s.font_cjk); unmap_font(&s.font_kr);
    s = State();
    ui = Ui();
}

}  // namespace gt5m

using namespace gt5m;

// ---- the API ------------------------------------------------------------------------------------------

extern "C" int gt5_menu_is_open(void) { return ui.open ? 1 : 0; }

extern "C" int gt5_menu_ask_quit(void)
{
    // [win] Only when the overlay can be seen: the menu is up and frames still come (a stopped picture
    // would hide the question, and the window would never close). [merge] Renderer-neutral: the frame
    // time is taken in core_build_frame, which each renderer reaches only with something to draw into.
    if (!s.inited) return 0;
    if (!s.last_frame_tick || GetTickCount64() - s.last_frame_tick > 1500) return 0;
    if (ui.quit) return 2;
    quit_ask();
    return 1;
}
extern "C" void gt5_menu_set_open(int open) { set_open(open != 0); }

extern "C" int gt5_menu_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
    if (!s.inited || hwnd != s.hwnd) return 0;
    ImGuiIO& io = ImGui::GetIO();
    if (result) *result = 0;
    switch (msg) {
    // What the player sees: the menu sizes itself from these (and lets them through).
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && LOWORD(lp) > 0 && HIWORD(lp) > 0) { s.client_w = LOWORD(lp); s.client_h = HIWORD(lp); }
        return 0;
    case WM_DPICHANGED:                        // the window moved to a monitor with another scaling, or it changed
        if (LOWORD(wp) >= 48) s.dpi = LOWORD(wp);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const bool repeat = (lp & (1L << 30)) != 0;
        // F11 and Alt+Enter: borderless full screen and back, as in most PC games, with the menu open or
        // closed (not while a key is being chosen for a row, and not when F11 is the menu's own key).
        const bool full_key = (msg == WM_KEYDOWN && wp == VK_F11 && s.menu_vk != VK_F11) ||
                              (msg == WM_SYSKEYDOWN && wp == VK_RETURN && (lp & (1L << 29)));
        if (full_key && ui.bind != BIND_KEY_ROW && ui.bind != BIND_MENU_KEY) {
            if (!repeat) {
                static char back[32] = "windowed";      // what to return to: a window, or a maximised one
                char mode[32];
                gt5_setting_stored("GT5_LAUNCH_WINDOW", mode, sizeof mode);
                if (!_stricmp(mode, "borderless")) gt5_setting_set("GT5_LAUNCH_WINDOW", back);
                else {
                    snprintf(back, sizeof back, "%s", mode[0] ? mode : "windowed");
                    gt5_setting_set("GT5_LAUNCH_WINDOW", "borderless");
                }
                s.swallow_up_vk = (int)wp;
            }
            return 1;
        }
        if (!ui.open && !ui.quit) {
            refresh_settings();
            if ((int)wp == s.menu_vk && !repeat) { set_open(true); s.swallow_up_vk = (int)wp; return 1; }
            return 0;
        }
        ui.keyboard_last = true;
        if (ui.quit) {                         // [win] the quit question has the keys (Esc is its No)
            if (msg == WM_SYSKEYDOWN && wp == VK_F4) return 0;        // Alt+F4 again: the window's (it quits)
            if ((int)wp == s.menu_vk) return 1;
            mods_event(io);
            const ImGuiKey k = vk_to_key(wp, lp);
            if (k != ImGuiKey_None) io.AddKeyEvent(k, true);
            return 1;
        }
        if (ui.bind == BIND_KEY_ROW || ui.bind == BIND_MENU_KEY) {
            // The next key is the one being chosen: it never reaches the menu's own navigation.
            if (!repeat) {
                s.swallow_up_vk = (int)wp;
                if (wp == VK_ESCAPE) ui_key_taken(nullptr);
                // The menu's own key and F11 (full screen) are never a game button's key.
                else if (!(ui.bind == BIND_KEY_ROW && ((int)wp == s.menu_vk || wp == VK_F11))) {
                    const char* name = gt5_kb_key_from_vk((int)wp, (long long)lp); if (name) ui_key_taken(name);
                }
            }
            return 1;
        }
        if ((int)wp == s.menu_vk) { if (!repeat) { set_open(false); s.swallow_up_vk = (int)wp; } return 1; }
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) return 0;            // Alt+F4 stays the window's
        mods_event(io);
        const ImGuiKey k = vk_to_key(wp, lp);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, true);
        return 1;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        if (s.swallow_up_vk && (int)wp == s.swallow_up_vk) { s.swallow_up_vk = 0; return 1; }
        if (!ui.open && !ui.quit) return 0;
        mods_event(io);
        const ImGuiKey k = vk_to_key(wp, lp);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, false);
        return 1;
    }
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
        return ui.open || ui.quit || s.swallow_up_vk ? 1 : 0;
    case WM_SYSCOMMAND:
        return (ui.open || ui.quit) && (wp & 0xFFF0) == SC_KEYMENU ? 1 : 0;        // Alt alone doesn't open the window menu
    case WM_MOUSEMOVE:
        if (!ui.open && !ui.quit) return 0;
        mouse_pos_event(io, lp);
        return 1;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: {
        if (!ui.open && !ui.quit) return 0;
        if ((msg == WM_XBUTTONDOWN || msg == WM_XBUTTONDBLCLK) && ui.bind == BIND_KEY_ROW) {
            ui_key_taken(GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? "Mouse4" : "Mouse5");
            if (result) *result = TRUE;
            return 1;
        }
        const int button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) ? 0 :
                           (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) ? 1 :
                           (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK) ? 2 : -1;
        if (button >= 0) {
            if (s.buttons_down++ == 0) SetCapture(hwnd);
            mouse_pos_event(io, lp);
            io.AddMouseButtonEvent(button, true);
        }
        return 1;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP: {
        if (!ui.open && !ui.quit) return 0;
        const int button = msg == WM_LBUTTONUP ? 0 : msg == WM_RBUTTONUP ? 1 : msg == WM_MBUTTONUP ? 2 : -1;
        if (button >= 0) {
            if (s.buttons_down > 0 && --s.buttons_down == 0 && GetCapture() == hwnd) ReleaseCapture();
            io.AddMouseButtonEvent(button, false);
        }
        return 1;
    }
    case WM_MOUSEWHEEL:
        if (!ui.open && !ui.quit) return 0;
        io.AddMouseWheelEvent(0.0f, (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA);
        ui.keyboard_last = false;
        return 1;
    case WM_SETCURSOR:
        // The arrow over the picture while the menu is open, whatever hid it for the game.
        if ((ui.open || ui.quit) && LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW)); if (result) *result = TRUE; return 1; }
        return 0;
    case WM_KILLFOCUS:
        if (ui.open || ui.quit) { io.AddFocusEvent(false); s.buttons_down = 0; }
        return 0;
    case WM_SETFOCUS:
        if (ui.open || ui.quit) io.AddFocusEvent(true);
        return 0;
    default:
        return 0;
    }
}

extern "C" void gt5_menu_feed_pad(const gt5_menu_pad* pad)
{
    if (!pad) return;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&s.feed_lock);
    s.pad = *pad;
    s.pad_at = now;
    // The buttons that open and close the menu, held together for half a second. The game is shut out
    // from the moment both are down, so it never sees the pair (it has seen the first of the two).
    const unsigned short mask = s.chord_mask;
    const bool down = s.inited && mask && pad->connected && (pad->buttons & mask) == mask;
    if (down) {
        if (!s.chord_t0) { s.chord_t0 = now; s.chord_fired = false; InterlockedExchange(&s.chord_hold, 1); }
        else if (!s.chord_fired && now - s.chord_t0 >= 500) { s.chord_fired = true; InterlockedExchange(&s.toggle_req, 1); }
    } else if (s.chord_t0) {
        s.chord_t0 = 0;
        InterlockedExchange(&s.chord_hold, 0);
        // let go: if the menu didn't open, the game gets the controller back once the buttons are up
        if (!ui.open) { s.release_until = now + 100; InterlockedExchange(&s.release_wait, 1); }
    }
    if (s.release_wait && !down && pad->buttons == 0 && now >= s.release_until && (keys_up() || now >= s.release_until + 1500)) InterlockedExchange(&s.release_wait, 0);
    ReleaseSRWLockExclusive(&s.feed_lock);
    update_captured();
}

extern "C" void gt5_menu_notice(int id, const char* en, const char* zh, unsigned ms)
{
    if (id < 0 || id >= GT5_NOTICE_COUNT) return;
    AcquireSRWLockExclusive(&s.notice_lock);
    State::Notice& nt = s.notice[id];
    if (!en || !*en) nt.on = false;
    else {
        snprintf(nt.en, sizeof nt.en, "%s", en);
        snprintf(nt.zh, sizeof nt.zh, "%s", zh ? zh : "");
        nt.ms = ms;
        nt.until = 0;
        nt.on = true;
    }
    bool any = false;
    for (int i = 0; i < GT5_NOTICE_COUNT; i++) any = any || s.notice[i].on;
    InterlockedExchange(&s.notices_on, any ? 1 : 0);
    ReleaseSRWLockExclusive(&s.notice_lock);
}

extern "C" void gt5_menu_feed_wheel(const gt5_menu_wheel* wheel)
{
    if (!wheel) return;
    AcquireSRWLockExclusive(&s.feed_lock);
    s.wheel = *wheel;
    s.wheel.name[sizeof s.wheel.name - 1] = 0;
    ReleaseSRWLockExclusive(&s.feed_lock);
}

extern "C" void gt5_menu_stats_clear(gt5_menu_stats* st)
{
    if (!st) return;
    memset(st, 0, sizeof *st);
    st->game_fps = st->shown_fps = st->drawn_fps = st->made_fps = st->cpu_cores = st->gpu_ms = -1.0f;
    st->draws = st->frame_w = st->frame_h = st->display_hz = -1;
}

extern "C" void gt5_menu_set_stats(const gt5_menu_stats* st)
{
    if (!st) return;
    AcquireSRWLockExclusive(&s.stats_lock);
    s.stats = *st;
    s.stats.gpu_name[sizeof s.stats.gpu_name - 1] = 0;
    s.have_stats = true;
    ReleaseSRWLockExclusive(&s.stats_lock);
}

extern "C" void gt5_menu_test_goto(int page, int sub)
{
    if (page >= 0 && page < GT5_PAGE_COUNT) ui.page = page;
    ui.sub = sub;
    ui.bind = BIND_NONE;
    ui.in_content = true;
    ui.focus_content = true;
    ui.focus_nav = false;
    if (sub == SUB_KEYS) { char b[1200]; gt5_setting_stored("PS3_KEYS", b, sizeof b); gt5_kb_parse(b, &ui.keys); }
    if (sub == SUB_WHEEL) {
        char b[600], p[200];
        gt5_setting_stored("PS3_WHEEL_BUTTONS", b, sizeof b);
        gt5_setting_stored("PS3_WHEEL_PEDALS", p, sizeof p);
        gt5_wheel_parse(b, p, &ui.wheel);
    }
}
extern "C" int gt5_menu_test_bind_row(int row)
{
    if (ui.sub == SUB_KEYS) { bind_begin(BIND_KEY_ROW, row, false); return 1; }
    if (ui.sub == SUB_WHEEL) { bind_begin(row >= GT5_WHEEL_BUTTONS ? BIND_WHEEL_PEDAL : BIND_WHEEL_BUTTON, row >= GT5_WHEEL_BUTTONS ? row - GT5_WHEEL_BUTTONS : row, false); return 1; }
    return 0;
}
extern "C" int gt5_menu_test_page(void) { return ui.page; }
extern "C" int gt5_menu_test_focus(const char* key)
{
    const gt5_setting_def* d = gt5_settings_find(key);
    if (!d || d->page < 0) return 0;
    ui.page = d->page;
    ui.sub = SUB_NONE;
    ui.in_content = true;
    snprintf(ui.focus_key, sizeof ui.focus_key, "%s", d->key);
    return 1;
}
extern "C" int gt5_menu_test_info_get(gt5_menu_test_info* out)
{
    if (!out || !s.inited || !s.have_frame) return 0;
    memset(out, 0, sizeof *out);
    out->client_w = s.client_w; out->client_h = s.client_h; out->dpi = (int)s.dpi;
    out->text_px = s.applied_scale;
    out->cap_px = ui.tv.cap_px;
    out->small_px = ui.tv.small_px;
    out->density = ui.tv.density;
    memcpy(out->panel, ui.tv.panel, sizeof out->panel);
    memcpy(out->page, ui.tv.page, sizeof out->page);
    memcpy(out->focus, ui.tv.focus, sizeof out->focus);
    memcpy(out->nav, ui.tv.nav, sizeof out->nav);
    out->stacked = ui.tv.stacked ? 1 : 0;
    out->help_cut = ui.tv.help_cut ? 1 : 0;
    out->help_inline = ui.tv.help_inline ? 1 : 0;
    snprintf(out->focus_key, sizeof out->focus_key, "%s", ui.tv.focus_key);
    return 1;
}
