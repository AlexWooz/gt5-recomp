# Send gt5.exe's visible windows to the back of the desktop without taking focus.
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class Gt5Window {
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    public static List<string> Back(uint target) {
        var done = new List<string>();
        EnumWindows((h, l) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid == target && IsWindowVisible(h)) {
                // HWND_BOTTOM, with NOSIZE | NOMOVE | NOACTIVATE
                SetWindowPos(h, new IntPtr(1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010);
                var s = new StringBuilder(128); GetWindowText(h, s, 128);
                done.Add(s.ToString());
            }
            return true;
        }, IntPtr.Zero);
        return done;
    }
}
"@
Get-Process gt5 -ErrorAction SilentlyContinue | ForEach-Object {
    $w = [Gt5Window]::Back([uint32]$_.Id)
    "pid $($_.Id): moved $($w.Count) window(s) to the back: $($w -join ', ')"
}
