# Sampling profiler for a running gt5.exe: prof.ps1 -At 130 -Seconds 10 -Out logs\prof_a.txt
# Suspends each thread briefly, records where it is, and writes "tid module rva count" lines
# for profsym.py to name. -At is seconds since the process started.
param([int]$At = 0, [int]$Seconds = 10, [string]$Out = "$(Split-Path $PSScriptRoot -Parent)\logs\prof.txt", [int]$Hz = 40)
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
public static class Gt5Prof {
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(int access, bool inherit, int id);
    [DllImport("kernel32.dll")] static extern uint SuspendThread(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr h);
    [DllImport("kernel32.dll")] static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    public static void Run(int pid, int seconds, int hz, string path) {
        var p = Process.GetProcessById(pid);
        var mods = new List<ProcessModule>();
        foreach (ProcessModule m in p.Modules) mods.Add(m);
        IntPtr raw = Marshal.AllocHGlobal(4096);
        IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
        var hist = new Dictionary<string, int>();
        var cpu0 = new Dictionary<int, double>();
        p.Refresh();
        foreach (ProcessThread t in p.Threads) { try { cpu0[t.Id] = t.TotalProcessorTime.TotalSeconds; } catch {} }
        var end = DateTime.Now.AddSeconds(seconds);
        var handles = new Dictionary<int, IntPtr>();
        while (DateTime.Now < end) {
            p.Refresh();
            foreach (ProcessThread t in p.Threads) {
                IntPtr h;
                if (!handles.TryGetValue(t.Id, out h)) {
                    // SUSPEND_RESUME | GET_CONTEXT | QUERY_INFORMATION
                    h = OpenThread(0x0002 | 0x0008 | 0x0040, false, t.Id);
                    handles[t.Id] = h;
                }
                if (h == IntPtr.Zero) continue;
                if (SuspendThread(h) == 0xFFFFFFFF) continue;
                Marshal.WriteInt32(ctx, 0x30, 0x00100001); // CONTEXT_CONTROL for AMD64
                long rip = 0;
                if (GetThreadContext(h, ctx)) rip = Marshal.ReadInt64(ctx, 0xF8);
                ResumeThread(h);
                if (rip == 0) continue;
                string name = "?"; long rva = rip;
                foreach (var m in mods) {
                    long b = m.BaseAddress.ToInt64();
                    if (rip >= b && rip < b + m.ModuleMemorySize) { name = m.ModuleName; rva = rip - b; break; }
                }
                string k = t.Id + " " + name + " " + rva.ToString("X");
                int n; hist.TryGetValue(k, out n); hist[k] = n + 1;
            }
            System.Threading.Thread.Sleep(1000 / hz);
        }
        p.Refresh();
        using (var w = new StreamWriter(path)) {
            foreach (ProcessThread t in p.Threads) {
                try {
                    double a; cpu0.TryGetValue(t.Id, out a);
                    w.WriteLine("#cpu " + t.Id + " " + ((t.TotalProcessorTime.TotalSeconds - a) / seconds * 100).ToString("F0"));
                } catch {}
            }
            foreach (var kv in hist) w.WriteLine(kv.Key + " " + kv.Value);
        }
        foreach (var h in handles.Values) if (h != IntPtr.Zero) CloseHandle(h);
    }
}
"@
$gt = $null
for ($i = 0; $i -lt 120 -and -not $gt; $i++) { $gt = Get-Process gt5 -ErrorAction SilentlyContinue | Select-Object -First 1; if (-not $gt) { Start-Sleep -Milliseconds 500 } }
if (-not $gt) { "gt5 not running"; exit }
while (((Get-Date) - $gt.StartTime).TotalSeconds -lt $At) { Start-Sleep -Milliseconds 500 }
[Gt5Prof]::Run($gt.Id, $Seconds, $Hz, $Out)
"wrote $Out"
