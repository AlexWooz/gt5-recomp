/*
 * gt5_binds.c - PS3_KEYS, PS3_WHEEL_BUTTONS and PS3_WHEEL_PEDALS as text (see gt5_binds.h).
 *
 * The keyboard half follows the launcher's KeyBinds class (GT5Launcher.cs) and the names
 * cellPad.c's kb_vk takes; the wheel half follows wheel_host_win.c's build_map. UTF-8.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt5_binds.h"

static int ieq(const char* a, const char* b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

/* ---- keyboard ---------------------------------------------------------------------------------------- */

static const struct {
    const char* name;      /* PS3_KEYS' spelling */
    const char* def;       /* the default key, "" = none */
    const char* action_en; const char* action_zh;   /* (launcher) kb.a.* */
    const char* button_en; const char* button_zh;   /* (launcher) kb.b.* */
} k_kb[GT5_KB_BUTTONS] = {
    { "cross",    "Z",     "Accelerate, OK", "加速、确定", "Cross", "✕" },
    { "square",   "A",     "Brake", "刹车", "Square", "□" },
    { "r2",       "2",     "Shift up", "升挡", "R2", "R2" },
    { "l2",       "1",     "Shift down", "降挡", "L2", "L2" },
    { "circle",   "X",     "Handbrake, Back", "手刹、返回", "Circle", "○" },
    { "triangle", "S",     "Reverse", "倒车", "Triangle", "△" },
    { "l1",       "Q",     "Look back", "后视", "L1", "L1" },
    { "r1",       "W",     "Headlights: flash, high/low beam", "大灯：闪灯、远/近光", "R1", "R1" },
    { "l3",       "",      "Horn", "喇叭", "L3 (left stick press)", "L3（按下左摇杆）" },
    { "r3",       "",      "Windscreen wipers", "雨刷", "R3 (right stick press)", "R3（按下右摇杆）" },
    { "select",   "Tab",   "Change view (camera)", "切换视角", "Select", "Select" },
    { "start",    "Enter", "Pause", "暂停", "Start", "Start" },
    { "up",       "Up",    "Ghost, menu up", "幽灵车、菜单上", "D-pad up", "十字键上" },
    { "down",     "Down",  "Menu down", "菜单下", "D-pad down", "十字键下" },
    { "left",     "Left",  "Steer left, menu left", "左转、菜单左", "D-pad left", "十字键左" },
    { "right",    "Right", "Steer right, menu right", "右转、菜单右", "D-pad right", "十字键右" },
    { "lstick_up",    "", "(nothing by default)", "（默认无）", "Left stick up", "左摇杆上" },
    { "lstick_down",  "", "(nothing by default)", "（默认无）", "Left stick down", "左摇杆下" },
    { "lstick_left",  "", "Steer left", "左转", "Left stick left", "左摇杆左" },
    { "lstick_right", "", "Steer right", "右转", "Left stick right", "左摇杆右" },
    { "rstick_up",    "", "Accelerate", "加速", "Right stick up", "右摇杆上" },
    { "rstick_down",  "", "Brake", "刹车", "Right stick down", "右摇杆下" },
    { "rstick_left",  "", "(nothing by default: Left View?)", "（默认无：左视角？）", "Right stick left", "右摇杆左" },
    { "rstick_right", "", "(nothing by default: Right View?)", "（默认无：右视角？）", "Right stick right", "右摇杆右" },
};

/* Windows virtual-key code -> the name the game takes. Letters, digits, F-keys and Num0-9 are made. */
static const struct { int vk; const char* name; } k_vk[] = {
    { 0x20, "Space" }, { 0x0D, "Enter" }, { 0x09, "Tab" }, { 0x08, "Backspace" }, { 0x26, "Up" }, { 0x28, "Down" },
    { 0x25, "Left" }, { 0x27, "Right" }, { 0x2D, "Insert" }, { 0x2E, "Delete" }, { 0x24, "Home" }, { 0x23, "End" },
    { 0x21, "PageUp" }, { 0x22, "PageDown" }, { 0x14, "CapsLock" }, { 0x6B, "NumPlus" }, { 0x6D, "NumMinus" },
    { 0x6A, "NumMultiply" }, { 0x6F, "NumDivide" }, { 0x6E, "NumDecimal" }, { 0xBC, "Comma" }, { 0xBE, "Period" },
    { 0xBF, "Slash" }, { 0xBA, "Semicolon" }, { 0xDE, "Apostrophe" }, { 0xDB, "LBracket" }, { 0xDD, "RBracket" },
    { 0xDC, "Backslash" }, { 0xBD, "Minus" }, { 0xBB, "Equals" }, { 0xC0, "Grave" }, { 0xE2, "IntlBackslash" },
    { 0xA0, "LShift" }, { 0xA1, "RShift" }, { 0xA2, "LCtrl" }, { 0xA3, "RCtrl" }, { 0xA4, "LAlt" }, { 0xA5, "RAlt" },
    { 0x05, "Mouse4" }, { 0x06, "Mouse5" },
    { 0x10, "Shift" }, { 0x11, "Ctrl" }, { 0x12, "Alt" },   /* names only: a key press always says which side */
};
#define N_VK ((int)(sizeof k_vk / sizeof k_vk[0]))

const char* gt5_kb_button(int b) { return b >= 0 && b < GT5_KB_BUTTONS ? k_kb[b].name : ""; }
const char* gt5_kb_action_label(int b, int zh)
{
    if (b < 0 || b >= GT5_KB_BUTTONS) return "";
    return zh ? k_kb[b].action_zh : k_kb[b].action_en;
}
const char* gt5_kb_button_label(int b, int zh)
{
    if (b < 0 || b >= GT5_KB_BUTTONS) return "";
    return zh ? k_kb[b].button_zh : k_kb[b].button_en;
}

/* Made names live here so a returned pointer stays good. */
static const char* made_name(int kind, int n)
{
    static char letters[26][2], digits[10][2], nums[10][5], fkeys[24][4];
    static int ready;
    if (!ready) {
        for (int i = 0; i < 26; i++) { letters[i][0] = (char)('A' + i); letters[i][1] = 0; }
        for (int i = 0; i < 10; i++) { digits[i][0] = (char)('0' + i); digits[i][1] = 0; snprintf(nums[i], sizeof nums[i], "Num%d", i); }
        for (int i = 0; i < 24; i++) snprintf(fkeys[i], sizeof fkeys[i], "F%d", i + 1);
        ready = 1;
    }
    switch (kind) {
    case 0: return letters[n];
    case 1: return digits[n];
    case 2: return nums[n];
    default: return fkeys[n];
    }
}

const char* gt5_kb_canon(const char* name)
{
    if (!name || !*name) return NULL;
    if (!name[1]) {
        const int c = toupper((unsigned char)name[0]);
        if (c >= 'A' && c <= 'Z') return made_name(0, c - 'A');
        if (c >= '0' && c <= '9') return made_name(1, c - '0');
    }
    if ((name[0] == 'N' || name[0] == 'n') && (name[1] == 'U' || name[1] == 'u') && (name[2] == 'M' || name[2] == 'm') &&
        name[3] >= '0' && name[3] <= '9' && !name[4])
        return made_name(2, name[3] - '0');
    if ((name[0] == 'F' || name[0] == 'f') && isdigit((unsigned char)name[1])) {
        char* end;
        const long n = strtol(name + 1, &end, 10);
        if (!*end && n >= 1 && n <= 24) return made_name(3, (int)n - 1);
    }
    for (int i = 0; i < N_VK; i++) if (ieq(k_vk[i].name, name)) return k_vk[i].name;
    return NULL;
}

int gt5_kb_vk(const char* name)
{
    const char* c = gt5_kb_canon(name);
    if (!c) return 0;
    if (!c[1]) return c[0];                                        /* A-Z, 0-9 are their own codes */
    if (c[0] == 'N' && c[1] == 'u' && c[2] == 'm' && isdigit((unsigned char)c[3]) && !c[4]) return 0x60 + (c[3] - '0');
    if (c[0] == 'F' && isdigit((unsigned char)c[1])) return 0x70 + atoi(c + 1) - 1;
    for (int i = 0; i < N_VK; i++) if (k_vk[i].name == c) return k_vk[i].vk;
    return 0;
}

const char* gt5_kb_key_from_vk(int vk, long long lparam)
{
    const int ext = (lparam & 0x01000000) != 0;
    const int scan = (int)((lparam >> 16) & 0xFF);
    if (vk == 0x10) vk = scan == 0x36 ? 0xA1 : 0xA0;   /* Shift: the right one has its own scan code */
    if (vk == 0x11) vk = ext ? 0xA3 : 0xA2;
    if (vk == 0x12) vk = ext ? 0xA5 : 0xA4;
    if (vk >= 'A' && vk <= 'Z') return made_name(0, vk - 'A');
    if (vk >= '0' && vk <= '9') return made_name(1, vk - '0');
    if (vk >= 0x70 && vk <= 0x87) return made_name(3, vk - 0x70);
    if (vk >= 0x60 && vk <= 0x69) return made_name(2, vk - 0x60);
    for (int i = 0; i < N_VK; i++) if (k_vk[i].vk == vk) return k_vk[i].name;
    return NULL;
}

const char* gt5_kb_pretty(const char* k)
{
    if (!k) return "";
    if (!strcmp(k, "LAlt")) return "Left Alt";
    if (!strcmp(k, "RAlt")) return "Right Alt";
    if (!strcmp(k, "LCtrl")) return "Left Ctrl";
    if (!strcmp(k, "RCtrl")) return "Right Ctrl";
    if (!strcmp(k, "LShift")) return "Left Shift";
    if (!strcmp(k, "RShift")) return "Right Shift";
    return k;
}

int gt5_kb_count(const gt5_keymap* m, int b)
{
    int n = 0;
    while (n < GT5_KB_KEYS && m->key[b][n][0]) n++;
    return n;
}
static int kb_has(const gt5_keymap* m, int b, const char* key)
{
    for (int k = 0; k < GT5_KB_KEYS; k++) if (m->key[b][k][0] && !strcmp(m->key[b][k], key)) return 1;
    return 0;
}
static void kb_remove(gt5_keymap* m, int b, const char* key)
{
    for (int k = 0; k < GT5_KB_KEYS; k++) {
        if (!m->key[b][k][0] || strcmp(m->key[b][k], key)) continue;
        for (int j = k; j < GT5_KB_KEYS - 1; j++) memcpy(m->key[b][j], m->key[b][j + 1], sizeof m->key[b][j]);
        m->key[b][GT5_KB_KEYS - 1][0] = 0;
        k--;
    }
}
static void kb_add(gt5_keymap* m, int b, const char* key)
{
    const int n = gt5_kb_count(m, b);
    if (n < GT5_KB_KEYS && !kb_has(m, b, key)) snprintf(m->key[b][n], sizeof m->key[b][n], "%s", key);
}

void gt5_kb_clear(gt5_keymap* m, int b) { if (b >= 0 && b < GT5_KB_BUTTONS) memset(m->key[b], 0, sizeof m->key[b]); }

void gt5_kb_defaults(gt5_keymap* m)
{
    memset(m, 0, sizeof *m);
    for (int b = 0; b < GT5_KB_BUTTONS; b++) if (k_kb[b].def[0]) kb_add(m, b, k_kb[b].def);
}

void gt5_kb_parse(const char* s, gt5_keymap* m)
{
    gt5_keymap bound;
    int is_bound[GT5_KB_BUTTONS];
    memset(&bound, 0, sizeof bound);
    memset(is_bound, 0, sizeof is_bound);
    gt5_kb_defaults(m);
    char buf[2048];
    snprintf(buf, sizeof buf, "%s", s ? s : "");
    for (char* item = buf; item && *item; ) {
        char* next = strchr(item, ',');
        if (next) *next++ = 0;
        char* colon = strchr(item, ':');
        if (colon) {
            *colon = 0;
            char* bn = item;
            while (*bn == ' ' || *bn == '\t') bn++;
            { char* e = bn + strlen(bn); while (e > bn && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0; }
            int b = -1;
            for (int i = 0; i < GT5_KB_BUTTONS; i++) if (ieq(bn, k_kb[i].name)) b = i;
            if (b >= 0) {
                is_bound[b] = 1;
                for (char* k = colon + 1; k; ) {
                    char* slash = strchr(k, '/');
                    if (slash) *slash++ = 0;
                    while (*k == ' ' || *k == '\t') k++;
                    { char* e = k + strlen(k); while (e > k && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0; }
                    const char* c = gt5_kb_canon(k);
                    if (c) kb_add(&bound, b, c);
                    k = slash;
                }
            }
        }
        item = next;
    }
    for (int b = 0; b < GT5_KB_BUTTONS; b++) if (is_bound[b]) memcpy(m->key[b], bound.key[b], sizeof m->key[b]);
    /* A default key that a binding took leaves its old button. */
    for (int b = 0; b < GT5_KB_BUTTONS; b++) {
        if (is_bound[b]) continue;
        for (int o = 0; o < GT5_KB_BUTTONS; o++) {
            if (!is_bound[o]) continue;
            for (int k = 0; k < GT5_KB_KEYS; k++) if (bound.key[o][k][0]) kb_remove(m, b, bound.key[o][k]);
        }
    }
}

size_t gt5_kb_format(const gt5_keymap* m, char* buf, size_t cap)
{
    size_t n = 0;
    if (cap) buf[0] = 0;
    for (int b = 0; b < GT5_KB_BUTTONS; b++) {
        const int cur = gt5_kb_count(m, b);
        const int def = k_kb[b].def[0] ? 1 : 0;
        const int same = cur == def && (!def || !strcmp(m->key[b][0], k_kb[b].def));
        if (same) continue;
        char part[128];
        size_t pn = (size_t)snprintf(part, sizeof part, "%s%s:", n ? "," : "", k_kb[b].name);
        if (!cur) pn += (size_t)snprintf(part + pn, sizeof part - pn, "none");
        for (int k = 0; k < cur; k++) pn += (size_t)snprintf(part + pn, sizeof part - pn, "%s%s", k ? "/" : "", m->key[b][k]);
        if (n + pn + 1 > cap) break;
        memcpy(buf + n, part, pn + 1);
        n += pn;
    }
    return n;
}

void gt5_kb_bind(gt5_keymap* m, int b, const char* key, int add)
{
    const char* c = gt5_kb_canon(key);
    if (!c || b < 0 || b >= GT5_KB_BUTTONS) return;
    for (int o = 0; o < GT5_KB_BUTTONS; o++) if (o != b) kb_remove(m, o, c);   /* a key used elsewhere moves here */
    if (!add) gt5_kb_clear(m, b);
    kb_add(m, b, c);
}

/* ---- wheel ------------------------------------------------------------------------------------------- */

static const struct { const char* name; const char* en; const char* zh; } k_wheel[GT5_WHEEL_BUTTONS] = {
    { "cross", "Cross", "✕" }, { "square", "Square", "□" }, { "circle", "Circle", "○" }, { "triangle", "Triangle", "△" },
    { "r1", "R1 (right paddle: shift up)", "R1（右拨片：升挡）" }, { "l1", "L1 (left paddle: shift down)", "L1（左拨片：降挡）" },
    { "r2", "R2", "R2" }, { "l2", "L2", "L2" },
    { "select", "Select", "Select" }, { "start", "Start", "Start" }, { "r3", "R3", "R3" }, { "l3", "L3", "L3" },
    { "gear1", "Shifter: 1st gear", "排挡杆：1 挡" }, { "gear2", "Shifter: 2nd gear", "排挡杆：2 挡" },
    { "gear3", "Shifter: 3rd gear", "排挡杆：3 挡" }, { "gear4", "Shifter: 4th gear", "排挡杆：4 挡" },
    { "gear5", "Shifter: 5th gear", "排挡杆：5 挡" }, { "gear6", "Shifter: 6th gear", "排挡杆：6 挡" },
    { "gearr", "Shifter: reverse", "排挡杆：倒挡" },
    { "plus", "+ button", "+ 按钮" }, { "minus", "- button", "- 按钮" },
    { "dialcw", "Dial, clockwise", "旋钮顺时针" }, { "dialccw", "Dial, anticlockwise", "旋钮逆时针" },
    { "up", "D-pad up", "十字键上" }, { "right", "D-pad right", "十字键右" },
    { "down", "D-pad down", "十字键下" }, { "left", "D-pad left", "十字键左" },
    { "throttle", "Throttle on a button", "用按钮代替油门" }, { "brake", "Brake on a button", "用按钮代替刹车" },
    { "clutch", "Clutch on a button", "用按钮代替离合" },
};
static const char* const k_axis[GT5_WHEEL_AXES] = { "X", "Y", "Z", "RX", "RY", "RZ", "S0", "S1" };
static const struct { const char* name; const char* en; const char* zh; } k_pedal[3] = {
    { "throttle", "Throttle", "油门" }, { "brake", "Brake", "刹车" }, { "clutch", "Clutch", "离合" },
};

const char* gt5_wheel_button(int i) { return i >= 0 && i < GT5_WHEEL_BUTTONS ? k_wheel[i].name : ""; }
const char* gt5_wheel_button_label(int i, int zh)
{
    if (i < 0 || i >= GT5_WHEEL_BUTTONS) return "";
    return zh ? k_wheel[i].zh : k_wheel[i].en;
}
const char* gt5_wheel_axis(int a) { return a >= 0 && a < GT5_WHEEL_AXES ? k_axis[a] : "none"; }
const char* gt5_wheel_pedal_label(int p, int zh)
{
    if (p < 0 || p >= 3) return "";
    return zh ? k_pedal[p].zh : k_pedal[p].en;
}

void gt5_wheel_parse(const char* buttons, const char* pedals, gt5_wheelmap* m)
{
    for (int i = 0; i < GT5_WHEEL_BUTTONS; i++) m->btn[i] = -1;
    for (int p = 0; p < 3; p++) { m->pedal_axis[p] = -2; m->pedal_inv[p] = 0; }
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", buttons ? buttons : "");
    for (char* c = buf; *c; c++) *c = (char)tolower((unsigned char)*c);
    for (char* tok = strtok(buf, ",; "); tok; tok = strtok(NULL, ",; ")) {
        char* eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        for (int i = 0; i < GT5_WHEEL_BUTTONS; i++)
            if (!strcmp(tok, k_wheel[i].name)) { const int n = atoi(eq + 1); m->btn[i] = n > 0 ? n : 0; }
    }
    snprintf(buf, sizeof buf, "%s", pedals ? pedals : "");
    for (char* c = buf; *c; c++) *c = (char)tolower((unsigned char)*c);
    for (int p = 0; p < 3; p++) {
        const char* at = strstr(buf, k_pedal[p].name);
        const size_t len = strlen(k_pedal[p].name);
        if (!at || at[len] != '=') continue;
        char n[16];
        int i = 0;
        for (const char* s = at + len + 1; s[i] && s[i] != ',' && i < 15; i++) n[i] = (char)toupper((unsigned char)s[i]);
        n[i] = 0;
        int inv = 0;
        char* q = strstr(n, "INV");
        if (q) { *q = 0; inv = 1; }
        int a = -2;
        for (int x = 0; x < GT5_WHEEL_AXES; x++) if (!strcmp(n, k_axis[x])) a = x;
        if (!strcmp(n, "SLIDER") || !strcmp(n, "SLIDER0")) a = 6;
        if (!strcmp(n, "NONE")) a = -1;
        if (a == -2) continue;
        m->pedal_axis[p] = a;
        m->pedal_inv[p] = inv;
    }
}

size_t gt5_wheel_format_buttons(const gt5_wheelmap* m, char* buf, size_t cap)
{
    size_t n = 0;
    if (cap) buf[0] = 0;
    for (int i = 0; i < GT5_WHEEL_BUTTONS; i++) {
        if (m->btn[i] < 0) continue;
        char part[48];
        const size_t pn = (size_t)snprintf(part, sizeof part, "%s%s=%d", n ? "," : "", k_wheel[i].name, m->btn[i]);
        if (n + pn + 1 > cap) break;
        memcpy(buf + n, part, pn + 1);
        n += pn;
    }
    return n;
}

size_t gt5_wheel_format_pedals(const gt5_wheelmap* m, char* buf, size_t cap)
{
    size_t n = 0;
    if (cap) buf[0] = 0;
    for (int p = 0; p < 3; p++) {
        if (m->pedal_axis[p] == -2) continue;
        char part[48];
        const size_t pn = (size_t)snprintf(part, sizeof part, "%s%s=%s%s", n ? "," : "", k_pedal[p].name,
                                           gt5_wheel_axis(m->pedal_axis[p]),
                                           m->pedal_axis[p] >= 0 && m->pedal_inv[p] ? "inv" : "");
        if (n + pn + 1 > cap) break;
        memcpy(buf + n, part, pn + 1);
        n += pn;
    }
    return n;
}
