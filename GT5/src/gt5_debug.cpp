// Debug helpers used by scripts/dbg_trace.py probes.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" unsigned char* vm_base;

static uint32_t rd32(uint32_t ea)
{
    uint32_t v;
    memcpy(&v, vm_base + ea, 4);
    return __builtin_bswap32(v);
}

static int printable(uint32_t ea)
{
    if (ea < 0x10000 || ea >= 0xF0000000u) return 0;
    int n = 0;
    for (; n < 64; n++) {
        unsigned char c = vm_base[ea + n];
        if (!c) break;
        if (c < 0x20 || c > 0x7E) return 0;
    }
    return n >= 2;
}

// A guest char*, or the text of a guest std::string (cap at +0x18, SSO buffer or
// pointer at +4), or "" when ea is neither.
extern "C" const char* gt5_dbgstr(uint64_t a)
{
    const uint32_t ea = (uint32_t)a;
    if (ea < 0x10000 || ea >= 0xF0000000u) return "";
    if (printable(ea)) return (const char*)vm_base + ea;
    const uint32_t cap = rd32(ea + 0x18);
    if (cap >= 15 && cap < 0x1000000) {
        const uint32_t p = cap >= 16 ? rd32(ea + 4) : ea + 4;
        if (printable(p)) return (const char*)vm_base + p;
    }
    return "";
}

// A function entry: arguments, their text, and the first words of the r3 object.
extern "C" void gt5_dbgobj(const char* fn, uint64_t r3, uint64_t r4, uint64_t r5, uint64_t lr, uint64_t tid)
{
    char words[320] = "";   /* 28 words of up to 8 digits and a space */
    const uint32_t ea = (uint32_t)r3;
    if (ea >= 0x10000 && ea < 0xF0000000u) {
        int p = 0;
        for (int i = 0; i < 0x70; i += 4)
            p += snprintf(words + p, sizeof words - p, "%X ", rd32(ea + i));
    }
    fprintf(stderr, "[tr %s] ENTER tid=%llu r3=%llX r4=%llX r5=%llX lr=%llX s3=[%s] s4=[%s] s5=[%s] obj: %s\n", fn,
            (unsigned long long)tid, (unsigned long long)r3, (unsigned long long)r4, (unsigned long long)r5,
            (unsigned long long)lr, gt5_dbgstr(r3), gt5_dbgstr(r4), gt5_dbgstr(r5), words);
}

// scripts/dbg_adc.py: append each stream read to a file named after the script
// the loader last opened. A new script starts its file over.
static char s_adc_path[256];
static int s_adc_new;
extern "C" void gt5_adc_path(uint64_t path)
{
    strncpy(s_adc_path, gt5_dbgstr(path), sizeof s_adc_path - 1);
    s_adc_new = 1;
}
extern "C" void gt5_dump_adc(uint64_t buf, uint64_t got, uint64_t tid)
{
    const uint32_t n = (uint32_t)got;
    if (tid != 1 || !s_adc_path[0] || n == 0 || n > 0x4000000 || (uint32_t)buf < 0x10000) return;
    static FILE* f;
    if (s_adc_new) {
        s_adc_new = 0;
        if (f) fclose(f);
        char name[300] = "E:/Games/gt5-recomp/GT5/analysis/adc/";
        char* out = name + strlen(name);
        for (const char* p = s_adc_path; *p && out < name + sizeof name - 8; p++)
            *out++ = (*p == '/') ? '_' : *p;
        strcpy(out, ".bin");
        f = fopen(name, "wb");
    }
    if (f) { fwrite(vm_base + (uint32_t)buf, 1, n, f); fflush(f); }
}

// scripts/dbg_native.py: first calls raw, to learn the method object's layout.
extern "C" void gt5_log_native(uint64_t obj, uint64_t r4, uint64_t r5, uint64_t tid)
{
    static long n;
    if (n++ > 400 || tid != 1) return;
    const uint32_t ea = (uint32_t)obj;
    fprintf(stderr, "[nat] obj=%X r4=%llX r5=%llX:", ea, (unsigned long long)r4, (unsigned long long)r5);
    for (int i = 0; i < 0x30; i += 4) {
        const uint32_t w = rd32(ea + i);
        fprintf(stderr, " %X", w);
        const char* s = gt5_dbgstr(w);
        if (s[0]) fprintf(stderr, "[%.32s]", s);
        else if (w >= 0x10000 && w < 0xF0000000u) {
            const char* s2 = gt5_dbgstr(rd32(w));
            if (s2[0]) fprintf(stderr, "[*%.32s]", s2);
        }
    }
    fputc('\n', stderr);
}

// Any text reachable from a guest pointer within two hops, to find a symbol's layout.
static int plausible(uint32_t ea) { return ea >= 0x10000 && ea < 0xF0000000u; }
static void deep(uint32_t v, int depth, int* left)
{
    if (!plausible(v) || depth > 2) return;
    for (int i = 0; i < 0x40 && *left > 0; i += 4) {
        const uint32_t w = rd32(v + i);
        if (!plausible(w)) continue;
        const char* s = gt5_dbgstr(w);
        if (s[0]) { fprintf(stderr, " %d:+%X=[%.40s]", depth, i, s); --*left; continue; }
        deep(w, depth + 1, left);
    }
}
extern "C" void gt5_dbgdeep(const char* tag, uint64_t a, uint64_t b, uint64_t c, uint64_t tid)
{
    static long n;
    if (n++ > 3000) return;
    fprintf(stderr, "[deep %s] tid=%llu", tag, (unsigned long long)tid);
    int left = 8;
    deep((uint32_t)a, 0, &left);
    fprintf(stderr, " |");
    left = 8;
    deep((uint32_t)b, 0, &left);
    fprintf(stderr, " |");
    left = 8;
    deep((uint32_t)c, 0, &left);
    fputc('\n', stderr);
}

// scripts/dbg_native.py: named script calls, repeats collapsed per thread.
#include <time.h>
extern "C" void gt5_log_call(const char* kind, uint64_t fn, uint64_t tid)
{
    const uint32_t ea = (uint32_t)fn;
    if (!plausible(ea)) return;
    const uint32_t n = rd32(ea + 0xC);
    const char* s = plausible(n) ? gt5_dbgstr(n + 4) : "";
    static thread_local char last[80];
    static thread_local long rep;
    if (!strcmp(last, s)) { rep++; return; }
    fprintf(stderr, "[sc %llu %ldms %s] %.70s (prev x%ld)\n", (unsigned long long)tid,
            (long)(clock() * 1000 / CLOCKS_PER_SEC), kind, s, rep + 1);
    strncpy(last, s, sizeof last - 1);
    rep = 0;
}

// scripts/dbg_throw.py: the type thrown, from its typeinfo name, and any text in the object.
extern "C" void gt5_log_throw(uint64_t obj, uint64_t ti, uint64_t tid)
{
    const uint32_t name = plausible((uint32_t)ti) ? rd32((uint32_t)ti + 4) : 0;
    fprintf(stderr, "[throw] tid=%llu %ldms type=%s obj=%X:", (unsigned long long)tid,
            (long)(clock() * 1000 / CLOCKS_PER_SEC), plausible(name) ? gt5_dbgstr(name) : "?", (uint32_t)obj);
    int left = 6;
    deep((uint32_t)obj, 0, &left);
    fputc('\n', stderr);
}

// The SQL of a sqlite3_stmt: Vdbe keeps zSql somewhere in its first few hundred bytes.
extern "C" void gt5_log_sql(uint64_t stmt, uint64_t tid)
{
    const uint32_t v = (uint32_t)stmt;
    if (!plausible(v)) return;
    static const char* const kw[] = {"SELECT", "INSERT", "CREATE", "UPDATE", "DELETE", "BEGIN", "COMMIT",
                                     "PRAGMA", "DROP", "REPLACE", "END", "ROLLBACK", "select", "insert", "create", "update"};
    for (int i = 0; i < 0x200; i += 4) {
        const uint32_t w = rd32(v + i);
        if (!plausible(w)) continue;
        const char* t = (const char*)vm_base + w;
        for (const char* k : kw) {
            if (strncmp(t, k, strlen(k)) == 0) {
                fprintf(stderr, "[sql %llu %ldms] %.300s\n", (unsigned long long)tid,
                        (long)(clock() * 1000 / CLOCKS_PER_SEC), t);
                return;
            }
        }
    }
}

// Why the game's heap said no: walk its sorted table of used page runs (8 KB pages) and report the gaps.
static uint32_t rd16u(uint32_t ea) { return (uint32_t)vm_base[ea] << 8 | vm_base[ea + 1]; }
extern "C" void gt5_heap_oom(uint64_t lr, uint64_t tid)
{
    static volatile long n = 0;
    if (n >= 3) return;
    n = n + 1;
    const uint32_t arena = rd32(0x018793BC), p = arena + 0x25D0, mt = rd32(p + 4);
    const uint32_t cnt = rd32(p + 0xC);
    fprintf(stderr, "[heap-oom] tid=%llu lr=0x%08X arena=0x%08X base=0x%08X mt=0x%08X runs=%u\n",
            (unsigned long long)tid, (uint32_t)lr, arena, rd32(arena + 0x30), mt, cnt);
    fprintf(stderr, "[heap-oom] p: %08X %08X %08X %08X %08X  mt: %08X %08X %08X\n",
            rd32(p), rd32(p + 4), rd32(p + 8), rd32(p + 0xC), rd32(p + 0x10), rd32(mt), rd32(mt + 4), rd32(mt + 8));
    uint32_t bits = 0;
    for (uint32_t i = 0; i < 0x40; i += 4) { uint32_t w = rd32(mt + 0x10 + i); while (w) { bits += w & 1; w >>= 1; } }
    fprintf(stderr, "[heap-oom] mapped 1MB blocks (first 64 bytes of bitmap): %u\n", bits);
    uint32_t prev = 0, used = 0, gap = 0, big = 0;
    for (uint32_t i = 0; i < cnt && i < 20000; ++i) {
        const uint32_t e = rd32(p + 0x10 + (i >> 9) * 4) + (i & 0x1FF) * 8;
        const uint32_t a = rd16u(e), b = rd16u(e + 2);
        if (i < 24 || i + 8 > cnt) fprintf(stderr, "[heap-oom]  run %u: %04X-%04X (%u KB) tag=%08X\n", i, a, b, (b - a + 1) * 8, rd32(e + 4));
        used += b - a + 1;
        if (i && a > prev + 1) { gap += a - prev - 1; if (a - prev - 1 > big) big = a - prev - 1; }
        prev = b;
    }
    fprintf(stderr, "[heap-oom] used %u KB, free in gaps %u KB, largest gap %u KB\n", used * 8, gap * 8, big * 8);
    if (n == 1) {
        static const uint32_t lim[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 1024, 0x10000};
        uint32_t hc[11] = {0}, hp[11] = {0}, top_a[40] = {0}, top_s[40] = {0};
        for (uint32_t i = 0; i < cnt && i < 20000; ++i) {
            const uint32_t e = rd32(p + 0x10 + (i >> 9) * 4) + (i & 0x1FF) * 8;
            const uint32_t a = rd16u(e), b = rd16u(e + 2);
            if (a > 0x7FFF || b < a) continue;
            const uint32_t s = b - a + 1;
            for (int k = 0; k < 11; ++k) if (s <= lim[k]) { hc[k]++; hp[k] += s; break; }
            for (int k = 0; k < 40; ++k) if (s > top_s[k]) {
                for (int m = 39; m > k; --m) { top_s[m] = top_s[m - 1]; top_a[m] = top_a[m - 1]; }
                top_s[k] = s; top_a[k] = a; break;
            }
        }
        for (int k = 0; k < 11; ++k)
            fprintf(stderr, "[heap-hist] runs up to %5u pages: %5u runs, %7u KB\n", lim[k], hc[k], hp[k] * 8);
        for (int k = 0; k < 40 && top_s[k]; ++k) {
            const uint32_t ea = rd32(arena + 0x30) + top_a[k] * 0x2000;
            char txt[49];
            for (int c = 0; c < 48; ++c) { const unsigned char ch = vm_base[ea + c]; txt[c] = (ch >= 32 && ch < 127) ? ch : '.'; }
            txt[48] = 0;
            fprintf(stderr, "[heap-top] %7u KB at 0x%08X: %08X %08X %08X %08X |%s|\n", top_s[k] * 8, ea,
                    rd32(ea), rd32(ea + 4), rd32(ea + 8), rd32(ea + 12), txt);
        }
    }
    fflush(stderr);
}

extern "C" void gt5_heap_big(uint64_t pages, uint64_t align, uint64_t tid)
{
    fprintf(stderr, "[heap-big %ldms] %llu KB align %llu tid=%llu\n", (long)(clock() * 1000 / CLOCKS_PER_SEC),
            (unsigned long long)((uint32_t)pages * 8), (unsigned long long)(uint32_t)align, (unsigned long long)tid);
}


// The music reader thread: its file, the open result, and how far it read.
extern "C" void gt5_log_bgm(const char* what, uint64_t self, uint64_t r3, uint64_t tid)
{
    const uint32_t o = (uint32_t)self;
    char txt[81];
    for (int i = 0; i < 80; ++i) { const unsigned char c = vm_base[o + 0x1B0 + i]; txt[i] = (c >= 32 && c < 127) ? c : '.'; }
    txt[80] = 0;
    fprintf(stderr, "[bgm %s %ldms] tid=%llu this=%08X r3=%08X flags a5=%u a6=%u a7=%u a8=%u size=%08X%08X pos=%08X%08X |%s|\n", what,
            (long)(clock() * 1000 / CLOCKS_PER_SEC), (unsigned long long)tid, o, (uint32_t)r3,
            vm_base[o + 0x1A5], vm_base[o + 0x1A6], vm_base[o + 0x1A7], vm_base[o + 0x1A8],
            rd32(o + 0x2C0), rd32(o + 0x2C4), rd32(o + 0x2D0), rd32(o + 0x2D4), txt);
    const uint32_t p = rd32(o + 0x1B0);
    if (p >= 0x10000 && p < 0xF0000000u) fprintf(stderr, "[bgm %s] *path=%.120s\n", what, (const char*)vm_base + p);
}


// Audio thread 19: each completion event, and the job list state it indexes with it.
extern "C" void gt5_log_aud(uint64_t r3, uint64_t r4, uint64_t r5, uint64_t r6, uint64_t r7, uint64_t self)
{
    static volatile long n = 0;
    if (n >= 60) return;
    n = n + 1;
    const uint32_t o = (uint32_t)self;
    fprintf(stderr, "[aud] rc=%08X src=%016llX d1=%016llX d2=%016llX d3=%016llX this=%08X n4280=%08X n4284=%08X n4A98=%08X n4A9C=%08X q=%08X port=%02X\n",
            (uint32_t)r3, (unsigned long long)r4, (unsigned long long)r5, (unsigned long long)r6, (unsigned long long)r7, o,
            rd32(o + 0x4280), rd32(o + 0x4284), rd32(o + 0x4A98), rd32(o + 0x4A9C), rd32(o + 0x218), vm_base[o + 0x21C]);
}
