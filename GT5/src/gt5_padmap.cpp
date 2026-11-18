// The controller's driving buttons: which buttons GT5 accelerates, brakes and shifts with, and when it is
// driving. cellPad.c (PAD_TRIGGERS) uses both: while GT5 drives, a controller's right trigger feeds the
// pressure of the button GT5 accelerates with and the left trigger that of the brake button, and the
// bumpers shift; at every other moment the controller is as on a PS3 pad.
//
// GT5 turns a controller into car controls in GTController::InputPort, the third function of its vtable:
//     update(this, mode, input, out)
//     this+4   the button assignment in use (the game's Options > Controller settings edit it)
//     mode     GT5's InputMode: 1 driving, 2 a replay, 3 the 0-400 m drag mode
//     input+2  the controller's type: 1 + the pad type (7: the DUALSHOCK 3 / SIXAXIS the port reports),
//              8 up a wheel, 0x12 up a pad and a wheel on one port (pad class = (type - 0x12) % 7 + 1)
//     out      what the controls came to: +0 flags (2 throttle, 4 brake), +8 throttle and +0xA brake,
//              each 0-4096
// The game reads a controller for a car through it, each player's port every frame (mode 2 in a
// replay); its menus read the controller elsewhere (its UI's own button events). So "update() ran in
// a driving mode a moment ago" is what tells driving from the menus here. (Read from the code. GT5's
// pause screen doesn't stop it, D54: see the race's pause below.)
//
// The assignment, all big-endian; a vector is {-, begin, end, capacity}:
//     {vector of modes}         mode:  {u16 InputMode, -, vector of classes}                     0x14 bytes
//     class: {u32 class (the controller type, for a pad), vector of analog pairs, vector of button pairs}
//                                                                                              0x24 bytes
//     pair:  {u8 action, u8 channel}
// Analog pairs (GT5's AnalogCode): 4 accelerate, 5 brake, 9 shift up, 10 shift down ...; their channels
// (SuperPortAnalogChannel) 0x18-0x23 are the pressure of up, down, left, right, circle, cross, triangle,
// square, L1, R1, L2, R2 (0-0xF the sticks' halves). Button pairs (ButtonCode): 3 accelerate, 5 brake,
// 9 shift up, 10 shift down ...; their channels (SuperPortButtonBit) 1-4 up / down / left / right,
// 7 start, 8 select, 9-12 circle / cross / triangle / square, 0x17-0x1C L1 / R1 / L2 / R2 / L3 / R3.
// GT5's own defaults for a pad when driving: throttle on the pressure of Cross and on the right stick
// up, brake on Square and the right stick down, shift up on R2, down on L2, look back L1, lights R1,
// horn L3, wipers R3, change view Select, pause Start, ghost d-pad up. (All read from GT5's executable,
// Europe 1.00: the names are its own, the numbers its tables'; GTController::DefaultConfig's table at
// 0x1794250 holds the defaults in this form.)
//
// GT5 reads the throttle on a face button, the d-pad, L1 or R1 as 4 x its pressure (full at 64 of 255)
// and the brake as 200/65 x (full at 83), the steps a DUALSHOCK 3's buttons cover under a firm press;
// L2 and R2 of a SIXAXIS / DUALSHOCK 3 it reads straight (0-255), as they are real triggers. A trigger
// feeds that range, so the throttle follows the trigger's whole travel. The two scale codes are looked
// for in the executable; without them a trigger feeds 0-255 (full throttle at a quarter of its travel).
//
// PAD_DRIVE_HOOK=0: not hooked (the triggers are then as on a PS3 pad everywhere). PAD_TRACE=2 adds
// what GT5 read, at each change: "[pad] t=... the game reads throttle N%, brake N%".
//
// The race's pause. GT5's pause screen (Start in a race: the bar with Continue, the race clock stopped)
// does not stop update(): the race reads the controller on, for the button that ends the pause (D54).
// What stops is the race engine, and its own getter (Europe 1.00 0x3FFD70, which the race's systems ask
// before they move anything) works it out as
//     paused = byte[X+U] || byte[X+S] || ((Y = word[X+P]) && (s32)word[Y+C] > 0) || h(X)
// X the race engine; U the pause the race's script asks for (GTFramework::mRaceOperator's pauseRace sets
// a request word the engine copies into U each tick, 0x4067F8), S a second request of the same kind, Y a
// pause counter, h a function that answers 0 in every release here. Europe 1.00 and Asia 1.00: U 0x9A9,
// S 0x9AA, P 0xA1C, C 0x14; Asia 2.01 and Europe 2.17: 0x9A5, 0x9A6, 0xA18, 0x14. The getter is found by
// its instructions and the numbers read from them. X comes from GTRace::DirectRaceStatus, the live race's
// status object ({vtable, X}, made on the fly and handed to whatever asks about the race): its functions
// that read this+4 are wrapped to note X when they are called. Read only: nothing in the game changes.
// The state goes to cellPad (cellPad_race_pause) with every update(): the triggers are as on a PS3 pad on
// the pause screen, and the in-game menu's pause (cellPad.c, GT5_MENU_PAUSE) knows paused from running.
// PAD_DRIVE_DUMP=1: a line a second with what all this reads ("[pad-dump] ...").
#include <array>
#include <utility>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu_context.h"

extern "C" unsigned char* vm_base;
extern "C" void ps3_on_loaded(void (*fn)(void));
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
// cellPad.c
extern "C" void cellPad_drive_frame(int play, unsigned accel, unsigned brake, unsigned up, unsigned down,
                                    unsigned accel_full, unsigned brake_full, unsigned jobs);
extern "C" unsigned long long cellPad_now_ms(void);
extern "C" void cellPad_race_pause(int state);   // 1 paused, 0 running, -1 not known

// The lifted function table (generated/ppu_recomp_table.cpp): guest address -> lifted function.
struct gt5_func_entry {
    uint64_t addr;
    void (*func)(ppu_context*);
    const char* name;
};
extern "C" const gt5_func_entry function_table[];
extern "C" const uint64_t function_table_count;

namespace {

// Where the executable and its data sit in guest memory: every GT5 release fits well inside this.
const uint32_t kImageLo = 0x00010000;
const uint32_t kImageHi = 0x02000000;

// CELL_PAD_CTRL_* (cellPad.h)
enum : uint16_t {
    SELECT = 1 << 0, L3 = 1 << 1, R3 = 1 << 2, START = 1 << 3, UP = 1 << 4, RIGHT = 1 << 5, DOWN = 1 << 6,
    LEFT = 1 << 7, L2 = 1 << 8, R2 = 1 << 9, L1 = 1 << 10, R1 = 1 << 11, TRIANGLE = 1 << 12, CIRCLE = 1 << 13,
    CROSS = 1 << 14, SQUARE = 1 << 15
};

// GT5's scaling of the throttle (x 4) and the brake (x 200 / 65) pressure, Europe 1.00 0x52F43C / 0x52F460.
const uint32_t kAccelScale[] = {0x54600DFC, 0x3D2051EB, 0x1C000064, 0x6129851F, 0x7C004896, 0x5403E13E};
const uint32_t kBrakeScale[] = {0x54600DFC, 0x3D207E07, 0x1C000064, 0x6129E07F, 0x7C004896, 0x5403D97E};

void (*s_update)(ppu_context*);
uint32_t s_opd;           // InputPort::update's function descriptor
uint32_t s_code;          // the game's update
unsigned s_accel_full = 255, s_brake_full = 255;
int s_trace;

uint32_t rd32(uint32_t ea)
{
    uint32_t v;
    memcpy(&v, vm_base + ea, 4);
    return __builtin_bswap32(v);
}

uint16_t rd16(uint32_t ea)
{
    uint16_t v;
    memcpy(&v, vm_base + ea, 2);
    return __builtin_bswap16(v);
}

uint8_t rd8(uint32_t ea) { return vm_base[ea]; }

void wr32(uint32_t ea, uint32_t v)
{
    v = __builtin_bswap32(v);
    memcpy(vm_base + ea, &v, 4);
}

// An address the game's heap or image could hold (the guest's user memory), with len bytes after it.
bool guest_ok(uint32_t ea, uint32_t len)
{
    return ea >= kImageLo && ea + len >= ea && ea + len <= 0xF0000000u;
}

// The first aligned word at or after `from` holding `value`, or 0.
uint32_t find_word(uint32_t value, uint32_t from)
{
    const uint32_t be = __builtin_bswap32(value);
    for (uint32_t ea = (from + 3) & ~3u; ea < kImageHi; ea += 4) {
        uint32_t w;
        memcpy(&w, vm_base + ea, 4);
        if (w == be) return ea;
    }
    return 0;
}

bool has_code(const uint32_t* words, int n)
{
    for (uint32_t ea = find_word(words[0], kImageLo); ea; ea = find_word(words[0], ea + 4)) {
        int i = 1;
        while (i < n && rd32(ea + 4 * i) == words[i]) i++;
        if (i == n) return true;
    }
    return false;
}

// A class's vtable by its C++ type name, or 0. The type information object is {its own vtable, name,
// ...}; the class's vtable {0, type information, virtual functions...}.
uint32_t find_vtable(const char* name)
{
    const size_t len = strlen(name) + 1;   // with its terminating zero
    const unsigned char* lo = vm_base + kImageLo;
    const unsigned char* hi = vm_base + kImageHi;
    for (const unsigned char* p = lo; p + len <= hi; p++) {
        p = (const unsigned char*)memchr(p, name[0], (size_t)(hi - p));
        if (!p || p + len > hi) break;
        if (memcmp(p, name, len) != 0 || p[-1] != 0) continue;
        const uint32_t name_ea = (uint32_t)(p - vm_base);
        for (uint32_t h = find_word(name_ea, kImageLo); h; h = find_word(name_ea, h + 4)) {
            const uint32_t ti = h - 4;
            if (!rd32(ti)) continue;
            for (uint32_t v = find_word(ti, kImageLo); v; v = find_word(ti, v + 4)) {
                const uint32_t vt = v - 4;
                if (rd32(vt) != 0) continue;
                const uint32_t opd = rd32(vt + 8);
                if (opd >= kImageLo && opd < kImageHi && (opd & 3) == 0) return vt;
            }
        }
    }
    return 0;
}

// InputPort's vtable entry for update() (its third function): the function descriptor's address, or 0.
uint32_t find_update_opd()
{
    const uint32_t vt = find_vtable("N12GTController9InputPortE");
    if (!vt) return 0;
    const uint32_t opd = rd32(vt + 8 + 2 * 4);
    return opd >= kImageLo && opd < kImageHi && (opd & 3) == 0 ? opd : 0;
}

void (*lifted(uint32_t code))(ppu_context*)
{
    for (uint64_t i = 0; i < function_table_count; i++)
        if (function_table[i].addr == code) return function_table[i].func;
    return nullptr;
}

// update() as GT5 compiles it: a stack frame, this->assignment read (lwz rN,4(rM)) and the next vtable
// function called (lwz r11,0xC(r9) ... bctrl), all near the top.
bool looks_like_update(uint32_t code)
{
    if (code < kImageLo || code >= kImageHi || (code & 3)) return false;
    bool frame = false, field4 = false, slot3 = false, call = false;
    for (uint32_t ea = code; ea < code + 0x140; ea += 4) {
        const uint32_t w = rd32(ea);
        if ((w & 0xFFFF0000u) == 0xF8210000u) frame = true;            // stdu r1,-N(r1)
        if ((w & 0xFC00FFFFu) == 0x80000004u) field4 = true;           // lwz rN,4(rM)
        if (w == 0x8169000Cu) slot3 = true;                            // lwz r11,0xC(r9)
        if (w == 0x4E800421u) call = true;                             // bctrl
        if (w == 0x4E800020u) break;                                   // blr
    }
    return frame && field4 && slot3 && call;
}

// ---- the race's pause (see the top) --------------------------------------------------------------------

struct PauseGetter {
    uint32_t at;                      // the getter's code (where its pattern starts)
    uint32_t user, sys, ptr, count;   // U, S, P, C
    bool ok;
} s_pg;

// A bl's target, through one cross-TOC stub (std r2,40(r1); addis r2; addi r2; b target).
uint32_t bl_target(uint32_t at)
{
    const uint32_t w = rd32(at);
    int32_t li = (int32_t)(w & 0x03FFFFFCu);
    if (li & 0x02000000) li -= 0x04000000;
    uint32_t t = at + (uint32_t)li;
    if (rd32(t) == 0xF8410028u && (rd32(t + 12) & 0xFC000003u) == 0x48000000u) {
        int32_t l2 = (int32_t)(rd32(t + 12) & 0x03FFFFFCu);
        if (l2 & 0x02000000) l2 -= 0x04000000;
        t = t + 12 + (uint32_t)l2;
    }
    return t;
}

// The getter, by its instructions (EU 1.00 0x3FFD90 onward; the same code in all four releases here, with
// or without the nop after its first call):
//   lwz r3,P(r3); li r29,0; cmpwi cr7,r3,0; clrldi r3,r3,32; beq cr7; bl count; [nop]; mr r29,r3; sync;
//   lbz r0,U(r31); clrldi r3,r30,32; cmpwi cr7,r0,0; bne cr7; lbz r0,S(r31); cmpwi cr7,r0,0; bne cr7; bl h
// count: lwz r0,C(r3); srawi r3,r0,31; subf r3,r0,r3; srwi r3,r3,31 (word[Y+C] > 0); h: li r3,0; blr.
bool find_pause_getter()
{
    int found = 0;
    for (uint32_t ea = kImageLo; ea + 0x60 < kImageHi; ea += 4) {
        const uint32_t w = rd32(ea);
        if ((w & 0xFFFF0000u) != 0x80630000u) continue;
        if (rd32(ea + 4) != 0x3BA00000u || rd32(ea + 8) != 0x2F830000u || rd32(ea + 12) != 0x78630020u) continue;
        if ((rd32(ea + 16) & 0xFFFF0003u) != 0x419E0000u || (rd32(ea + 20) & 0xFC000003u) != 0x48000001u) continue;
        uint32_t p = ea + 24;
        if (rd32(p) == 0x60000000u) p += 4;
        if (rd32(p) != 0x7C7D1B78u || rd32(p + 4) != 0x7C0004ACu) continue;
        const uint32_t u = rd32(p + 8), s = rd32(p + 24);
        if ((u & 0xFFFF0000u) != 0x881F0000u || rd32(p + 12) != 0x7BC30020u || rd32(p + 16) != 0x2F800000u) continue;
        if ((rd32(p + 20) & 0xFFFF0003u) != 0x409E0000u || (s & 0xFFFF0000u) != 0x881F0000u) continue;
        if (rd32(p + 28) != 0x2F800000u || (rd32(p + 32) & 0xFFFF0003u) != 0x409E0000u) continue;
        if ((rd32(p + 36) & 0xFC000003u) != 0x48000001u) continue;
        const uint32_t cnt = bl_target(ea + 20), h = bl_target(p + 36);
        const uint32_t c0 = rd32(cnt);
        if ((c0 & 0xFFFF0000u) != 0x80030000u || rd32(cnt + 4) != 0x7C03FE70u || rd32(cnt + 8) != 0x7C601850u ||
            rd32(cnt + 12) != 0x54630FFEu)
            continue;
        if (found++) continue;   // the first; a second one is only counted
        s_pg.at = ea;
        s_pg.ptr = w & 0xFFFFu;
        s_pg.user = u & 0xFFFFu;
        s_pg.sys = s & 0xFFFFu;
        s_pg.count = c0 & 0xFFFFu;
        s_pg.ok = s_pg.ptr < 0x4000 && s_pg.user < 0x4000 && s_pg.sys < 0x4000 && s_pg.count < 0x1000;
        if (!(rd32(h) == 0x38600000u && rd32(h + 4) == 0x4E800020u))
            fprintf(stderr, "[pad] the race's pause getter has a fourth condition (0x%08X) that isn't read\n", h);
    }
    if (found > 1) fprintf(stderr, "[pad] %d code pieces look like the race's pause getter: the first is used\n", found);
    return s_pg.ok;
}

// GTRace::DirectRaceStatus: its functions that read this+4 (X), wrapped to note X.
struct StatusSlot { uint32_t opd; void (*fn)(ppu_context*); };
const int kStatusMax = 48;
StatusSlot s_status[kStatusMax];
int s_nstatus;
volatile uint32_t s_race_x;                 // the race engine as last seen, 0 = never
volatile unsigned long long s_race_x_ms;    // cellPad_now_ms when
volatile unsigned s_status_calls;           // calls seen (PAD_DRIVE_DUMP)

void status_note(uint32_t self)
{
    __atomic_add_fetch(&s_status_calls, 1u, __ATOMIC_RELAXED);
    if (!guest_ok(self, 8)) return;
    const uint32_t x = rd32(self + 4);
    if (!guest_ok(x, 0x1000)) return;
    __atomic_store_n(&s_race_x, x, __ATOMIC_RELAXED);
    __atomic_store_n(&s_race_x_ms, cellPad_now_ms(), __ATOMIC_RELAXED);
}

template <int N>
void status_wrap(ppu_context* ctx)
{
    status_note((uint32_t)ctx->gpr[3]);
    s_status[N].fn(ctx);
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = nullptr;
        tf(ctx);
    }
}

template <int... I>
constexpr std::array<void (*)(ppu_context*), sizeof...(I)> status_wraps(std::integer_sequence<int, I...>)
{
    return {{&status_wrap<I>...}};
}
const std::array<void (*)(ppu_context*), kStatusMax> kStatusWrap = status_wraps(std::make_integer_sequence<int, kStatusMax>{});

// Wraps DirectRaceStatus's functions that read this+4 and have a descriptor of their own (no other
// table or code refers to it, so only calls on this class land in the wrapper). Returns how many.
int hook_race_status()
{
    const uint32_t vt = find_vtable("N6GTRace16DirectRaceStatusE");
    if (!vt) return 0;
    uint32_t opd[64], code[64];
    int n = 0;
    for (int i = 2; i < 64; i++) {
        const uint32_t o = rd32(vt + 8 + 4 * i);
        if (o < kImageLo || o >= kImageHi || (o & 3)) break;
        const uint32_t c = rd32(o);
        if (c < kImageLo || c >= kImageHi || (c & 3)) break;
        bool reads_x = false;
        for (int k = 0; k < 5; k++)
            if ((rd32(c + 4 * k) & 0xFC1FFFFFu) == 0x80030004u) reads_x = true;   // lwz rN,4(r3)
        if (reads_x) { opd[n] = o; code[n] = c; n++; }
    }
    if (!n) return 0;
    uint32_t lo = opd[0], hi = opd[0];
    for (int i = 1; i < n; i++) { if (opd[i] < lo) lo = opd[i]; if (opd[i] > hi) hi = opd[i]; }
    unsigned refs[64] = {};
    for (uint32_t ea = kImageLo; ea < kImageHi; ea += 4) {
        const uint32_t w = rd32(ea);
        if (w < lo || w > hi) continue;
        for (int i = 0; i < n; i++) if (w == opd[i]) refs[i]++;
    }
    for (int i = 0; i < n && s_nstatus < kStatusMax; i++) {
        if (refs[i] != 1) continue;
        void (*fn)(ppu_context*) = lifted(code[i]);
        if (!fn) continue;
        const int k = s_nstatus++;
        s_status[k].opd = opd[i];
        s_status[k].fn = fn;
        ppu_register_function(opd[i], kStatusWrap[k]);
        wr32(opd[i], opd[i]);
    }
    return s_nstatus;
}

// When the current driving began after a gap (a new race): an X noted before the last race ended is stale.
unsigned long long s_play_last, s_play_prev_end;

// The race's pause now: 1 paused, 0 running, -1 not known (no getter, X not seen in this race, or X not
// looking like the race engine).
int race_pause_state()
{
    if (!s_pg.ok) return -1;
    const uint32_t x = __atomic_load_n(&s_race_x, __ATOMIC_RELAXED);
    if (!x || __atomic_load_n(&s_race_x_ms, __ATOMIC_RELAXED) <= s_play_prev_end) return -1;
    if (!guest_ok(x, s_pg.ptr + 4)) return -1;
    const uint8_t u = rd8(x + s_pg.user), s = rd8(x + s_pg.sys);
    if (u > 1 || s > 1) return -1;
    if (u || s) return 1;
    const uint32_t y = rd32(x + s_pg.ptr);
    if (y) {
        if (!guest_ok(y, s_pg.count + 4)) return -1;
        if ((int32_t)rd32(y + s_pg.count) > 0) return 1;
    }
    return 0;
}

// The CELL_PAD_CTRL_* bit of an analog pressure channel / of a button channel, 0 = none.
uint16_t press_bit(uint8_t ch)
{
    static const uint16_t k[12] = {UP, DOWN, LEFT, RIGHT, CIRCLE, CROSS, TRIANGLE, SQUARE, L1, R1, L2, R2};
    return ch >= 0x18 && ch <= 0x23 ? k[ch - 0x18] : 0;
}

uint16_t button_bit(uint8_t ch)
{
    switch (ch) {
    case 1: return UP;          case 2: return DOWN;        case 3: return LEFT;        case 4: return RIGHT;
    case 7: return START;       case 8: return SELECT;      case 9: return CIRCLE;      case 10: return CROSS;
    case 11: return TRIANGLE;   case 12: return SQUARE;     case 0x17: return L1;       case 0x18: return R1;
    case 0x19: return L2;       case 0x1A: return R2;       case 0x1B: return L3;       case 0x1C: return R3;
    default: return 0;
    }
}

const char* bit_name(uint16_t b)
{
    switch (b) {
    case SELECT: return "Select";     case L3: return "L3";           case R3: return "R3";
    case START: return "Start";       case UP: return "Up";           case RIGHT: return "Right";
    case DOWN: return "Down";         case LEFT: return "Left";       case L2: return "L2";
    case R2: return "R2";             case L1: return "L1";           case R1: return "R1";
    case TRIANGLE: return "Triangle"; case CIRCLE: return "Circle";   case CROSS: return "Cross";
    case SQUARE: return "Square";     default: return "none";
    }
}

// The four driving jobs: accelerate, brake, shift up, shift down. Their AnalogCode and ButtonCode, and
// the button each prefers when the assignment has it among others: the one a trigger or bumper is on
// a PS3 pad, so a player who put them there keeps exactly that.
const uint8_t kAnalogJob[4] = {4, 5, 9, 10};
const uint8_t kButtonJob[4] = {3, 5, 9, 10};
const uint16_t kPrefer[4] = {R2, L2, R1, L1};

struct Map {
    uint16_t job[4];      // CELL_PAD_CTRL_* button for accelerate, brake, shift up, shift down; 0 = none
    bool analog[4];       // ... read as a pressure (else as a button)
    uint16_t jobs;        // every button the assignment gives something to do
    unsigned full[2];     // the pressure the game reads as full throttle / full brake on that button, 0 = on/off
    bool operator!=(const Map& o) const { return memcmp(this, &o, sizeof *this) != 0; }
};

// The pressure at which the game reads a job fully on a button. GT5 (0x52F080 in Europe 1.00) scales the
// pressure of the d-pad, the face buttons, L1 and R1 per job (throttle x 4, brake x 200 / 65); L2 and R2
// on a SIXAXIS / DUALSHOCK 3 (class 7) it reads straight, 0-255, as they are real triggers there.
unsigned full_pressure(uint16_t bit, uint32_t cls, unsigned scaled)
{
    return (bit == L2 || bit == R2) && cls == 7 ? 255 : scaled;
}

// A vector {-, begin, end, capacity} at v, of `stride`-byte elements, at most `max` of them.
bool vec(uint32_t v, uint32_t stride, uint32_t max, uint32_t* begin, uint32_t* n)
{
    if (!guest_ok(v, 16)) return false;
    const uint32_t b = rd32(v + 4), e = rd32(v + 8);
    *begin = b;
    if (!b && !e) { *n = 0; return true; }
    if (!guest_ok(b, 0) || e < b || !guest_ok(e, 0) || (e - b) % stride || (e - b) / stride > max) return false;
    *n = (e - b) / stride;
    return true;
}

// The assignment's buttons for one mode and controller class. false: it isn't laid out as expected.
bool parse(uint32_t cfg, uint32_t mode, uint32_t cls, Map* m)
{
    memset(m, 0, sizeof *m);
    uint32_t gb, gn;
    if (!vec(cfg, 0x14, 16, &gb, &gn)) return false;
    uint32_t grp = 0;
    for (uint32_t i = 0; i < gn && !grp; i++)
        if (rd16(gb + i * 0x14) == mode) grp = gb + i * 0x14;
    if (!grp) return false;
    uint32_t cb, cn;
    if (!vec(grp + 4, 0x24, 64, &cb, &cn)) return false;
    uint32_t c = 0;
    for (uint32_t i = 0; i < cn && !c; i++)
        if (rd32(cb + i * 0x24) == cls) c = cb + i * 0x24;
    if (!c) return false;
    uint32_t ab, an, bb, bn;
    if (!vec(c + 4, 2, 128, &ab, &an) || !vec(c + 0x14, 2, 128, &bb, &bn)) return false;
    uint16_t first_a[4] = {0, 0, 0, 0}, first_b[4] = {0, 0, 0, 0};
    bool pref_a[4] = {false, false, false, false}, pref_b[4] = {false, false, false, false};
    for (uint32_t i = 0; i < an; i++) {
        const uint8_t act = rd8(ab + 2 * i), ch = rd8(ab + 2 * i + 1);
        const uint16_t bit = press_bit(ch);
        if (!act || !bit) continue;   // the sticks and the motion sensor are no buttons
        m->jobs |= bit;
        for (int j = 0; j < 4; j++) {
            if (act != kAnalogJob[j]) continue;
            if (bit == kPrefer[j]) pref_a[j] = true;
            if (!first_a[j]) first_a[j] = bit;
        }
    }
    for (uint32_t i = 0; i < bn; i++) {
        const uint8_t act = rd8(bb + 2 * i), ch = rd8(bb + 2 * i + 1);
        const uint16_t bit = button_bit(ch);
        if (!act || !bit) continue;
        m->jobs |= bit;
        for (int j = 0; j < 4; j++) {
            if (act != kButtonJob[j]) continue;
            if (bit == kPrefer[j]) pref_b[j] = true;
            if (!first_b[j]) first_b[j] = bit;
        }
    }
    for (int j = 0; j < 4; j++) {
        if (pref_a[j] || pref_b[j]) { m->job[j] = kPrefer[j]; m->analog[j] = pref_a[j]; }
        else if (first_a[j])        { m->job[j] = first_a[j]; m->analog[j] = true; }
        else                        { m->job[j] = first_b[j]; m->analog[j] = false; }
    }
    return true;
}

// The pad class of a controller type, as InputPort's own code (0x52DC44) picks the assignment it reads:
// 0 = no pad on this port. A type is 1 + the pad (1-7), 8 + the wheel (8-17), or 0x12 + 7 x wheel + pad
// (a port with both; pad 0 = none, 6 = SIXAXIS). Every race updates player 2's port too, empty: type
// 0x12, no pad and no wheel. 0x18 is our SIXAXIS with no wheel on it.
uint32_t pad_class(uint32_t type)
{
    uint32_t pad;
    if (type >= 1 && type <= 7) pad = type - 1;
    else if (type >= 0x12 && type - 0x12 <= 0x45) pad = (type - 0x12) % 7;
    else return 0;
    return pad ? pad + 1 : 0;
}

// The game's ports as update() has shown them: each InputPort (one per player), its assignment and
// what it holds. Only a port with a pad on it says which buttons drive; when two do (split screen),
// the first player's (the lower object: GT5's GTController::DefaultConfig keeps its two ports in
// order) decides.
struct Port {
    uint32_t self, cfg, mode, cls, type;
    unsigned long long seen_ms, read_ms;
    bool ok;
    Map map;
};
Port s_ports[4];
volatile char s_ports_lock;

Port* port_of(uint32_t self, unsigned long long now)
{
    Port* free_slot = nullptr;
    for (Port& p : s_ports) {
        if (p.self == self) return &p;
        if (!free_slot && (!p.self || p.seen_ms + 2000 < now)) free_slot = &p;   // gone for 2 s: reuse
    }
    if (!free_slot) free_slot = &s_ports[0];
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->self = self;
    return free_slot;
}

// The player's port: a pad on it, seen in the last second, the lowest object. nullptr: none.
const Port* player_port(unsigned long long now)
{
    const Port* best = nullptr;
    for (const Port& p : s_ports)
        if (p.self && p.cls && p.seen_ms + 1000 >= now && (!best || p.self < best->self)) best = &p;
    return best;
}

uint32_t s_player_self;   // the port the trace follows

// Before the game's update: what it is about to read, handed to cellPad.
void note(uint32_t self, uint32_t mode, uint32_t input)
{
    static Map s_said;           // what the last "[pad] the game's buttons" line said
    static uint32_t s_said_port, s_bad_cfg, s_bad_mode, s_bad_type;
    static bool s_said_any;
    const bool play = mode == 1 || mode == 3;
    while (__atomic_test_and_set(&s_ports_lock, __ATOMIC_ACQUIRE)) {}
    const unsigned long long now = cellPad_now_ms();   // under the lock: never older than a seen_ms
    if (play) {
        Port* p = port_of(self, now);
        const uint32_t type = guest_ok(input, 4) ? rd16(input + 2) : 0;
        const uint32_t cls = pad_class(type);
        const uint32_t cfg = guest_ok(self, 8) ? rd32(self + 4) : 0;
        p->seen_ms = now;
        // Read when it may have changed: another assignment, mode or controller, and twice a second
        // (the game's Options page edits the assignment in place). A port with no pad isn't read.
        if (cfg != p->cfg || mode != p->mode || cls != p->cls || now - p->read_ms >= 500) {
            p->cfg = cfg; p->mode = mode; p->cls = cls; p->type = type; p->read_ms = now;
            p->ok = false;
            memset(&p->map, 0, sizeof p->map);
            if (cls && cfg) {
                if (parse(cfg, mode, cls, &p->map)) {
                    p->map.full[0] = p->map.analog[0] ? full_pressure(p->map.job[0], cls, s_accel_full) : 0;
                    p->map.full[1] = p->map.analog[1] ? full_pressure(p->map.job[1], cls, s_brake_full) : 0;
                    p->ok = true;
                } else {
                    memset(&p->map, 0, sizeof p->map);
                    if (cfg != s_bad_cfg || mode != s_bad_mode || type != s_bad_type) {   // once per case
                        s_bad_cfg = cfg; s_bad_mode = mode; s_bad_type = type;
                        fprintf(stderr, "[pad] the game's button assignment isn't laid out as expected (0x%08X, mode %u, "
                                        "controller %u): the triggers stay as on a PS3 pad\n", cfg, mode, type);
                    }
                }
            }
        }
    }
    const Port* pl = player_port(now);
    Map m;
    if (pl) memcpy(&m, &pl->map, sizeof m); else memset(&m, 0, sizeof m);   // no pad driving (a wheel alone): PS3 layout
    s_player_self = pl ? pl->self : 0;
    // One line for each real change of the buttons that drive (or of the port they come from).
    if (pl && (!s_said_any || m != s_said || pl->self != s_said_port)) {
        s_said_any = true;
        memcpy(&s_said, &m, sizeof s_said);
        s_said_port = pl->self;
        if (pl->ok)
            fprintf(stderr, "[pad] the game's buttons (mode %u, controller %u, assignment 0x%08X): throttle %s (full at "
                            "%u), brake %s (full at %u), shift up %s, shift down %s\n",
                    pl->mode, pl->type, pl->cfg, bit_name(m.job[0]), m.full[0], bit_name(m.job[1]), m.full[1],
                    bit_name(m.job[2]), bit_name(m.job[3]));
    }
    // The race's pause: with every driving update(), before its buttons (one line per change).
    int pause = -1;
    if (play) {
        if (s_play_last && now > s_play_last + 1500) s_play_prev_end = s_play_last;   // a new race
        s_play_last = now;
        pause = race_pause_state();
        static int said = -2;
        if (pause != said) {
            said = pause;
            fprintf(stderr, "[pad] t=%llu the race%s\n", now,
                    pause == 1 ? " is paused" : pause == 0 ? " runs" : "'s pause isn't known (yet)");
        }
    }
    __atomic_clear(&s_ports_lock, __ATOMIC_RELEASE);
    if (play) cellPad_race_pause(pause);
    cellPad_drive_frame(play ? 1 : 0, m.job[0], m.job[1], m.job[2], m.job[3], m.full[0], m.full[1], m.jobs);
}

// PAD_DRIVE_DUMP=1: once a second, from the player's port, what the pause is worked out from.
int s_dump;
void dump(uint32_t self, uint32_t mode, uint32_t input, uint32_t out, uint32_t lr)
{
    static unsigned long long last;
    if (self != s_player_self) return;
    const unsigned long long now = cellPad_now_ms();
    if (now < last + 1000) return;
    const unsigned calls = __atomic_exchange_n(&s_status_calls, 0u, __ATOMIC_RELAXED);
    const unsigned long long dt = last ? now - last : 1000;
    last = now;
    const uint32_t x = __atomic_load_n(&s_race_x, __ATOMIC_RELAXED);
    const bool xo = x && guest_ok(x, 0x1000) && s_pg.ok;
    const uint32_t y = xo ? rd32(x + s_pg.ptr) : 0;
    const uint32_t p = x && guest_ok(x, 0x20) ? rd32(x + 0x1C) : 0;
    fprintf(stderr, "[pad-dump] t=%llu mode %u controller %u port %08X assignment %08X input %08X out %08X caller %08X"
                    " | status calls %u/s, X %08X (seen %lld ms ago), getter %08X: U+%X=%u S+%X=%u Y(+%X)=%08X"
                    " count=%d -> %d | X+1C %08X: +30 %08X +34 %08X\n",
            now, mode, guest_ok(input, 4) ? rd16(input + 2) : 0, self, guest_ok(self, 8) ? rd32(self + 4) : 0, input,
            out, lr, (unsigned)(calls * 1000 / (dt ? dt : 1)), x,
            x ? (long long)(now - __atomic_load_n(&s_race_x_ms, __ATOMIC_RELAXED)) : -1LL, s_pg.at,
            s_pg.user, xo ? rd8(x + s_pg.user) : 0, s_pg.sys, xo ? rd8(x + s_pg.sys) : 0, s_pg.ptr, y,
            y && guest_ok(y, s_pg.count + 4) ? (int)rd32(y + s_pg.count) : 0, race_pause_state(), p,
            p && guest_ok(p, 0x38) ? rd32(p + 0x30) : 0, p && guest_ok(p, 0x38) ? rd32(p + 0x34) : 0);
}

// After it: what the game made of the player's pad (PAD_TRACE=2).
void trace(uint32_t self, uint32_t mode, uint32_t out)
{
    static int last_t = -1, last_b = -1;
    static unsigned lines;
    if (!(mode == 1 || mode == 3) || self != s_player_self || !guest_ok(out, 0x10)) return;
    const uint8_t f = rd8(out);
    const int t = (f & 2) ? (int)rd16(out + 8) * 100 / 4096 : 0;
    const int b = (f & 4) ? (int)rd16(out + 0xA) * 100 / 4096 : 0;
    if ((abs(t - last_t) >= 2 || abs(b - last_b) >= 2 || (t == 0) != (last_t == 0) || (b == 0) != (last_b == 0)) &&
        lines < 20000) {
        lines++;
        last_t = t; last_b = b;
        fprintf(stderr, "[pad] t=%llu the game reads throttle %d%%, brake %d%%\n", cellPad_now_ms(), t, b);
    }
}

void inputport_update(ppu_context* ctx)
{
    const uint32_t self = (uint32_t)ctx->gpr[3], mode = (uint32_t)ctx->gpr[4];
    const uint32_t input = (uint32_t)ctx->gpr[5], out = (uint32_t)ctx->gpr[6];
    note(self, mode, input);
    if (s_dump) dump(self, mode, input, out, (uint32_t)ctx->lr);
    s_update(ctx);
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = nullptr;
        tf(ctx);
    }
    if (s_trace) trace(self, mode, out);
}

void gt5_padmap_apply()
{
    const char* e = getenv("PAD_DRIVE_HOOK");
    if (e && e[0] == '0') {
        fprintf(stderr, "[pad] PAD_DRIVE_HOOK=0: the triggers stay as on a PS3 pad\n");
        return;
    }
    e = getenv("PAD_TRACE");
    s_trace = e && atoi(e) >= 2;   // play.bat ships PAD_TRACE=1: this many lines only when asked for
    e = getenv("PAD_DRIVE_DUMP");
    s_dump = e && *e && *e != '0';
    // Found once, when the game is loaded. The wrapper is registered under the descriptor's address and
    // the descriptor pointed at itself: the game's vtable call lands in inputport_update. No other code
    // refers to the descriptor, and nothing calls update() directly (Europe 1.00).
    const uint32_t opd = find_update_opd();
    s_code = opd ? rd32(opd) : 0;
    if (!opd || !looks_like_update(s_code)) {
        fprintf(stderr, "[pad] the game's race input isn't where it's expected (0x%08X): the triggers stay as on a "
                        "PS3 pad\n", s_code);
        return;
    }
    for (uint64_t i = 0; i < function_table_count; i++)
        if (function_table[i].addr == s_code) s_update = function_table[i].func;
    if (!s_update) {
        fprintf(stderr, "[pad] the game's race input (0x%08X) isn't in the lifted code: the triggers stay as on a "
                        "PS3 pad\n", s_code);
        return;
    }
    if (has_code(kAccelScale, 6)) s_accel_full = 64;
    if (has_code(kBrakeScale, 6)) s_brake_full = 83;
    s_opd = opd;
    ppu_register_function(opd, inputport_update);
    wr32(opd, opd);
    fprintf(stderr, "[pad] the game's race input at 0x%08X: the triggers follow its buttons (full throttle at "
                    "pressure %u, full brake at %u)\n", s_code, s_accel_full, s_brake_full);
    // The race's pause: its getter's numbers, and the status object that names the race engine.
    if (!find_pause_getter()) {
        fprintf(stderr, "[pad] the race's pause getter isn't where it's expected: the pause isn't known\n");
        return;
    }
    const int wrapped = hook_race_status();
    if (!wrapped) {
        fprintf(stderr, "[pad] the race's status object isn't where it's expected: the pause isn't known\n");
        s_pg.ok = false;
        return;
    }
    fprintf(stderr, "[pad] the race's pause: getter at 0x%08X (paused +0x%X, +0x%X, counter at +0x%X then +0x%X), "
                    "%d status functions noted\n", s_pg.at, s_pg.user, s_pg.sys, s_pg.ptr, s_pg.count, wrapped);
}

const int s_registered = (ps3_on_loaded(gt5_padmap_apply), 0);

}  // namespace
