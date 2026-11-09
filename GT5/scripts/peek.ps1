# Read a running gt5.exe from outside: the guest registers of a spinning host thread, and guest memory.
#   peek.ps1 -Thread 38916 -Ea 0x60000000,64   (Thread: host thread id, from profsym.py; Ea: address,bytes pairs)
# Finds the ppu_context by looking for the guest TOC in the memory a host register points at.
param([int]$Thread = 0, [string[]]$Ea = @(), [switch]$Scan, [string]$Dump = "")
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
public static class Gt5Peek {
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(int a, bool i, int id);
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(int a, bool i, int id);
    [DllImport("kernel32.dll")] static extern uint SuspendThread(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr h);
    [DllImport("kernel32.dll")] static extern bool GetThreadContext(IntPtr h, IntPtr c);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualQueryEx(IntPtr p, IntPtr a, byte[] b, IntPtr n);
    public static IntPtr P;
    public static byte[] Read(long a, int n) {
        var b = new byte[n]; IntPtr r;
        if (!ReadProcessMemory(P, new IntPtr(a), b, new IntPtr(n), out r) || r.ToInt64() != n) return null;
        return b;
    }
    public static long Base() {
        // The guest address space: the largest single reservation in the process.
        long a = 0, best = 0, bestSize = 0, curBase = 0, curSize = 0;
        var m = new byte[48];
        while (a < 0x7FFFFFFFFFFFL) {
            if (VirtualQueryEx(P, new IntPtr(a), m, new IntPtr(48)) == IntPtr.Zero) break;
            long alloc = BitConverter.ToInt64(m, 8), size = BitConverter.ToInt64(m, 24); int state = BitConverter.ToInt32(m, 32);
            if (state != 0x10000) {
                if (alloc == curBase) curSize += size; else { curBase = alloc; curSize = size; }
                if (curSize > bestSize) { bestSize = curSize; best = curBase; }
            }
            a = BitConverter.ToInt64(m, 0) + size;
        }
        Console.WriteLine("largest allocation 0x" + best.ToString("X") + " size 0x" + bestSize.ToString("X"));
        return best;
    }
    public static long[] Regs(int tid) {
        IntPtr h = OpenThread(0x0002 | 0x0008 | 0x0040, false, tid);
        IntPtr raw = Marshal.AllocHGlobal(4096);
        IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
        SuspendThread(h);
        Marshal.WriteInt32(ctx, 0x30, 0x00100003);
        GetThreadContext(h, ctx);
        ResumeThread(h);
        var v = new long[16];
        for (int i = 0; i < 16; i++) v[i] = Marshal.ReadInt64(ctx, 0x78 + i * 8);
        return v;
    }
    // Every ppu_context in the process: gpr[1] a guest stack, gpr[2] one of the TOCs.
    public static void Scan(long[] toc) {
        long a = 0x10000;
        var m = new byte[48];
        while (a < 0x7FFFFFFFFFFFL) {
            if (VirtualQueryEx(P, new IntPtr(a), m, new IntPtr(48)) == IntPtr.Zero) break;
            long bas = BitConverter.ToInt64(m, 0), size = BitConverter.ToInt64(m, 24);
            int state = BitConverter.ToInt32(m, 32), type = BitConverter.ToInt32(m, 40), prot = BitConverter.ToInt32(m, 36);
            if (state == 0x1000 && (prot & 0x04) != 0 && size < 0x10000000) {
                var b = Read(bas, (int)size);
                if (b != null) for (int o = 8; o + 248 <= b.Length; o += 8) {
                    long g2 = BitConverter.ToInt64(b, o + 8);
                    if (Array.IndexOf(toc, g2) < 0) continue;
                    long g1 = BitConverter.ToInt64(b, o);
                    if (g1 < 0xC0000000L || g1 > 0xF0000000L) continue;
                    var t = new System.Text.StringBuilder("ctx 0x" + (bas + o - 8).ToString("X"));
                    for (int r = 0; r < 32; r++) t.Append(" r" + r + "=" + BitConverter.ToInt64(b, o - 8 + r * 8).ToString("X"));
                    System.IO.File.AppendAllText(@"$(Split-Path $PSScriptRoot -Parent)\logs\peek.txt", t.ToString() + System.Environment.NewLine);
                }
            }
            a = bas + size;
        }
    }
    public static long Find(byte[] sig) {
        long a = 0x10000;
        var m = new byte[48];
        while (a < 0x7FFFFFFFFFFFL) {
            if (VirtualQueryEx(P, new IntPtr(a), m, new IntPtr(48)) == IntPtr.Zero) break;
            long bas = BitConverter.ToInt64(m, 0), size = BitConverter.ToInt64(m, 24);
            int state = BitConverter.ToInt32(m, 32), prot = BitConverter.ToInt32(m, 36);
            if (state == 0x1000 && (prot & 0x66) != 0 && size >= 0x10000 && size < 0x20000000) {
                var b = Read(bas, (int)size);
                if (b != null) for (int o = 0; o + 32 <= b.Length; o += 16) {
                    if (b[o] != sig[0] || b[o + 1] != sig[1] || b[o + 2] != sig[2]) continue;
                    int k = 3; while (k < 32 && b[o + k] == sig[k]) k++;
                    if (k == 32 && ((bas + o - 0x177F200) & 0xFFFF) == 0) return bas + o;
                }
            }
            a = bas + size;
        }
        return 0;
    }
    public static void Open(int pid) { P = OpenProcess(0x0410, false, pid); }
}
"@
$gt = Get-Process gt5 | Select-Object -First 1
[Gt5Peek]::Open($gt.Id)
$base = [Gt5Peek]::Base()
# Guest address 0: where the Wws_Job module's signature sits, 0x177F200 into the guest image.
$elf = [System.IO.File]::ReadAllBytes("$(Split-Path $PSScriptRoot -Parent)\input\EBOOT.elf")
$sig = New-Object byte[] 32
[Array]::Copy($elf, 0x177F200 - 0x10000, $sig, 0, 32)
$hit = [Gt5Peek]::Find($sig)
if ($hit) { $base = $hit - 0x177F200; "guest address 0 is host 0x{0:X}" -f $base } else { "signature not found" }
function BE32($b, $o) { return ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o+1] -shl 16) -bor ([uint32]$b[$o+2] -shl 8) -bor [uint32]$b[$o+3] }
if ($Thread) {
    $toc = 0x1846AF0, 0x1866624, 0x18765B0, 0x185682C
    $found = $false
    foreach ($r in [Gt5Peek]::Regs($Thread)) {
        if ($r -lt 0x10000) { continue }
        for ($back = 0; $back -le 0x800 -and -not $found; $back += 8) {
            $b = [Gt5Peek]::Read($r - $back, 0x100)
            if (-not $b) { break }
            $g2 = [BitConverter]::ToInt64($b, 16)
            $g1 = [BitConverter]::ToInt64($b, 8)
            if (($toc -contains $g2) -and $g1 -gt 0x10000000 -and $g1 -lt 0xF0000000 -and ($g1 % 16) -eq 0) {
                $found = $true
                "ppu_context at 0x{0:X}" -f ($r - $back)
                for ($i = 0; $i -lt 32; $i++) { "r{0} = 0x{1:X}" -f $i, [BitConverter]::ToInt64($b, $i * 8) }
            }
        }
        if ($found) { break }
    }
    if (-not $found) { "no ppu_context found" }
}
if ($Scan) { [Gt5Peek]::Scan([long[]](0x1846AF0, 0x1866624, 0x18765B0, 0x185682C)) }
foreach ($pair in $Ea) {
    $p = $pair -split ","
    $addr = [Convert]::ToInt64($p[0], 16)
    $len = [int]$p[1]
    $b = [Gt5Peek]::Read($base + $addr, $len)
    if (-not $b) { "0x{0:X}: unreadable" -f $addr; continue }
    if ($Dump) { [System.IO.File]::WriteAllBytes($Dump, $b); "wrote $Dump"; continue }
    for ($o = 0; $o -lt $len; $o += 16) {
        $n = [Math]::Min(16, $len - $o)
        "{0:X8}: {1}" -f ($addr + $o), (($b[$o..($o + $n - 1)] | ForEach-Object { $_.ToString("X2") }) -join "")
    }
}
