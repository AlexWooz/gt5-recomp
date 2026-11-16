// Thread load: how busy each kind of thread was, written to the log (play.err) every 5 seconds:
//
//   [threads] 5 s, 100 = one core busy: ppu main 87 | spu jobs 152 (6, top 61) | rsx walker 64 | ...
//             | whole game 369 of 1600, busiest thread: ppu main 87
//
// 100 is one CPU core kept busy for the whole period. A kind with several threads shows its total,
// how many threads, and its busiest one. A single thread near 100 is the one holding the frame rate
// back: no other core can take over its work. The kinds come from the names the runtime gives its
// threads ("ppu main", "spurs jc 0", "audio mix", ...): "ppu" are the game's own threads, "spu jobs"
// the PS3's SPU work (GT5 runs it through SPURS), "rsx walker" the thread that turns the game's drawing
// commands into the renderer's and presents the frames, "rsx record" the one that records them for
// DirectX 12; threads without a name (the graphics driver's, Windows') count as "other".
//
// PS3_THREAD_STATS=0 turns it off, PS3_THREAD_STATS=<seconds> sets the period (1 to 60).
// Windows only: elsewhere the runtime doesn't name its threads.
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void ps3_on_loaded(void (*fn)(void));
extern "C" volatile unsigned long g_gcm_drain_tid;   // cellGcmSys.c: the thread that last walked the FIFO

namespace {

typedef HRESULT(WINAPI* get_desc_fn)(HANDLE, PWSTR*);
typedef HRESULT(WINAPI* set_desc_fn)(HANDLE, PCWSTR);

struct Kind {
    const char* label;
    const char* prefix;   // thread name starts with this
};
// First match wins.
const Kind kKinds[] = {
    {"ppu main", "ppu main"},
    {"ppu other", "ppu "},
    {"spu jobs", "spu job"},
    {"spu jobs", "spurs "},   // GT5 runs its SPU jobs through SPURS
    {"spu threads", "spu"},
    {"spu threads", "raw spu"},
    {"rsx record", "rsx record"},
    {"shader compile", "rsx shader compile"},
    {"shader compile", "vk-shaders"},
    {"shader compile", "vk-prebuild"},
    {"vk present", "vk-present"},
    {"sound", "audio mix"},
    {"decoders", "cellAdec"},
    {"decoders", "cellDmux"},
    {"decoders", "cellVdec"},
};
const int kNumKinds = sizeof kKinds / sizeof kKinds[0];
const char* const kWalker = "rsx walker";
const char* const kOther = "other";

struct Thread {
    DWORD tid;
    HANDLE h;
    uint64_t last;   // kernel + user, 100 ns
    bool seen;
};
const int kMaxThreads = 1024;
Thread s_threads[kMaxThreads];
int s_count;

struct Sum {
    const char* label;
    double total, top;
    int n;
};

uint64_t ft64(const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

const char* kind_of(DWORD tid, HANDLE h, get_desc_fn get_desc)
{
    char name[64] = "";
    PWSTR w = NULL;
    if (get_desc && SUCCEEDED(get_desc(h, &w)) && w) {
        int i = 0;
        for (; w[i] && i < 63; i++) name[i] = (char)(w[i] < 128 ? w[i] : '?');
        name[i] = 0;
        LocalFree(w);
    }
    if (tid == g_gcm_drain_tid && strncmp(name, "ppu", 3) != 0) return kWalker;
    for (int k = 0; k < kNumKinds; k++)
        if (strncmp(name, kKinds[k].prefix, strlen(kKinds[k].prefix)) == 0) return kKinds[k].label;
    return kOther;
}

void add(Sum* sums, int* nsums, const char* label, double v)
{
    int i = 0;
    while (i < *nsums && strcmp(sums[i].label, label) != 0) i++;
    if (i == *nsums) {
        if (*nsums >= 32) return;
        sums[i] = Sum{label, 0.0, 0.0, 0};
        (*nsums)++;
    }
    sums[i].total += v;
    sums[i].n++;
    if (v > sums[i].top) sums[i].top = v;
}

DWORD WINAPI stats_thread(LPVOID arg)
{
    const DWORD period_ms = (DWORD)(uintptr_t)arg;
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    get_desc_fn get_desc = (get_desc_fn)(void*)GetProcAddress(k32, "GetThreadDescription");
    set_desc_fn set_desc = (set_desc_fn)(void*)GetProcAddress(k32, "SetThreadDescription");
    if (set_desc) set_desc(GetCurrentThread(), L"thread stats");
    const DWORD pid = GetCurrentProcessId();
    const DWORD cores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    fprintf(stderr, "[threads] every %lu s: the CPU each kind of thread used, 100 = one core busy the whole"
                    " time, %lu cores here (PS3_THREAD_STATS=0 turns this off)\n",
            (unsigned long)(period_ms / 1000), (unsigned long)cores);

    LARGE_INTEGER freq, t_last, t_now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_last);
    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);
    uint64_t period_start = ft64(now_ft);

    for (bool first = true;; first = false) {
        Sum sums[32];
        int nsums = 0;
        double whole = 0.0;
        double busiest = 0.0;
        const char* busiest_label = kOther;
        QueryPerformanceCounter(&t_now);
        const double wall_100ns = (double)(t_now.QuadPart - t_last.QuadPart) * 1e7 / (double)freq.QuadPart;
        t_last = t_now;

        for (int i = 0; i < s_count; i++) s_threads[i].seen = false;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te;
            te.dwSize = sizeof te;
            for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                if (te.th32OwnerProcessID != pid) continue;
                int i = 0;
                while (i < s_count && s_threads[i].tid != te.th32ThreadID) i++;
                bool is_new = false;
                if (i == s_count) {
                    if (s_count >= kMaxThreads) continue;
                    HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
                    if (!h) continue;
                    s_threads[i] = Thread{te.th32ThreadID, h, 0, false};
                    s_count++;
                    is_new = true;
                }
                Thread& t = s_threads[i];
                t.seen = true;
                FILETIME c, e, k, u;
                if (!GetThreadTimes(t.h, &c, &e, &k, &u)) continue;
                const uint64_t used = ft64(k) + ft64(u);
                // A thread started during the period: all of its time is in the period.
                uint64_t delta = 0;
                if (!is_new) delta = used - t.last;
                else if (!first && ft64(c) >= period_start) delta = used;
                t.last = used;
                if (first) continue;
                const double v = wall_100ns > 0.0 ? 100.0 * (double)delta / wall_100ns : 0.0;
                const char* label = kind_of(t.tid, t.h, get_desc);
                add(sums, &nsums, label, v);
                whole += v;
                if (v > busiest) {
                    busiest = v;
                    busiest_label = label;
                }
            }
            CloseHandle(snap);
        }
        // Threads that ended: let their slots go.
        for (int i = 0; i < s_count;) {
            if (s_threads[i].seen) {
                i++;
                continue;
            }
            CloseHandle(s_threads[i].h);
            s_threads[i] = s_threads[--s_count];
        }
        GetSystemTimeAsFileTime(&now_ft);
        period_start = ft64(now_ft);

        if (!first) {
            // Biggest first.
            for (int a = 0; a < nsums; a++)
                for (int b = a + 1; b < nsums; b++)
                    if (sums[b].total > sums[a].total) {
                        Sum s = sums[a];
                        sums[a] = sums[b];
                        sums[b] = s;
                    }
            char line[1024];
            int len = snprintf(line, sizeof line, "[threads] %lu s, 100 = one core busy:",
                               (unsigned long)(period_ms / 1000));
            int shown = 0;
            for (int i = 0; i < nsums && len < (int)sizeof line - 80; i++) {
                const Sum& s = sums[i];
                if (s.total < 0.5 && s.n == 1) continue;
                if (s.n == 1)
                    len += snprintf(line + len, sizeof line - len, "%s %s %.0f", shown ? " |" : "", s.label,
                                    s.total);
                else
                    len += snprintf(line + len, sizeof line - len, "%s %s %.0f (%d, top %.0f)",
                                    shown ? " |" : "", s.label, s.total, s.n, s.top);
                shown++;
            }
            snprintf(line + len, sizeof line - len, " | whole game %.0f of %lu, busiest thread: %s %.0f\n",
                     whole, (unsigned long)(cores * 100), busiest_label, busiest);
            fputs(line, stderr);
        }
        Sleep(period_ms);
    }
    return 0;
}

void gt5_threadstats_start()
{
    long seconds = 5;
    if (const char* e = getenv("PS3_THREAD_STATS")) {
        if (*e) seconds = strtol(e, NULL, 10);
    }
    if (seconds <= 0) return;
    if (seconds > 60) seconds = 60;
    HANDLE h = CreateThread(NULL, 64 * 1024, stats_thread, (LPVOID)(uintptr_t)(seconds * 1000), 0, NULL);
    if (h) CloseHandle(h);
}

const int s_registered = (ps3_on_loaded(gt5_threadstats_start), 0);

}  // namespace
#endif
