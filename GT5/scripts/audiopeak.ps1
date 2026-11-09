# The Windows peak meter for gt5.exe's own audio session: audiopeak.ps1 -At 60 -Seconds 15
# Reads what actually reaches the output device from this process, ignoring other apps.
param([int]$At = 60, [int]$Seconds = 15)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom {}
[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator {
    int EnumAudioEndpoints(int dataFlow, int mask, out IntPtr devices);
    int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
}
[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice {
    int Activate(ref Guid iid, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o);
}
[ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionManager2 {
    int GetAudioSessionControl(IntPtr a, int b, out IntPtr c);
    int GetSimpleAudioVolume(IntPtr a, int b, out IntPtr c);
    int GetSessionEnumerator(out IAudioSessionEnumerator e);
}
[ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionEnumerator {
    int GetCount(out int n);
    int GetSession(int i, [MarshalAs(UnmanagedType.IUnknown)] out object s);
}
[ComImport, Guid("BFB7FF88-7239-4FC9-8FA2-07C950BE9C6D"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionControl2 {
    int a0(); int a1(); int a2(); int a3(); int a4(); int a5(); int a6(); int a7(); int a8();
    int GetSessionIdentifier(out IntPtr s);
    int GetSessionInstanceIdentifier(out IntPtr s);
    int GetProcessId(out uint pid);
}
[ComImport, Guid("C02216F6-8C67-4B5B-9D00-D008E73E0064"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioMeterInformation { int GetPeakValue(out float peak); }
public static class Gt5Meter {
    static IAudioMeterInformation meter;
    public static bool Find(uint pid) {
        var en = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
        IMMDevice dev; en.GetDefaultAudioEndpoint(0, 1, out dev);
        var iid = new Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F");
        object o; dev.Activate(ref iid, 23, IntPtr.Zero, out o);
        IAudioSessionEnumerator se; ((IAudioSessionManager2)o).GetSessionEnumerator(out se);
        int n; se.GetCount(out n);
        for (int i = 0; i < n; i++) {
            object s; se.GetSession(i, out s);
            uint p; ((IAudioSessionControl2)s).GetProcessId(out p);
            if (p == pid) { meter = (IAudioMeterInformation)s; return true; }
        }
        return false;
    }
    public static float Peak() { float v; meter.GetPeakValue(out v); return v; }
}
"@
$gt = $null
for ($i = 0; $i -lt 60 -and -not $gt; $i++) { $gt = Get-Process gt5 -ErrorAction SilentlyContinue | Select-Object -First 1; if (-not $gt) { Start-Sleep -Milliseconds 500 } }
if (-not $gt) { "gt5 not running"; exit }
while (((Get-Date) - $gt.StartTime).TotalSeconds -lt $At) { Start-Sleep -Milliseconds 500 }
if (-not [Gt5Meter]::Find([uint32]$gt.Id)) { "no audio session found for gt5 pid $($gt.Id)"; exit }
$max = 0.0; $end = (Get-Date).AddSeconds($Seconds); $n = 0
while ((Get-Date) -lt $end) { $v = [Gt5Meter]::Peak(); if ($v -gt $max) { $max = $v }; $n++; Start-Sleep -Milliseconds 50 }
"device peak for gt5.exe over $Seconds s: $max ($n samples)"
