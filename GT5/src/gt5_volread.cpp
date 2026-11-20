// Volume reader: finds the listed shader programs on a fresh PC before the game draws them, by reading the
// game's model files with the game's OWN file code while the player is in the menus. Nothing of the
// volume's format, key or index is known here: the game's code lists, opens, decrypts and unpacks; this
// only asks it to, and hands the unpacked bytes to the shader scan (libs/video/rsx_shader_prep.inc), which
// stores the programs it knows from the shipped list and builds them ahead.
//
// Where programs are (probe runs 1-2, 19 Nov, Europe 1.00): only in model containers ("MDL3" / "PACB" once
// unpacked): /car/hq, /car/race, /car/interior, /wheel/hq, /wheel/race, /tire/hq, /tire/race, /character,
// /crowd, and in /crs the course models <course>x and the skies <course>.sky / .envsky. Programs repeat a lot
// between files (the second car: 1 new, 30 already known), so the directories are read in turns, one file
// of each, the ones a player meets first first: cars (menus), courses, wheels, tyres, then the rest.
// 18,813 files, 6.0 GB stored / 15.2 GB unpacked: ~5 min of reading at the idle rate (20 MB/s stored,
// unpack 240 MB/s); it usually stops far earlier, when every listed program is known.
//
// Rules: on by default when the shader lists have programs not known yet (no list or nothing unknown:
// nothing happens); GT5_VOLREAD=0 off. One guest thread of its own at the lowest priority. Between files it
// waits while the game itself uses its file layer (a request of its own in the last 500 ms, seen by wrapping
// FileObject's request entry; its loads go first) and while a player drives (see
// player_driving: not the menus' demo race, car select or pre-race). A file read
// once (searched to the end) is written to <prep dir>\volume_read.txt and never read again for that shipped
// list. Memory: one 30 MB and one 62 MB guest buffer from the runtime's heap while it runs, freed at the end.
// Lines: "[shader-prep] volume: ..." when it starts, 20 s in, once a minute, and when it stops.
//
// The game's functions are FOUND at start, not taken from fixed addresses: three of them by their code
// (find_functions: branch targets, TOC offsets and address halves masked; exactly one match each), the
// others as the targets of their calls at fixed places inside those three, cross-checked (load and stat
// must call the same open / run / close), and the TOC from the executable's function descriptors. The
// patterns are Europe 1.00's; Asia 1.00 matches them (checked offline: same addresses, its TOC 0x018765A8);
// 2.xx does not (another compiler schedule): then one line names what was not found and nothing else
// happens. The addresses found go to the log. Europe 1.00's, for reference (TOC 0x018765B0):
//   0x00BB539C FileObject ctor, 0x00BB61F0 open(obj, path), 0x00BB4D34 run the request and wait for it,
//   0x00BB6408 close, 0x00BB522C dtor; request code at obj+0x84, 3 = list a directory with sizes (entries at
//   obj+0xA0, room at +0xA4; 0x40 bytes each: +0 type 3 directory / 1 file, +4 size as stored, +8 unpacked,
//   +0xC name), result at obj+0xB0 (status, +0x10 the number of entries);
//   0x00BB6A90 load(result, path, allocator, {buffer, size}, range[4], flag): the file as stored;
//   0x00BE27A4 unpack(state, dst, src, 1) inflates it whole, 0x00BE271C(state) ends the state (as 0x008C8998).
// The requests run on the volume device's own thread, as the game's own do.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ppu_context.h"

#ifdef _WIN32
#include <windows.h>

extern "C" unsigned char* vm_base;
extern "C" void ps3_on_loaded(void (*fn)(void));
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern "C" int64_t sys_ppu_thread_create(ppu_context* ctx);
extern "C" void* sys_heap_malloc(uint32_t heap, uint32_t size);
extern "C" int32_t sys_heap_free(uint32_t heap, void* ptr);
extern "C" int cellPad_race_pause_state(void);   // libs/input/cellPad.c: -1 = no race engine driving a car
extern "C" unsigned long long cellPad_last_activity_ms(void);   // the last poll with a pad or the keys in use
extern "C" unsigned long long cellPad_now_ms(void);
// libs/video/rsx_shader_prep.inc
extern "C" int rsx_shader_prep_scan_guest(uint32_t ea, uint32_t n, uint32_t* found, uint32_t* known,
                                          uint32_t* vertex);
extern "C" uint32_t rsx_shader_prep_scan_listed(void);
extern "C" uint32_t rsx_shader_prep_scan_unknown(uint64_t* list_hash, const char** dir);

struct gt5_func_entry {
    uint64_t addr;
    void (*func)(ppu_context*);
    const char* name;
};
extern "C" const gt5_func_entry function_table[];
extern "C" const uint64_t function_table_count;

namespace {

// Found by find_functions().
uint32_t kToc, kFoCtor, kFoDtor, kFoOpen, kFoRun, kFoClose, kLoad, kUnpack, kUnpackEnd;

// Europe 1.00's code of the three functions searched for, masked as mask() masks (tools\sig2.py made them):
// load (0x00BB6A90, 82 words: its calls of open at +0x70, run at +0xF0, close at +0x140), stat (0x00BB6708,
// 58 words: FileObject ctor +0x3C, open +0x4C, run +0x6C, close +0xBC, dtor +0xE4) and the packed-buffer
// reader (0x008C8998, 44 words: unpack +0x9C, end +0xA8, both through cross-TOC stubs).
const uint32_t kPatLoad[] = {
    0xF821FE01, 0x7C0802A6, 0xFAE101B8, 0xFBE101F8, 0x3AE10070, 0x787F0020, 0xFBA101E8, 0x7FE3FB78,
    0x7AFD0020, 0xFB0101C0, 0xFB2101C8, 0xFB4101D0, 0xFB6101D8, 0xFB8101E0, 0xF8010210, 0x7C9C2378,
    0x7CDA3378, 0x7CFB3B78, 0x7D194378, 0x7CB82B78, 0xFBC101F0, 0x48000001, 0x60000000, 0x7FA3EB78,
    0x48000001, 0x60000000, 0x7FA3EB78, 0x7B840020, 0x48000001, 0x2F830000, 0x7C7D1B78, 0x7C7E1B78,
    0x419E0000, 0x38000000, 0x817A0004, 0x813A0000, 0x787C0020, 0x980101A0, 0x38000000, 0x91210198,
    0x9161019C, 0x9B210184, 0x90030084, 0x7B040020, 0x7F83E378, 0x48000001, 0x60000000, 0x801B0000,
    0x2F800000, 0x419E0000, 0x901D0074, 0x801B0004, 0x901D0078, 0x801B0008, 0x2F800000, 0x419E0000,
    0x901D007C, 0x801B000C, 0x901D0080, 0x7F83E378, 0x48000001, 0x60000000, 0x393E00B0, 0x7F83E378,
    0xE8090000, 0xE9690008, 0xE9490010, 0xE9090018, 0xF81F0000, 0xF97F0008, 0xF95F0010, 0xF91F0018,
    0x80E90038, 0xE8090020, 0xE9690028, 0xE9490030, 0xF81F0020, 0xF97F0028, 0xF95F0030, 0x90FF0038,
    0x48000001, 0x48000000,
};
const uint32_t kPatStat[] = {
    0xF821FE71, 0x7C0802A6, 0xFBC10180, 0xFBE10188, 0x3BC10070, 0x787F0020, 0xFBA10178, 0x7FE3FB78,
    0x7BDD0020, 0xFB810170, 0xF80101A0, 0x7C9C2378, 0x48000001, 0x60000000, 0x7FA3EB78, 0x48000001,
    0x60000000, 0x7FA3EB78, 0x7B840020, 0x48000001, 0x2F830000, 0x7C7D1B78, 0x419E0000, 0x38000004,
    0x787C0020, 0x90030084, 0x7F83E378, 0x48000001, 0x60000000, 0x393D00B0, 0x7F83E378, 0xE8090000,
    0xE9690008, 0xE9490010, 0xE9090018, 0xF81F0000, 0xF97F0008, 0xF95F0010, 0xF91F0018, 0x80E90038,
    0xE8090020, 0xE9690028, 0xE9490030, 0xF81F0020, 0xF97F0028, 0xF95F0030, 0x90FF0038, 0x48000001,
    0x48000000, 0x7C7D1B78, 0x7FC3F378, 0x48000001, 0x60000000, 0x7FA3EB78, 0x48000001, 0x60000000,
    0x7FC3F378, 0x48000001,
};
const uint32_t kPatUnpackCaller[] = {
    0xF821E721, 0x7C0802A6, 0xFB6118B8, 0xF80118F0, 0x3C000000, 0xFBA118C8, 0x6000EEC5, 0xFBE118D8,
    0xFB8118C0, 0xFBC118D0, 0x7D20242C, 0x7F890000, 0x7C7B1B78, 0x7C9D2378, 0x7CBF2B78, 0x38600000,
    0x409E0000, 0x39240004, 0x7C004C2C, 0x7C0007B4, 0x7C0000D0, 0x78030020, 0x90050000, 0x48000001,
    0xE8410028, 0x813F0000, 0x3C000000, 0x787E0020, 0x7F890040, 0x79240020, 0x7B630020, 0x409D0000,
    0x48000001, 0x7FA5EB78, 0x3BA10070, 0x38C00001, 0x7BBD0020, 0x7FC4F378, 0x7FA3EB78, 0x48000001,
    0xE8410028, 0x7FA3EB78, 0x48000001, 0xE8410028,
};

// The carrier groups, in the order a player meets them; an ending of "" means every file of the directory.
struct Group { const char* dir; const char* ending; };
const Group kGroups[] = {
    {"/car/hq", ""}, {"/crs", "x"}, {"/wheel/hq", ""}, {"/tire/hq", ""}, {"/crs", ".sky"}, {"/crs", ".envsky"},
    {"/car/race", ""}, {"/wheel/race", ""}, {"/tire/race", ""}, {"/character", ""}, {"/crowd", ""},
    {"/car/interior", ""},
};

const uint32_t kBufCap = 30u << 20, kUbufCap = 62u << 20;

uint32_t rd32(uint32_t ea) { uint32_t v; memcpy(&v, vm_base + ea, 4); return __builtin_bswap32(v); }
void wr32(uint32_t ea, uint32_t v) { v = __builtin_bswap32(v); memcpy(vm_base + ea, &v, 4); }
uint64_t rd64(uint32_t ea) { return ((uint64_t)rd32(ea) << 32) | rd32(ea + 4); }
uint32_t galloc(uint32_t n) { return (uint32_t)(uintptr_t)sys_heap_malloc(0, n); }
void gfree(uint32_t ea) { if (ea) sys_heap_free(0, (void*)(uintptr_t)ea); }

double now_s()
{
    static LARGE_INTEGER f;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}

struct Fn { uint32_t code; void (*fn)(ppu_context*); };
Fn s_fn[8];
ppu_context* s_self;   // the reader thread's own context (its TLS, thread id and stack)

void (*lifted(uint32_t code))(ppu_context*)
{
    for (uint64_t j = 0; j < function_table_count; j++)
        if (function_table[j].addr == code) return function_table[j].func;
    return nullptr;
}

// ---- finding the game's functions --------------------------------------------------------------------------
const uint32_t kImageLo = 0x00010000, kImageHi = 0x02000000;   // every GT5 release's executable fits

uint32_t mask(uint32_t w)
{
    const uint32_t op = w >> 26, ra = (w >> 16) & 31;
    if (op == 18) return w & 0xFC000003u;   // b / bl: the target
    if (op == 16) return w & 0xFFFF0003u;   // bc: the target
    if (ra == 2 && (op == 14 || op == 32 || op == 34 || op == 36 || op == 38 || op == 40 || op == 44 || op == 48 ||
                    op == 50 || op == 52 || op == 54 || op == 58 || op == 62))
        return w & 0xFFFF0000u;              // TOC-relative: the offset
    if (op == 15) return w & 0xFFFF0000u;   // lis / addis: an address half
    return w;
}

// The one place the masked pattern matches; 0 if none or more than one (*n = matches).
uint32_t find_pattern(const uint32_t* pat, uint32_t words, int* n)
{
    uint32_t at = 0;
    *n = 0;
    for (uint32_t ea = kImageLo; ea + 4 * words <= kImageHi; ea += 4) {
        if (mask(rd32(ea)) != pat[0]) continue;
        uint32_t i = 1;
        while (i < words && mask(rd32(ea + 4 * i)) == pat[i]) i++;
        if (i == words) { (*n)++; at = ea; }
    }
    return *n == 1 ? at : 0;
}

// The function a bl at ea calls, through a cross-TOC stub (std r2,40(r1); addis; addi; b) if it is one.
uint32_t bl_target(uint32_t ea)
{
    const uint32_t w = rd32(ea);
    if ((w >> 26) != 18 || (w & 3) != 1) return 0;
    int32_t off = (int32_t)(w & 0x03FFFFFCu);
    if (off & 0x02000000) off -= 0x04000000;
    uint32_t t = ea + (uint32_t)off;
    if (t >= kImageLo && t < kImageHi - 16 && rd32(t) == 0xF8410028u && (rd32(t + 12) >> 26) == 18) {
        int32_t o2 = (int32_t)(rd32(t + 12) & 0x03FFFFFCu);
        if (o2 & 0x02000000) o2 -= 0x04000000;
        t = t + 12 + (uint32_t)o2;
    }
    return t >= kImageLo && t < kImageHi ? t : 0;
}

// A function's TOC: that of the function descriptor {code, toc} with the highest code at or below it (the
// executable's descriptors; a toc counts when hundreds of descriptors share it).
uint32_t toc_of(uint32_t fn)
{
    // Every descriptor-shaped pair once: {code 4-aligned in the image, toc 8-aligned in its upper half}; the
    // tocs hundreds of pairs share are real (Europe 1.00: four, 3,800 to 8,500 functions each).
    static std::vector<std::pair<uint32_t, uint32_t>> s_opd;   // code, toc; sorted by code
    if (s_opd.empty()) {
        std::vector<std::pair<uint32_t, uint32_t>> all;
        std::unordered_map<uint32_t, uint32_t> uses;
        for (uint32_t ea = kImageLo; ea + 8 <= kImageHi; ea += 8) {
            const uint32_t code = rd32(ea), toc = rd32(ea + 4);
            if ((code & 3) || (toc & 7) || code < kImageLo || code >= toc || toc < 0x01000000 || toc >= kImageHi)
                continue;
            all.push_back({code, toc});
            uses[toc]++;
        }
        for (const auto& p : all)
            if (uses[p.second] >= 200) s_opd.push_back(p);
        std::sort(s_opd.begin(), s_opd.end());
    }
    auto it = std::upper_bound(s_opd.begin(), s_opd.end(), std::make_pair(fn, 0xFFFFFFFFu));
    if (it == s_opd.begin()) return 0;
    --it;
    return fn - it->first < 0x40000 ? it->second : 0;
}

// The game's request entry (FileObject's third virtual function, "submit") is wrapped so the reader knows
// when the game itself uses its file layer: found by the class's type name, its vtable's first function
// checked against the FileObject dtor found above. The descriptor's own address stands in for the function
// (as GT5\src\gt5_cockpit.cpp does).
void (*s_submit)(ppu_context*);
uint32_t s_submit_opd;
volatile LONG s_inflight;              // the reader has a request of its own running
volatile ULONGLONG s_game_submit_ms;   // GetTickCount64 of the game's last request
uint64_t s_reader_tid = ~0ull;

void submit_wrap(ppu_context* ctx)
{
    if (ctx->thread_id != s_reader_tid && !InterlockedCompareExchange(&s_inflight, 0, 0))
        s_game_submit_ms = GetTickCount64();
    s_submit(ctx);
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = nullptr;
        tf(ctx);
    }
}

uint32_t find_word(uint32_t value, uint32_t from)
{
    for (uint32_t ea = (from + 3) & ~3u; ea + 4 <= kImageHi; ea += 4)
        if (rd32(ea) == value) return ea;
    return 0;
}

uint32_t find_submit_opd(uint32_t dtor)
{
    static const char kName[] = "N6PDISTD10FileObjectE";   // with its terminating zero
    const unsigned char* lo = vm_base + kImageLo;
    const unsigned char* hi = vm_base + kImageHi;
    for (const unsigned char* p = lo; p + sizeof kName <= hi; p++) {
        p = (const unsigned char*)memchr(p, 'N', (size_t)(hi - p));
        if (!p || p + sizeof kName > hi) break;
        if (memcmp(p, kName, sizeof kName) != 0 || p[-1] != 0) continue;
        const uint32_t name_ea = (uint32_t)(p - vm_base);
        for (uint32_t h = find_word(name_ea, kImageLo); h; h = find_word(name_ea, h + 4)) {
            const uint32_t ti = h - 4;   // type information: {its own vtable, name, ...}
            for (uint32_t v = find_word(ti, kImageLo); v; v = find_word(ti, v + 4)) {
                // the class's vtable: {0, type information, functions...}; slot 0 = the dtor
                if (rd32(v - 4) != 0) continue;
                const uint32_t opd0 = rd32(v + 4), opd2 = rd32(v + 12);
                if (opd0 < kImageLo || opd0 >= kImageHi || rd32(opd0) != dtor) continue;
                if (opd2 >= kImageLo && opd2 < kImageHi && !(opd2 & 3)) return opd2;
            }
        }
    }
    return 0;
}

bool find_functions()
{
    int n1, n2, n3;
    const uint32_t load = find_pattern(kPatLoad, sizeof kPatLoad / 4, &n1);
    const uint32_t stat = find_pattern(kPatStat, sizeof kPatStat / 4, &n2);
    const uint32_t upk = find_pattern(kPatUnpackCaller, sizeof kPatUnpackCaller / 4, &n3);
    if (!load || !stat || !upk) {
        fprintf(stderr, "[shader-prep] volume: the game's file code was not found in this executable (load %d, stat %d, "
                        "unpack %d matches; Europe / Asia 1.00 code expected): the volume is not read\n", n1, n2, n3);
        return false;
    }
    kLoad = load;
    kFoOpen = bl_target(load + 0x70); kFoRun = bl_target(load + 0xF0); kFoClose = bl_target(load + 0x140);
    kFoCtor = bl_target(stat + 0x3C); kFoDtor = bl_target(stat + 0xE4);
    kUnpack = bl_target(upk + 0x9C); kUnpackEnd = bl_target(upk + 0xA8);
    const bool same = bl_target(stat + 0x4C) == kFoOpen && bl_target(stat + 0x6C) == kFoRun &&
                      bl_target(stat + 0xBC) == kFoClose;
    kToc = toc_of(load);
    const uint32_t want[8] = {kFoCtor, kFoDtor, kFoOpen, kFoRun, kFoClose, kLoad, kUnpack, kUnpackEnd};
    static const char* const names[8] = {"FileObject ctor", "FileObject dtor", "open", "run", "close", "load",
                                         "unpack", "unpack end"};
    for (int i = 0; i < 8; i++) {
        s_fn[i].code = want[i];
        s_fn[i].fn = want[i] ? lifted(want[i]) : nullptr;
        if (!s_fn[i].fn || !same || !kToc || toc_of(want[i]) != kToc) {
            fprintf(stderr, "[shader-prep] volume: the game's %s was not found (0x%08X%s%s): the volume is not read\n",
                    names[i], want[i], same ? "" : ", load and stat call different functions",
                    kToc ? "" : ", no TOC");
            return false;
        }
    }
    s_submit_opd = find_submit_opd(kFoDtor);
    if (s_submit_opd) {
        s_submit = lifted(rd32(s_submit_opd));
        if (s_submit) {
            ppu_register_function(s_submit_opd, submit_wrap);
            wr32(s_submit_opd, s_submit_opd);   // the descriptor now leads to submit_wrap
        }
    }
    fprintf(stderr, "[shader-prep] volume: the game's file code: load 0x%08X, stat 0x%08X, open 0x%08X, run 0x%08X, "
                    "close 0x%08X, FileObject 0x%08X / 0x%08X, unpack 0x%08X / 0x%08X (from 0x%08X), TOC 0x%08X; "
                    "its requests seen at 0x%08X%s\n", kLoad, stat, kFoOpen, kFoRun, kFoClose, kFoCtor, kFoDtor,
            kUnpack, kUnpackEnd, upk, kToc, s_submit_opd, s_submit ? "" : " (not wrapped: waits by disc reads)");
    return true;
}

uint64_t call(uint32_t code, uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0,
              uint64_t a4 = 0, uint64_t a5 = 0)
{
    void (*fn)(ppu_context*) = nullptr;
    for (const Fn& f : s_fn)
        if (f.code == code) fn = f.fn;
    if (!fn) return 0;
    InterlockedExchange(&s_inflight, 1);
    ppu_context c = *s_self;
    c.gpr[1] = (s_self->gpr[1] - 0x2000) & ~0xFull;   // below the thread's own frame
    c.gpr[2] = kToc;
    c.gpr[3] = a0; c.gpr[4] = a1; c.gpr[5] = a2; c.gpr[6] = a3; c.gpr[7] = a4; c.gpr[8] = a5;
    c.lr = 0;
    c.cia = code;
    ppu_context* saved = g_active_ctx;
    g_active_ctx = &c;
    fn(&c);
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = nullptr;
        tf(&c);
    }
    g_active_ctx = saved;
    InterlockedExchange(&s_inflight, 0);
    return c.gpr[3];
}

uint32_t s_path, s_obj, s_result, s_bufdesc, s_range, s_state, s_ent, s_ent_cap, s_buf, s_ubuf;

void put_path(const std::string& p)
{
    const size_t n = p.size() < 1000 ? p.size() : 1000;
    memcpy(vm_base + s_path, p.data(), n);
    vm_base[s_path + n] = 0;
}

struct Ent { std::string name; uint32_t type, stored, unpacked; };

// One directory as the game lists it: 0 = listed, else the request's status (4: the volume's index is not
// read yet), -1 = not opened.
int list_dir(const std::string& dir, std::vector<Ent>& out)
{
    out.clear();
    for (int pass = 0; pass < 2; pass++) {
        put_path(dir);
        memset(vm_base + s_obj, 0, 0x200);
        call(kFoCtor, s_obj);
        const uint32_t o = (uint32_t)call(kFoOpen, s_obj, s_path);
        int status = -1;
        uint64_t count = 0;
        if (o) {
            wr32(o + 0x84, 3);
            wr32(o + 0xA0, pass ? s_ent : 0);
            wr32(o + 0xA4, pass ? s_ent_cap : 0);
            call(kFoRun, o);
            status = (int)rd32(o + 0xB0);
            count = rd64(o + 0xC0);
            call(kFoClose, o);
        }
        call(kFoDtor, s_obj);
        if (status != 0) return status;
        if (pass == 0) {
            if (count > s_ent_cap) {
                const uint32_t want = (uint32_t)(count + 64);
                const uint32_t e = galloc(want * 0x40);
                if (!e) return -2;
                gfree(s_ent);
                s_ent = e; s_ent_cap = want;
            }
            continue;
        }
        if (count > s_ent_cap) count = s_ent_cap;
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t e = s_ent + i * 0x40;
            char name[64];
            memcpy(name, vm_base + e + 0xC, 52);
            name[52] = 0;
            out.push_back({name, rd32(e), rd32(e + 4), rd32(e + 8)});
        }
    }
    return 0;
}

bool ends_with(const std::string& s, const char* e)
{
    const size_t n = strlen(e);
    return s.size() >= n && s.compare(s.size() - n, n, e) == 0;
}

// The game using its file layer right now (its own loads go first). With the request wrapper: a request of
// the game's (not the reader's, not made while the reader's own request runs: the device's work for it) in
// the last 500 ms. Without it (the wrapper not found): process reads over 100 ms while this thread reads
// nothing, 1 MB in 100 ms = 10 MB/s. Round 4 showed that test never firing: a 100 ms sample once a second
// rarely meets the game's short, mostly small loads (the demo's files are tens of KB).
bool game_reading()
{
    if (s_submit) {
        if (GetTickCount64() - s_game_submit_ms >= 500) return false;
        Sleep(100);
        return true;
    }
    IO_COUNTERS a, b;
    if (!GetProcessIoCounters(GetCurrentProcess(), &a)) return false;
    Sleep(100);
    if (!GetProcessIoCounters(GetCurrentProcess(), &b)) return false;
    return b.ReadTransferCount - a.ReadTransferCount >= (1u << 20);
}

struct Stats {
    uint64_t files = 0, stored = 0, unpacked = 0, found = 0, found_v = 0, too_big = 0, failed = 0;
    double wait_load = 0, wait_drive = 0;
} s_st;
uint32_t s_gfound[sizeof kGroups / sizeof kGroups[0]];          // programs found per group
std::string s_glast[sizeof kGroups / sizeof kGroups[0]];        // the group's file of its last find

// One file: load as stored, unpack if packed, search. false: not read (too big / failed).
bool read_file(const std::string& path, const Ent& e)
{
    if (e.stored > kBufCap || e.unpacked > kUbufCap) { s_st.too_big++; return true; }   // never fits: done
    put_path(path);
    memset(vm_base + s_result, 0, 0x80);
    wr32(s_bufdesc, s_buf);
    wr32(s_bufdesc + 4, kBufCap);
    memset(vm_base + s_range, 0, 16);
    wr32(s_buf, 0);
    call(kLoad, s_result, s_path, 0, s_bufdesc, s_range, 0);
    if (rd32(s_result) != 0) { s_st.failed++; return false; }
    uint32_t data = s_buf, size = e.stored;
    if (rd32(s_buf) == 0xC5EEF7FFu) {   // packed: C5 EE F7 FF, then minus the unpacked size (little-endian)
        const uint32_t usize = 0u - __builtin_bswap32(rd32(s_buf + 4));
        if (usize == 0 || usize > kUbufCap) { s_st.too_big++; return true; }
        memset(vm_base + s_state, 0, 0x2000);
        call(kUnpack, s_state, s_ubuf, s_buf, 1);
        call(kUnpackEnd, s_state);
        data = s_ubuf;
        size = usize;
    }
    uint32_t found = 0, known = 0, vert = 0;
    rsx_shader_prep_scan_guest(data, size, &found, &known, &vert);
    s_st.files++; s_st.stored += e.stored; s_st.unpacked += size; s_st.found += found; s_st.found_v += vert;
    return true;
}

// A player driving: a race engine drives a car and is not paused (cellPad_race_pause_state() == 0) AND the
// controller or the keys were used within the last 3 s. The race engine alone is not enough: the title
// screen and the top menu run a demo race behind the menus, and car select and the pre-race screen keep a
// live one (19 Nov, D87: the reader slept through 78 s of menus). In a race the throttle is held; at the
// menus, the demo, replays, the pre-race and loading screens nobody holds anything for long, so the reader
// runs there (menu presses hold it back for 3 s each). A paused race (1) is not driving.
bool player_driving()
{
    if (cellPad_race_pause_state() != 0) return false;
    const unsigned long long now = cellPad_now_ms(), act = cellPad_last_activity_ms();
    return act && now >= act && now - act < 3000;
}

void reader_thread(ppu_context* ctx)
{
    s_self = ctx;
    s_reader_tid = ctx->thread_id;
    const double t0 = now_s();
    // The lists first (shader-prep reads them a few seconds in); nothing to look for: nothing to do.
    uint64_t hash = 0;
    const char* dir = nullptr;
    for (int i = 0; i < 2400 && !rsx_shader_prep_scan_listed(); i++) Sleep(250);
    const uint32_t unknown0 = rsx_shader_prep_scan_unknown(&hash, &dir);
    if (!unknown0 || !dir) {
        fprintf(stderr, "[shader-prep] volume: %s: nothing to read\n",
                rsx_shader_prep_scan_listed() ? "every listed program is known" : "no shader list");
        return;
    }
    s_path = galloc(1024); s_obj = galloc(0x200); s_result = galloc(0x80); s_bufdesc = galloc(16);
    s_range = galloc(16); s_state = galloc(0x2000); s_ent_cap = 4096; s_ent = galloc(s_ent_cap * 0x40);
    s_buf = galloc(kBufCap); s_ubuf = galloc(kUbufCap);
    if (!s_path || !s_obj || !s_result || !s_bufdesc || !s_range || !s_state || !s_ent || !s_buf || !s_ubuf) {
        fprintf(stderr, "[shader-prep] volume: no guest memory for the reader: not read this session\n");
        return;
    }
    // The done-list: "list <hash>" then one path a line; another shipped list starts it over.
    const std::string done_path = std::string(dir) + "\\volume_read.txt";
    std::unordered_set<std::string> done;
    char head[64];
    snprintf(head, sizeof head, "list %016llX", (unsigned long long)hash);
    if (FILE* f = fopen(done_path.c_str(), "r")) {
        char line[1100];
        bool same = fgets(line, sizeof line, f) && strncmp(line, head, strlen(head)) == 0;
        while (same && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0]) done.insert(line);
        }
        fclose(f);
        if (!same) done.clear();
    }
    FILE* dl = fopen(done_path.c_str(), done.empty() ? "w" : "a");
    if (dl && done.empty()) fprintf(dl, "%s\n", head);
    // The volume's index: listing answers 4 until the game has read it.
    std::vector<std::vector<std::pair<std::string, Ent>>> todo(sizeof kGroups / sizeof kGroups[0]);
    std::vector<Ent> ents;
    int st = -1;
    for (int i = 0; i < 1200; i++) {
        st = list_dir(kGroups[0].dir, ents);
        if (st != 4 && st != -1) break;
        Sleep(250);
    }
    if (st != 0) {
        fprintf(stderr, "[shader-prep] volume: the game's volume did not answer (status %d): not read this session\n", st);
        if (dl) fclose(dl);
        return;
    }
    size_t n_files = 0, n_before = 0;
    uint64_t n_bytes = 0;
    for (size_t g = 0; g < todo.size(); g++) {
        if (list_dir(kGroups[g].dir, ents) != 0) continue;
        for (const Ent& e : ents) {
            if (e.type != 1 || (kGroups[g].ending[0] && !ends_with(e.name, kGroups[g].ending))) continue;
            std::string p = std::string(kGroups[g].dir) + "/" + e.name;
            if (done.count(p)) { n_before++; continue; }
            n_files++; n_bytes += e.stored;
            todo[g].push_back({p, e});
        }
    }
    fprintf(stderr, "[shader-prep] volume: reading the game's model files for %u listed programs not seen yet: %zu "
                    "files (%.0f MB) in %zu groups, %zu read in earlier sessions\n",
            unknown0, n_files, n_bytes / 1048576.0, todo.size(), n_before);
    // In turns, one file of each group; stop when nothing listed is unknown any more.
    size_t left = n_files, idx = 0;
    double last_line = 0, last_check = 0;
    bool said20 = false;
    std::vector<size_t> pos(todo.size(), 0);
    const char* why = "every file read";
    // The progress line: 20 s after the start, then once a minute, and at the end.
    auto progress = [&](uint32_t unknown, bool force) {
        const double t = now_s();
        if (!force && !(!said20 && t - t0 >= 20.0) && !(last_line && t - last_line >= 60.0)) return;
        said20 = true;
        last_line = t;
        fprintf(stderr, "[shader-prep] volume t=%.0f s: %llu files read (%.0f MB, %.0f MB unpacked), %llu programs "
                        "found (%llu vertex), %u listed still unknown, %zu files left; waited %.0f s for the "
                        "game's loads, %.0f s while a player drove\n",
                t - t0, (unsigned long long)s_st.files, s_st.stored / 1048576.0, s_st.unpacked / 1048576.0,
                (unsigned long long)s_st.found, (unsigned long long)s_st.found_v, unknown, left, s_st.wait_load,
                s_st.wait_drive);
        // Per group: files read / programs found, and the file of the last find (which groups the last
        // programs come from, to order the groups better).
        std::string g;
        char one[160];
        for (size_t i = 0; i < todo.size(); i++) {
            snprintf(one, sizeof one, "%s%s%s %zu/%u%s%s", i ? ", " : "", kGroups[i].dir,
                     kGroups[i].ending[0] ? kGroups[i].ending : "", pos[i], s_gfound[i],
                     s_glast[i].empty() ? "" : " last ", s_glast[i].c_str());
            g += one;
        }
        fprintf(stderr, "[shader-prep] volume groups (files read / programs found): %s\n", g.c_str());
    };
    uint32_t unknown = unknown0;
    while (left) {
        const size_t g = idx++ % todo.size();
        if (pos[g] >= todo[g].size()) continue;
        // The game first: no turn while it reads or while a player drives.
        if (now_s() - last_check >= 1.0) {
            for (;;) {
                progress(unknown, false);
                if (player_driving()) { Sleep(250); s_st.wait_drive += 0.25; continue; }
                const double w0 = now_s();
                if (game_reading()) { s_st.wait_load += now_s() - w0; continue; }
                break;
            }
            last_check = now_s();
        }
        const auto& item = todo[g][pos[g]++];
        left--;
        const uint64_t found_before = s_st.found;
        if (read_file(item.first, item.second) && dl) { fprintf(dl, "%s\n", item.first.c_str()); fflush(dl); }
        if (s_st.found > found_before) {
            s_gfound[g] += (uint32_t)(s_st.found - found_before);
            s_glast[g] = item.first.substr(item.first.rfind('/') + 1);
        }
        unknown = rsx_shader_prep_scan_unknown(nullptr, nullptr);
        progress(unknown, !unknown);
        if (!unknown) { why = "every listed program is known"; break; }
    }
    if (dl) fclose(dl);
    gfree(s_buf); gfree(s_ubuf); gfree(s_ent); gfree(s_state);
    fprintf(stderr, "[shader-prep] volume: done (%s) after %.0f s: %llu files, %.0f MB read, %llu programs found, "
                    "%llu too big, %llu not read\n", why, now_s() - t0, (unsigned long long)s_st.files,
            s_st.stored / 1048576.0, (unsigned long long)s_st.found, (unsigned long long)s_st.too_big,
            (unsigned long long)s_st.failed);
}

DWORD WINAPI starter(LPVOID)
{
    // Off the game's start: the search reads the 32 MB image a few times (well under a second).
    if (!find_functions()) return 0;
    // Started once the shader lists are read (the runtime and the game are up by then); the reader itself
    // waits for the volume (its listing answers once the game has mounted it and read its index).
    for (int i = 0; i < 2400 && !rsx_shader_prep_scan_listed(); i++) Sleep(250);
    if (!rsx_shader_prep_scan_listed()) return 0;
    const uint32_t opd = galloc(16), name = galloc(16), tid = galloc(8);
    if (!opd || !name || !tid) return 0;
    wr32(opd, opd);   // the descriptor's own address stands in for the function (registered below)
    wr32(opd + 4, kToc);
    wr32(opd + 8, 0);
    memcpy(vm_base + name, "volread", 8);
    ppu_register_function(opd, reader_thread);
    ppu_context c;
    memset(&c, 0, sizeof c);
    c.gpr[2] = kToc;
    c.gpr[3] = tid; c.gpr[4] = opd; c.gpr[5] = 0; c.gpr[6] = 3071; c.gpr[7] = 0x100000; c.gpr[8] = 0;
    c.gpr[9] = name;
    if (sys_ppu_thread_create(&c) != 0) fprintf(stderr, "[shader-prep] volume: the reader thread was not created\n");
    return 0;
}

void volread_loaded()
{
    const char* e = getenv("GT5_VOLREAD");
    if (e && atoi(e) == 0) return;
    HANDLE h = CreateThread(nullptr, 64u << 10, starter, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

const int s_registered = (ps3_on_loaded(volread_loaded), 0);

}  // namespace

#endif  // _WIN32
