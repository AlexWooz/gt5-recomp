// GT5 Launcher: settings, play, updates and help for the Gran Turismo 5 PC recompilation.
// Built on your PC by launcher\build-launcher.ps1 with the C# compiler that comes with Windows
// (.NET Framework 4), so nothing prebuilt is downloaded. C# 5 only: that is what that compiler takes.
//
// Settings are written to GT5\settings.bat, which GT5\play.bat runs after its own defaults, so the
// launcher and play.bat always start the game the same way. Pictures come from your own disc
// (GT5\vfs\PS3_GAME\ICON0.PNG); nothing from the game is part of this file.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.Eventing.Reader;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.IO;
using System.Net;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;

[assembly: AssemblyTitle("GT5 Launcher")]
[assembly: AssemblyProduct("Gran Turismo 5 PC recompilation")]
[assembly: AssemblyVersion("1.0.0.0")]

namespace Gt5Launcher
{
    static class Program
    {
        [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr value);

        [STAThread]
        static void Main(string[] args)
        {
            // The layout is in 100%-scaling pixels. As a DPI-aware program only its text followed the
            // screen's scaling, so on a 4K screen at 200% the text outgrew its boxes and the window was cut
            // off (discussion #9). DPI-unaware with GDI scaling instead (-5): Windows draws the whole window
            // at the screen's scaling and keeps the text sharp (Windows 10 1809 and later; older versions
            // stretch it, or lack the call and stay unaware, which also fits).
            try { SetProcessDpiAwarenessContext(new IntPtr(-5)); } catch { }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            if (args.Length >= 2 && args[0] == "--screenshot")
            {
                // Every page as a PNG in the given folder (for the README and for testing), then exit.
                // A third argument "online" checks GitHub first, to show the Updates page filled in.
                bool online = args.Length > 2 && args[2] == "online";
                var f = new LauncherForm(!online);
                f.StartPosition = FormStartPosition.Manual; f.Location = new Point(-4000, -4000);
                f.Show(); Application.DoEvents();
                if (online) { var until = DateTime.Now.AddSeconds(8); while (DateTime.Now < until) { Application.DoEvents(); Thread.Sleep(50); } }
                f.SaveScreenshots(args[1]);
                return;
            }
            // --save-report [folder]: the report of the last run (ProblemReport) as a ZIP in that folder (else on the
            // desktop), with no window: for testing, and for a helper that collects a report.
            if (args.Length >= 1 && args[0] == "--save-report")
            {
                var p = new Paths();
                try { ProblemReport.Save(p, ProblemReport.Classify(Path.Combine(p.Logs, "play.err"), null, -1), args.Length > 1 ? args[1] : null, null); } catch { }
                return;
            }
            // --update-window <install folder> <launcher> [update.ps1 arguments]: the update window (#39), run
            // by "Update now" from a copy of this exe, so the build can replace GT5Launcher.exe meanwhile.
            if (args.Length >= 3 && args[0] == "--update-window")
            {
                var rest = new string[args.Length - 3];
                Array.Copy(args, 3, rest, 0, rest.Length);
                Application.Run(new UpdateForm(args[1], args[2], rest));
                return;
            }
            // One launcher per install: a second start brings the open one to the front instead (two would each write
            // settings.bat over the other's changes, and an update can't replace an open one).
            bool first;
            string dir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\').ToLowerInvariant();
            uint h = 2166136261;   // FNV-1a of the folder: the same name in every run
            foreach (char c in dir) { h ^= c; h *= 16777619; }
            one = new Mutex(true, "Local\\GT5Launcher-" + h.ToString("x8"), out first);
            if (!first) { ShowOther(dir); return; }
            // --play: press Play straight away (a "play directly" shortcut).
            var form = new LauncherForm(false);
            if (Array.IndexOf(args, "--play") >= 0) form.Shown += delegate { form.PlayNow(); };
            Application.Run(form);
            GC.KeepAlive(one);
        }

        static Mutex one;
        [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
        [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int cmd);
        [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);

        // The launcher already open from this folder (also one an update renamed while it ran): to the front.
        static void ShowOther(string dir)
        {
            try
            {
                int me = Process.GetCurrentProcess().Id;
                foreach (var p in Process.GetProcesses())
                {
                    try
                    {
                        if (p.Id == me || !p.ProcessName.StartsWith("GT5Launcher", StringComparison.OrdinalIgnoreCase)) continue;
                        if (!string.Equals(Path.GetDirectoryName(p.MainModule.FileName).TrimEnd('\\'), dir, StringComparison.OrdinalIgnoreCase)) continue;
                        IntPtr w = p.MainWindowHandle;
                        if (w == IntPtr.Zero) continue;
                        if (IsIconic(w)) ShowWindow(w, 9);   // SW_RESTORE
                        SetForegroundWindow(w);
                        return;
                    }
                    catch { }
                }
            }
            catch { }
        }
    }

    // ---- Where things are ------------------------------------------------------------------------
    class Paths
    {
        public string Root, Gt5, PlayBat, Settings, Exe, Elf, Logs, Mods, VersionFile, Icon0, Update, Readme, Vol, FixFiles;
        public Paths() : this(AppDomain.CurrentDomain.BaseDirectory) { }
        public Paths(string dir)
        {
            string here = dir.TrimEnd('\\');
            // The launcher sits in the install folder (next to update.ps1); a bare GT5 folder works too.
            if (Directory.Exists(Path.Combine(here, "GT5"))) { Root = here; Gt5 = Path.Combine(here, "GT5"); }
            else { Gt5 = here; Root = Path.GetDirectoryName(here); }
            PlayBat = Path.Combine(Gt5, "play.bat");
            Settings = Path.Combine(Gt5, "settings.bat");
            Exe = Path.Combine(Gt5, "build", "gt5.exe");
            Elf = Path.Combine(Gt5, "input", "EBOOT.elf");
            Logs = Path.Combine(Gt5, "logs");
            Mods = Path.Combine(Gt5, "mods");
            VersionFile = Path.Combine(Root, ".gt5", "version.txt");
            Icon0 = Path.Combine(Gt5, "vfs", "PS3_GAME", "ICON0.PNG");
            Update = Path.Combine(Root, "update.ps1");
            Readme = Path.Combine(Root, "README.md");
            Vol = Path.Combine(Gt5, "vfs", "PS3_GAME", "USRDIR", "GT.VOL");
            FixFiles = Path.Combine(Gt5, "scripts", "fix_game_files.ps1");
        }
        // GT.VOL in pieces (GT.VOL.66600, .66601, ...): a dump made to a FAT32 drive.
        public bool VolInPieces() { return File.Exists(Vol + ".66600"); }
        public string InstalledVersion()
        {
            try { return File.ReadAllLines(VersionFile)[0].Trim(); } catch { return ""; }
        }
    }

    // ---- This PC's graphics cards ------------------------------------------------------------------
    // NVIDIA DLSS runs on an NVIDIA RTX card only, and its frame generation on an RTX 40 series or newer: the
    // launcher offers them only there. The names come from DXGI (every graphics card, also a laptop's NVIDIA chip
    // that drives no screen), else from the display devices.
    class GpuInfo
    {
        public bool Rtx, RtxFrameGen;
        public List<string> Names = new List<string>();

        [ComImport, Guid("770aae78-f26f-4dba-a829-253c83d1b387"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IDXGIFactory1
        {
            void SetPrivateData(); void SetPrivateDataInterface(); void GetPrivateData(); void GetParent();
            void EnumAdapters(); void MakeWindowAssociation(); void GetWindowAssociation(); void CreateSwapChain(); void CreateSoftwareAdapter();
            [PreserveSig] int EnumAdapters1(uint index, out IDXGIAdapter1 adapter);
        }
        [ComImport, Guid("29038f61-3839-4626-91fd-086879011a05"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IDXGIAdapter1
        {
            void SetPrivateData(); void SetPrivateDataInterface(); void GetPrivateData(); void GetParent();
            void EnumOutputs(); void GetDesc(); void CheckInterfaceSupport();
            [PreserveSig] int GetDesc1(out AdapterDesc1 desc);
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct AdapterDesc1
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string Description;
            public uint VendorId, DeviceId, SubSysId, Revision;
            public UIntPtr DedicatedVideoMemory, DedicatedSystemMemory, SharedSystemMemory;
            public uint LuidLow; public int LuidHigh; public uint Flags;
        }
        [DllImport("dxgi.dll")] static extern int CreateDXGIFactory1(ref Guid riid, [MarshalAs(UnmanagedType.Interface)] out IDXGIFactory1 factory);
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct DisplayDevice
        {
            public int cb;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceString;
            public int StateFlags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceID;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceKey;
        }
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool EnumDisplayDevices(string device, uint i, ref DisplayDevice dd, uint flags);

        public static GpuInfo Read()
        {
            var g = new GpuInfo();
            // GT5_LAUNCHER_GPU=<name> (testing): as if that were the only card.
            string test = Environment.GetEnvironmentVariable("GT5_LAUNCHER_GPU");
            if (!string.IsNullOrEmpty(test)) g.Names.Add(test);
            else
            {
                try
                {
                    var iid = typeof(IDXGIFactory1).GUID;
                    IDXGIFactory1 f;
                    if (CreateDXGIFactory1(ref iid, out f) == 0 && f != null)
                    {
                        for (uint i = 0; i < 16; i++)
                        {
                            IDXGIAdapter1 a;
                            if (f.EnumAdapters1(i, out a) != 0 || a == null) break;
                            AdapterDesc1 d;
                            if (a.GetDesc1(out d) == 0 && (d.Flags & 2) == 0 && !string.IsNullOrEmpty(d.Description) && !g.Names.Contains(d.Description.Trim()))
                                g.Names.Add(d.Description.Trim());   // flag 2: a software adapter
                            Marshal.ReleaseComObject(a);
                        }
                        Marshal.ReleaseComObject(f);
                    }
                }
                catch { }
                if (g.Names.Count == 0)
                {
                    try
                    {
                        var dd = new DisplayDevice(); dd.cb = Marshal.SizeOf(typeof(DisplayDevice));
                        for (uint i = 0; EnumDisplayDevices(null, i, ref dd, 0); i++) { if (!g.Names.Contains(dd.DeviceString)) g.Names.Add(dd.DeviceString); dd.cb = Marshal.SizeOf(typeof(DisplayDevice)); }
                    }
                    catch { }
                }
            }
            foreach (var n in g.Names)
            {
                if (n.IndexOf("NVIDIA", StringComparison.OrdinalIgnoreCase) < 0 || !Regex.IsMatch(n, @"\bRTX\b", RegexOptions.IgnoreCase)) continue;
                g.Rtx = true;
                // RTX 40 / 50 series (GeForce "RTX 4070", laptops too), and the Ada and Blackwell workstation cards.
                Match m = Regex.Match(n, @"\bRTX\s+(\d\d)\d\d\b", RegexOptions.IgnoreCase);
                if (Regex.IsMatch(n, @"\b(Ada|Blackwell)\b", RegexOptions.IgnoreCase) ||
                    (m.Success && n.IndexOf("Quadro", StringComparison.OrdinalIgnoreCase) < 0 && int.Parse(m.Groups[1].Value) >= 40))
                    g.RtxFrameGen = true;
            }
            return g;
        }
    }

    // ---- Text in two languages ---------------------------------------------------------------------
    static class T
    {
        public static bool Zh;
        static readonly Dictionary<string, string[]> S = new Dictionary<string, string[]>
        {
            { "subtitle", new[] { "PC recompilation", "PC 重编译版" } },
            { "nav.play", new[] { "Play", "开始" } },
            { "nav.display", new[] { "Display", "画面" } },
            { "nav.controls", new[] { "Controls", "操控" } },
            { "nav.game", new[] { "Game", "游戏" } },
            { "nav.updates", new[] { "Updates", "更新" } },
            { "nav.help", new[] { "Help", "帮助" } },
            { "play", new[] { "PLAY", "开始游戏" } },
            { "running", new[] { "RUNNING", "运行中" } },
            { "ready", new[] { "Ready.", "就绪。" } },
            { "notbuilt", new[] { "The game isn't installed yet: run the installer.", "游戏尚未安装：请运行安装程序。" } },
            { "noelf", new[] { "The install isn't finished: run the installer again.", "安装尚未完成：请再次运行安装程序。" } },
            { "novol", new[] { "The game's data is missing: run the installer again and choose your game.", "缺少游戏数据：请再次运行安装程序并选择你的游戏。" } },
            { "volparts.page", new[] { "The game's data is in pieces: Play joins them first (a few minutes, 12 GB free space).", "游戏数据被分成了多个部分：按“开始游戏”会先合并（几分钟，需 12 GB 可用空间）。" } },
            { "already", new[] { "The game is already running.", "游戏已经在运行。" } },
            { "started", new[] { "Starting the game...", "正在启动游戏..." } },
            { "stopped", new[] { "The game has closed.", "游戏已关闭。" } },
            // The game ended on its own with an exit code that isn't one of its own (0, or 20 / 21, where it has told
            // the player itself): what Windows recorded goes to GT5\logs\play.err, not here. The report line's buttons
            // follow it (ProblemReport).
            { "crashed", new[] { "The game stopped unexpectedly.", "游戏意外停止。" } },
            // After a freeze or a start that showed nothing, the same line (ProblemReport).
            { "rep.freeze", new[] { "The game stopped responding.", "游戏停止响应。" } },
            { "rep.nostart", new[] { "The game didn't start properly.", "游戏未能正常启动。" } },
            { "rep.save", new[] { "Save a report", "保存报告" } },
            { "rep.page", new[] { "Open the report page", "打开报告页面" } },
            { "rep.hide", new[] { "Hide", "隐藏" } },
            { "rep.saved", new[] { "Report saved: attach it on the report page.", "报告已保存：请在报告页面附上它。" } },
            { "rep.notsaved", new[] { "The report couldn't be saved.", "无法保存报告。" } },
            { "nostart", new[] { "The game couldn't be started.", "无法启动游戏。" } },
            { "version", new[] { "Version", "版本" } },
            { "devbuild", new[] { "development build", "开发版本" } },
            // Display. The rows say what the game's own menu says (ps3recomp runtime\settings\gt5_settings_table.h).
            { "res", new[] { "Resolution", "分辨率" } },
            { "aa", new[] { "Anti-aliasing", "抗锯齿" } },
            { "aa.off", new[] { "Off", "关" } },
            { "aa.dlaa", new[] { "DLAA", "DLAA" } },
            { "aa.q", new[] { "DLSS: Quality", "DLSS：质量" } },
            { "aa.b", new[] { "DLSS: Balanced", "DLSS：平衡" } },
            { "aa.p", new[] { "DLSS: Performance", "DLSS：性能" } },
            { "aa.u", new[] { "DLSS: Ultra Performance", "DLSS：超级性能" } },
            { "aa.c", new[] { "DLSS: custom scale", "DLSS：自定义比例" } },
            { "shadows", new[] { "Shadows", "阴影" } },
            { "shadows.standard", new[] { "Standard", "标准" } },
            { "shadows.high", new[] { "High", "高" } },
            { "shadows.ultra", new[] { "Ultra", "极高" } },
            // New shaders (LD_SHADERS), as the game's menu says it (gt5_settings_table.h, k_ch_shaders).
            { "shaders", new[] { "Asynchronous shader loading", "异步着色器加载" } },
            { "shaders.wait", new[] { "Off", "关" } },
            { "shaders.bg", new[] { "On", "开" } },
            // The NVIDIA DLSS row under Anti-aliasing (an add-on: see class Addon). [[...]] is the link.
            { "dlss.name", new[] { "NVIDIA DLSS", "NVIDIA DLSS" } },
            { "dlss.note", new[] { "From NVIDIA, about 60 MB. Adding it accepts [[NVIDIA's licence]].", "来自 NVIDIA，约 60 MB。添加即表示接受 [[NVIDIA 的许可协议]]。" } },
            { "dlss.note.added", new[] { "NVIDIA's software, under [[NVIDIA's licence]].", "NVIDIA 的软件，适用 [[NVIDIA 的许可协议]]。" } },
            { "dlss.title.add", new[] { "Adding NVIDIA DLSS", "正在添加 NVIDIA DLSS" } },
            { "dlss.title.remove", new[] { "Removing NVIDIA DLSS", "正在移除 NVIDIA DLSS" } },
            { "dlss.ask.remove", new[] { "Remove NVIDIA DLSS?", "移除 NVIDIA DLSS？" } },
            { "dlss.ask.remove.text", new[] { "Anti-aliasing goes back to Off. You can add it again any time.", "抗锯齿会改回关闭。之后可随时再次添加。" } },
            { "dlss.added", new[] { "NVIDIA DLSS is ready: pick a mode in Anti-aliasing.", "NVIDIA DLSS 已就绪：在“抗锯齿”中选择模式即可。" } },
            { "dlss.notadded", new[] { "NVIDIA DLSS wasn't added.", "未添加 NVIDIA DLSS。" } },
            { "dlss.removed", new[] { "NVIDIA DLSS is removed.", "已移除 NVIDIA DLSS。" } },
            { "dlss.notremoved", new[] { "NVIDIA DLSS wasn't removed.", "未能移除 NVIDIA DLSS。" } },
            { "dlss.first", new[] { "Add NVIDIA DLSS first (the button below).", "请先添加 NVIDIA DLSS（下面的按钮）。" } },
            { "addon.add", new[] { "Add", "添加" } },
            { "addon.adding", new[] { "Adding...", "正在添加…" } },
            { "addon.added", new[] { "Added", "已添加" } },
            { "addon.remove", new[] { "Remove", "移除" } },
            { "addon.removing", new[] { "Removing...", "正在移除…" } },
            { "shape", new[] { "Screen shape", "画面比例" } },
            { "shape.help", new[] { "", "" } },
            { "shape.auto", new[] { "Auto (your monitor)", "自动（跟随显示器）" } },
            { "window", new[] { "Window", "窗口" } },
            { "window.help", new[] { "", "" } },
            { "window.windowed", new[] { "Window", "窗口模式" } },
            { "window.max", new[] { "Maximized window", "最大化窗口" } },
            { "window.borderless", new[] { "Borderless full screen", "无边框全屏" } },
            { "size", new[] { "Window size", "窗口大小" } },
            { "size.help", new[] { "", "" } },
            { "size.default", new[] { "Default", "默认" } },
            { "speed", new[] { "Frame rate", "帧率" } },
            { "intro", new[] { "Skip the opening movie", "跳过开场动画" } },
            { "intro.help", new[] { "", "" } },
            { "speed.full", new[] { "60 fps", "60 fps" } },
            { "speed.30", new[] { "30 fps", "30 fps" } },
            { "framegen", new[] { "Frame generation", "帧生成" } },
            { "fgmult", new[] { "Multiplier", "倍数" } },
            { "fg.off", new[] { "Off", "关" } },
            { "fg.on", new[] { "On", "开" } },
            { "fgmethod", new[] { "Generator", "生成器" } },
            { "fgmethod.auto", new[] { "Auto (best available)", "自动（最佳可用）" } },
            { "fgmethod.builtin", new[] { "Built-in", "内置" } },
            // The AMD FSR 3 row (an add-on, like NVIDIA DLSS).
            { "fsr.name", new[] { "AMD FSR 3", "AMD FSR 3" } },
            { "fsr.note", new[] { "From AMD, about 20 MB.", "来自 AMD，约 20 MB。" } },
            { "fsr.note.added", new[] { "", "" } },
            { "fsr.title.add", new[] { "Adding AMD FSR 3", "正在添加 AMD FSR 3" } },
            { "fsr.title.remove", new[] { "Removing AMD FSR 3", "正在移除 AMD FSR 3" } },
            { "fsr.ask.remove", new[] { "Remove AMD FSR 3?", "移除 AMD FSR 3？" } },
            { "fsr.ask.remove.text", new[] { "Frame generation will use another generator. You can add it again any time.", "帧生成将改用其他生成器。之后可随时再次添加。" } },
            { "fsr.added", new[] { "AMD FSR 3 is ready.", "AMD FSR 3 已就绪。" } },
            { "fsr.notadded", new[] { "AMD FSR 3 wasn't added.", "未添加 AMD FSR 3。" } },
            { "fsr.removed", new[] { "AMD FSR 3 is removed.", "已移除 AMD FSR 3。" } },
            { "fsr.notremoved", new[] { "AMD FSR 3 wasn't removed.", "未能移除 AMD FSR 3。" } },
            { "fsr.first", new[] { "Add AMD FSR 3 first (the button below).", "请先添加 AMD FSR 3（下面的按钮）。" } },
            { "renderer", new[] { "Renderer", "渲染器" } },
            { "renderer.dx12", new[] { "DirectX 12", "DirectX 12" } },
            { "renderer.vk", new[] { "Vulkan (experimental)", "Vulkan（实验性）" } },
            // Under Renderer while Vulkan is chosen: the rows it does nothing with yet are greyed.
            { "renderer.dx12only", new[] { "Greyed settings need DirectX 12.", "灰色的设置需要 DirectX 12。" } },
            { "cockpit", new[] { "Cockpit view", "车内视角" } },
            { "cockpit.game", new[] { "As in the game", "游戏原样" } },
            { "cfov", new[] { "Field of view", "视野" } },
            { "cfov.help", new[] { "", "" } },
            { "cfov.wider", new[] { "{0}° wider", "加宽 {0}°" } },
            { "cfov.narrower", new[] { "{0}° narrower", "缩小 {0}°" } },
            { "cseat", new[] { "Seat forward / back", "座椅前后" } },
            { "cseat.help", new[] { "", "" } },
            { "cseat.fwd", new[] { "{0} cm forward", "向前 {0} 厘米" } },
            { "cseat.back", new[] { "{0} cm back", "向后 {0} 厘米" } },
            { "cheight", new[] { "Seat height", "座椅高度" } },
            { "cheight.up", new[] { "{0} cm higher", "升高 {0} 厘米" } },
            { "cheight.down", new[] { "{0} cm lower", "降低 {0} 厘米" } },
            // Controls
            { "triggers", new[] { "Triggers", "扳机键" } },
            { "triggers.drive", new[] { "Throttle and brake", "油门和刹车" } },
            { "triggers.ps3", new[] { "As on a PS3 pad", "与 PS3 手柄相同" } },
            { "symbols", new[] { "Button symbols", "按键图标" } },
            { "symbols.game", new[] { "As in the game", "游戏原样" } },
            { "symbols.xbox", new[] { "Xbox", "Xbox" } },
            { "symbols.keys", new[] { "Keyboard", "键盘" } },
            { "symbols.auto", new[] { "Automatic", "自动" } },
            { "padbg", new[] { "Controller works when the game isn't in front", "游戏不在前台时手柄仍可用" } },
            { "clip", new[] { "Keep the mouse inside the window", "鼠标限制在窗口内" } },
            { "wheel", new[] { "Steering wheel", "方向盘" } },
            { "wheel.help", new[] { "", "" } },
            { "wheel.on", new[] { "Use one if connected", "已连接时使用" } },
            { "wheel.off", new[] { "Don't look for a wheel", "不使用方向盘" } },
            { "range", new[] { "Wheel rotation", "方向盘转角" } },
            { "range.help", new[] { "", "" } },
            { "ffb", new[] { "Force feedback strength", "力反馈强度" } },
            { "ffbinv", new[] { "Forces push the wrong way (invert)", "力反馈方向相反（反转）" } },
            { "mouse", new[] { "Steer with the mouse", "用鼠标转向" } },
            { "mouse.help", new[] { "", "" } },
            { "msens", new[] { "Mouse steering speed", "鼠标转向灵敏度" } },
            { "msens.help", new[] { "", "" } },
            { "msens.normal", new[] { "Normal", "正常" } },
            { "mcenter", new[] { "Wheel straightens by itself", "方向盘自动回正" } },
            { "mcenter.help", new[] { "", "" } },
            { "mcenter.slow", new[] { "Slowly", "慢" } },
            { "mcenter.mid", new[] { "Medium", "中" } },
            { "mcenter.fast", new[] { "Quickly", "快" } },
            { "mouse.short", new[] { "Mouse steering", "鼠标转向" } },
            // The Play page: how to reach the game's own settings ({0}: the menu key, and the controller's buttons).
            { "menukey", new[] { "In the game, {0} opens the settings.", "游戏中按 {0} 打开设置。" } },
            // Keyboard keys (PS3_KEYS)
            { "kb.title", new[] { "Keyboard keys", "键盘按键" } },
            { "kb.help", new[] { "", "" } },
            { "kb.views", new[] { "Left View, Right View, Flash Lights: give them a button in the game first.", "左视角、右视角、闪灯：先在游戏中为它们分配按钮。" } },
            { "kb.reset", new[] { "Reset to defaults", "恢复默认" } },
            { "kb.press", new[] { "Press a key...", "请按一个键..." } },
            { "kb.none", new[] { "(no key)", "（无按键）" } },
            { "kb.stick", new[] { "Sticks (full tilt)", "摇杆（推到底）" } },
            { "kb.a.cross", new[] { "Accelerate, OK", "加速、确定" } },
            { "kb.a.square", new[] { "Brake", "刹车" } },
            { "kb.a.r2", new[] { "Shift up", "升挡" } },
            { "kb.a.l2", new[] { "Shift down", "降挡" } },
            { "kb.a.circle", new[] { "Handbrake, Back", "手刹、返回" } },
            { "kb.a.triangle", new[] { "Reverse", "倒车" } },
            { "kb.a.l1", new[] { "Look back", "后视" } },
            { "kb.a.r1", new[] { "Headlights: flash, high/low beam", "大灯：闪灯、远/近光" } },
            { "kb.a.l3", new[] { "Horn", "喇叭" } },
            { "kb.a.r3", new[] { "Windscreen wipers", "雨刷" } },
            { "kb.a.select", new[] { "Change view (camera)", "切换视角" } },
            { "kb.a.start", new[] { "Pause", "暂停" } },
            { "kb.a.up", new[] { "Ghost, menu up", "幽灵车、菜单上" } },
            { "kb.a.down", new[] { "Menu down", "菜单下" } },
            { "kb.a.left", new[] { "Steer left, menu left", "左转、菜单左" } },
            { "kb.a.right", new[] { "Steer right, menu right", "右转、菜单右" } },
            { "kb.a.lstick_up", new[] { "(nothing by default)", "（默认无）" } },
            { "kb.a.lstick_down", new[] { "(nothing by default)", "（默认无）" } },
            { "kb.a.lstick_left", new[] { "Steer left", "左转" } },
            { "kb.a.lstick_right", new[] { "Steer right", "右转" } },
            { "kb.a.rstick_up", new[] { "Accelerate", "加速" } },
            { "kb.a.rstick_down", new[] { "Brake", "刹车" } },
            { "kb.a.rstick_left", new[] { "(nothing by default)", "（默认无）" } },
            { "kb.a.rstick_right", new[] { "(nothing by default)", "（默认无）" } },
            { "kb.b.cross", new[] { "Cross", "✕" } }, { "kb.b.square", new[] { "Square", "□" } },
            { "kb.b.circle", new[] { "Circle", "○" } }, { "kb.b.triangle", new[] { "Triangle", "△" } },
            { "kb.b.l1", new[] { "L1", "L1" } }, { "kb.b.r1", new[] { "R1", "R1" } }, { "kb.b.l2", new[] { "L2", "L2" } },
            { "kb.b.r2", new[] { "R2", "R2" } }, { "kb.b.l3", new[] { "L3 (left stick press)", "L3（按下左摇杆）" } },
            { "kb.b.r3", new[] { "R3 (right stick press)", "R3（按下右摇杆）" } },
            { "kb.b.select", new[] { "Select", "Select" } }, { "kb.b.start", new[] { "Start", "Start" } },
            { "kb.b.up", new[] { "D-pad up", "十字键上" } }, { "kb.b.down", new[] { "D-pad down", "十字键下" } },
            { "kb.b.left", new[] { "D-pad left", "十字键左" } }, { "kb.b.right", new[] { "D-pad right", "十字键右" } },
            { "kb.b.lstick_up", new[] { "Left stick up", "左摇杆上" } }, { "kb.b.lstick_down", new[] { "Left stick down", "左摇杆下" } },
            { "kb.b.lstick_left", new[] { "Left stick left", "左摇杆左" } }, { "kb.b.lstick_right", new[] { "Left stick right", "左摇杆右" } },
            { "kb.b.rstick_up", new[] { "Right stick up", "右摇杆上" } }, { "kb.b.rstick_down", new[] { "Right stick down", "右摇杆下" } },
            { "kb.b.rstick_left", new[] { "Right stick left", "右摇杆左" } }, { "kb.b.rstick_right", new[] { "Right stick right", "右摇杆右" } },
            // Game
            { "lang", new[] { "Game language", "游戏语言" } },
            { "lang.help", new[] { "Only languages your disc has.", "仅限光盘包含的语言。" } },
            { "lang.system", new[] { "Same as Windows", "与 Windows 相同" } },
            { "mods", new[] { "Mods", "模组" } },
            // Mods the port ships have a few words here by their folder name ("mod.<name>", GT5\mods\<name>) in place of
            // mod.json's text; another mod shows its own name only. (half-rate is Frame rate's 30 now: no tick of its own.)
            { "sound", new[] { "Sound", "声音" } },
            { "volume", new[] { "Volume", "音量" } },
            { "bgmute", new[] { "Mute when in the background", "在后台时静音" } },
            { "launcher", new[] { "Launcher", "启动器" } },
            { "ui", new[] { "Launcher language", "启动器语言" } },
            { "hide", new[] { "Minimize the launcher while playing", "游戏时最小化启动器" } },
            { "autocheck", new[] { "Check for updates when the launcher opens", "打开启动器时检查更新" } },
            // Updates
            { "upd.installed", new[] { "Installed", "已安装" } },
            { "upd.latest", new[] { "Newest release", "最新版本" } },
            { "upd.check", new[] { "Check now", "立即检查" } },
            { "upd.now", new[] { "Update now", "立即更新" } },
            { "upd.checking", new[] { "Checking...", "正在检查..." } },
            { "upd.uptodate", new[] { "You're up to date.", "已是最新版本。" } },
            { "upd.available", new[] { "Update available: {0}", "有可用更新：{0}" } },
            { "upd.failed", new[] { "Couldn't check for updates.", "无法检查更新。" } },
            { "upd.notes", new[] { "What's new", "更新内容" } },
            { "upd.running", new[] { "The game is running: close it before updating.", "游戏正在运行：请先关闭游戏再更新。" } },
            { "upd.nogit", new[] { "Updating needs an install made by the installer.", "需要由安装程序创建的安装才能更新。" } },
            // The update window
            { "uw.title", new[] { "Updating Gran Turismo 5", "正在更新 Gran Turismo 5" } },
            { "uw.start", new[] { "Starting the update", "正在开始更新" } },
            { "uw.fetch", new[] { "Getting the new version", "正在获取新版本" } },
            { "uw.prep", new[] { "Getting the build ready", "正在准备构建" } },
            { "uw.relift", new[] { "Translating the game's code again (about 10 minutes)", "正在重新转换游戏代码（约 10 分钟）" } },
            { "uw.build", new[] { "Building the game", "正在构建游戏" } },
            { "uw.files", new[] { "{0:N0} of {1:N0} steps", "第 {0:N0} / {1:N0} 步" } },
            { "uw.finish", new[] { "Finishing", "正在完成" } },
            { "uw.addon", new[] { "Adding {0}", "正在添加 {0}" } },
            { "uw.shaders", new[] { "Preparing shaders", "正在准备着色器" } },
            { "uw.shcount", new[] { "{0:N0} of {1:N0}", "{0:N0} / {1:N0}" } },
            { "prep.status", new[] { "Preparing shaders {0:N0} of {1:N0}", "正在准备着色器 {0:N0} / {1:N0}" } },
            { "prep.now", new[] { "PLAY NOW", "立即开始" } },
            { "uw.done", new[] { "Updated to {0}", "已更新到 {0}" } },
            { "uw.donegen", new[] { "Update finished", "更新完成" } },
            { "uw.uptodate", new[] { "Already up to date", "已是最新版本" } },
            { "uw.opening", new[] { "Opening the launcher...", "正在打开启动器..." } },
            { "uw.failed", new[] { "The update stopped", "更新已中止" } },
            { "uw.f.game", new[] { "Close the game, then update again.", "请关闭游戏后再次更新。" } },
            { "uw.f.net", new[] { "Couldn't connect. Check the internet connection and try again.", "无法连接。请检查网络连接后重试。" } },
            { "uw.f.other", new[] { "It stopped before the end. Run Update now again to carry on.", "更新在完成前中断。再次点击“立即更新”即可继续。" } },
            { "uw.log", new[] { "The full text is under Details.", "完整内容见“详细信息”。" } },
            { "uw.details", new[] { "Details", "详细信息" } },
            { "uw.hide", new[] { "Hide details", "隐藏详细信息" } },
            { "uw.open", new[] { "Open the launcher", "打开启动器" } },
            { "uw.close", new[] { "Close", "关闭" } },
            { "uw.stop", new[] { "Stop the update?\n\nRun Update now again later to finish it.", "要停止更新吗？\n\n之后再次点击“立即更新”即可完成。" } },
            // Help
            { "help.logs", new[] { "Open the logs folder", "打开日志文件夹" } },
            { "help.save", new[] { "Save logs for a report", "保存日志用于报告" } },
            { "help.saved", new[] { "Saved to your desktop: {0}. Attach them to your report.", "已保存到桌面：{0}。请附加到报告中。" } },
            { "help.nologs", new[] { "No logs yet: start the game once first.", "还没有日志：请先启动一次游戏。" } },
            { "help.report", new[] { "Report a problem", "报告问题" } },
            { "help.shaders", new[] { "Clear the shader cache", "清除着色器缓存" } },
            { "help.pkg", new[] { "Install a game update or DLC (.pkg)", "安装游戏更新或 DLC（.pkg）" } },
            { "pkg.title", new[] { "Installing game updates and DLC", "正在安装游戏更新和 DLC" } },
            { "pkg.done", new[] { "Installed.", "已安装。" } },
            { "pkg.next", new[] { "Installed. To play the update, run setup.bat in the install folder once (about an hour).", "已安装。要玩这个更新，请在安装文件夹中运行一次 setup.bat（约一小时）。" } },
            { "pkg.dlc", new[] { "Installed. Each DLC also needs its licence key (README, DLC).", "已安装。每个 DLC 还需要对应的许可密钥（见 README 的 DLC 部分）。" } },
            { "pkg.notdone", new[] { "The packages weren't all installed.", "部分安装包未能安装。" } },
            { "pkg.norpcs3", new[] { "Unpacking packages needs RPCS3 (the PS3 emulator) on this PC. Install it from rpcs3.net, then try again.", "解包需要这台电脑上有 RPCS3（PS3 模拟器）。请从 rpcs3.net 安装后重试。" } },
            { "pkg.notpkg", new[] { "Those files aren't PS3 game packages.", "这些文件不是 PS3 游戏包。" } },
            { "pkg.some", new[] { "Some packages weren't installed. The log says which.", "部分安装包未能安装。详情见日志。" } },
            // Joining GT.VOL when it was copied in pieces (Play)
            { "vol.ask", new[] { "Join the game's data?", "合并游戏数据？" } },
            { "vol.text", new[] { "The game's data was copied in pieces, as happens with some USB drives. The game needs it in one piece: joining takes a few minutes and about 12 GB of free space.", "游戏数据是分成多个部分复制的（某些 U 盘会这样）。游戏需要完整的一份：合并需要几分钟和约 12 GB 可用空间。" } },
            { "vol.join", new[] { "Join", "合并" } },
            { "vol.title", new[] { "Joining the game's data", "正在合并游戏数据" } },
            { "vol.done", new[] { "The game's data is in one piece.", "游戏数据已合并完成。" } },
            { "vol.failed", new[] { "The game's data wasn't joined, so the game can't start yet.", "游戏数据未能合并，游戏暂时无法启动。" } },
            // The task strip: questions and progress at the bottom of the page area (class TaskStrip)
            { "ask.add", new[] { "Add it", "添加" } },
            { "ask.notnow", new[] { "Not now", "暂不" } },
            { "ask.keep", new[] { "Keep it", "保留" } },
            { "busy", new[] { "One thing at a time: wait until this one finishes.", "请等当前任务完成后再试。" } },
            { "st.start", new[] { "Starting", "正在开始" } },
            { "st.download", new[] { "Downloading", "正在下载" } },
            { "st.unpack", new[] { "Unpacking", "正在解包" } },
            { "st.prepare", new[] { "Getting ready", "正在准备" } },
            { "st.build", new[] { "Building", "正在构建" } },
            { "st.buildgame", new[] { "Building the game", "正在构建游戏" } },
            { "st.install", new[] { "Installing", "正在安装" } },
            { "st.copy", new[] { "Copying", "正在复制" } },
            { "st.join", new[] { "Joining", "正在合并" } },
            { "st.remove", new[] { "Removing", "正在移除" } },
            { "st.working", new[] { "Working", "正在处理" } },
            { "st.shaders", new[] { "Preparing shaders", "正在准备着色器" } },
            { "st.stopping", new[] { "Stopping", "正在停止" } },
            { "st.done", new[] { "Done.", "完成。" } },
            { "st.failed", new[] { "Didn't finish.", "未完成。" } },
            { "st.stopped", new[] { "Stopped.", "已停止。" } },
            { "st.again", new[] { "You can try again any time.", "之后可随时重试。" } },
            { "st.cancel", new[] { "Cancel", "取消" } },
            { "st.log", new[] { "Open the log", "打开日志" } },
            { "st.closeask", new[] { "This isn't finished yet. Stop it and close the launcher?", "尚未完成。要停止并关闭启动器吗？" } },
            { "st.stopclose", new[] { "Stop and close", "停止并关闭" } },
            { "st.keepgoing", new[] { "Keep going", "继续" } },
            { "fail.net", new[] { "The download didn't finish. Check the internet connection and try again.", "下载未完成。请检查网络连接后重试。" } },
            { "fail.game", new[] { "The game is running. Close it, then try again.", "游戏正在运行。请关闭后重试。" } },
            { "fail.update", new[] { "This needs the newest version first: update on the Updates page.", "需要先更新到最新版本（更新页面）。" } },
            { "fail.notbuilt", new[] { "The game isn't built yet: finish the setup first.", "游戏尚未构建：请先完成安装。" } },
            { "fail.tools", new[] { "The game's build tools weren't found. Running the installer again sets them up.", "未找到游戏的构建工具。重新运行安装程序即可安装。" } },
            { "fail.space", new[] { "There isn't enough free disk space. Free some, then try again.", "磁盘可用空间不足。请释放一些空间后重试。" } },
            { "fail.nonvidia", new[] { "DLSS needs an NVIDIA graphics card, and none was found in this PC.", "DLSS 需要 NVIDIA 显卡，这台电脑上没有找到。" } },
            { "fail.build", new[] { "The build stopped before the end. The log says why: attach it if you report this.", "构建在完成前中断。原因见日志，报告问题时请附上。" } },
            { "fail.other", new[] { "It stopped before the end. The log says why.", "在完成前中断。原因见日志。" } },
            { "fail.start", new[] { "It couldn't be started. The log says why.", "无法启动。原因见日志。" } },
            { "shaders.ask", new[] { "Delete the shader cache ({0} files, {1:0} MB)?\n\nThe game makes them again as you play, with some stutter at first. Saves and settings stay.", "删除着色器缓存（{0} 个文件，{1:0} MB）？\n\n游戏会在游玩时重新生成，起初会有些卡顿。存档和设置不受影响。" } },
            { "shaders.empty", new[] { "The shader cache is already empty.", "着色器缓存已经是空的。" } },
            { "shaders.done", new[] { "Shader cache cleared. The game compiles its shaders again as you play.", "已清除着色器缓存。游戏会在你游玩时重新编译着色器。" } },
            { "shaders.partly", new[] { "Some files couldn't be deleted: close the game and try again.", "部分文件无法删除：请关闭游戏后重试。" } },
            { "shaders.running", new[] { "The game is running: close it first.", "游戏正在运行：请先关闭游戏。" } },
            { "help.readme", new[] { "Read the FAQ", "常见问题" } },
            { "help.changes", new[] { "Changelog", "更新日志" } },
            { "help.discord", new[] { "Discord", "Discord" } },
            { "help.about", new[] { "An unofficial PC port of Gran Turismo 5 made by static recompilation. It needs your own copy of the game; nothing from the game is distributed.", "通过静态重编译制作的非官方《GT赛车5》PC 移植版。需要你自己拥有的游戏副本；不分发任何游戏内容。" } },
            { "on", new[] { "On", "开" } },
            { "off", new[] { "Off", "关" } },
        };
        public static string Get(string key)
        {
            string[] v;
            if (!S.TryGetValue(key, out v)) return key;
            return Zh && v.Length > 1 && v[1].Length > 0 ? v[1] : v[0];
        }
    }

    // ---- Settings: environment variables for play.bat ------------------------------------------------
    class Choice
    {
        public string Value, Label;
        // The add-on this choice needs (a DLSS mode needs NVIDIA DLSS): without it the choice is greyed and can't be picked.
        public Addon Needs;
        public Choice(string v, string l) { Value = v; Label = l; }
        public bool Off { get { return Needs != null && !Needs.Present(); } }
        public override string ToString() { return Label; }
    }

    class Settings
    {
        // Every key the launcher manages, with play.bat's default. "" = not set. (LD_WATCH and SPURS_EXTRA_SPUS no
        // longer have rows, as in the game's menu: a value set before is kept, as the game keeps it.) The window
        // starts borderless full screen, as the game's own table has it (gt5_settings_table.h).
        public static readonly string[][] Defaults = new[]
        {
            new[] { "PS3_VIDEO_MODE", "1080" }, new[] { "PS3_ASPECT", "auto" }, new[] { "PS3_WINDOW_SIZE", "" },
            new[] { "LD_WATCH", "" }, new[] { "SPURS_EXTRA_SPUS", "2" }, new[] { "PAD_BACKGROUND", "1" },
            new[] { "PAD_CURSOR_CLIP", "" }, new[] { "PS3_WHEEL", "" }, new[] { "PS3_WHEEL_RANGE", "" },
            new[] { "PS3_WHEEL_FFB", "" }, new[] { "PS3_WHEEL_FFB_INVERT", "" }, new[] { "PS3_LANG", "" },
            new[] { "GT5_LAUNCH_WINDOW", "borderless" }, new[] { "GT5_LAUNCHER_LANG", "" },
            new[] { "GT5_LAUNCHER_HIDE", "1" }, new[] { "GT5_LAUNCHER_AUTOCHECK", "1" },
            new[] { "GT5_SPEED", "" }, new[] { "PS3_SKIP_INTRO", "1" }, new[] { "LD_WATCH_DRAW", "" },
            new[] { "LD_FG_METHOD", "" }, new[] { "LD_FRAMEGEN", "" }, new[] { "LD_FRAMEGEN_MULT", "" }, new[] { "LD_RES_TARGET", "" }, new[] { "LD_AA", "" }, new[] { "GT5_RENDERER", "" }, new[] { "PS3_TEX_DUMP", "" },
            new[] { "PS3_MOUSE_STEER", "" }, new[] { "PS3_MOUSE_STEER_SENS", "" }, new[] { "PS3_MOUSE_STEER_CENTER", "" },
            new[] { "PS3_KEYS", "" },
            new[] { "PS3_COCKPIT_FOV", "" }, new[] { "PS3_COCKPIT_SEAT", "" }, new[] { "PS3_COCKPIT_HEIGHT", "" },
        };
        // True when there was no settings.bat yet: the launcher's first start on this install.
        public bool Fresh;
        public Dictionary<string, string> V = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        readonly string path;
        const string SpeedMark = "REM Speed";
        const string OwnMark = "REM Your own lines";
        // set "NAME=value", and set NAME=value without the quotes, which cmd takes just the same
        // (before v0.1.86 a line without them was dropped at the next save).
        static readonly Regex SetLine = new Regex("^\\s*@?set\\s+(?:\"([A-Za-z0-9_]+)=(.*)\"|([A-Za-z0-9_]+)=(.*))\\s*$", RegexOptions.IgnoreCase);
        // The Speed lines older launchers wrote after SpeedMark for 30 fps: the launcher's, not the player's.
        static readonly string[] SpeedKeys = { "LD_WATCH", "LD_WATCH_PRIORITY", "LD_WATCH_DRAW" };

        // The values as the file had them when it was last read or written. A value the launcher hasn't
        // changed since then gives way to an edit made by hand meanwhile: the launcher stays open (hidden)
        // while the game runs, and before v0.1.67 its next save wrote its old copy over lines players had
        // added, such as PS3_WHEEL_PEDALS ("it just got deleted every time I launched the game").
        Dictionary<string, string> Base;

        public Settings(string path)
        {
            this.path = path;
            Fresh = !File.Exists(path);
            foreach (var d in Defaults) V[d[0]] = d[1];
            try { foreach (var kv in Read(path)) V[kv.Key] = kv.Value; } catch { }
            Base = new Dictionary<string, string>(V, StringComparer.OrdinalIgnoreCase);
            stamp = Stamp();
            HasSpeedLines = SpeedLinesInFile();
        }

        // The file still ends in the Speed lines an older launcher wrote for 30 fps (LD_WATCH=2 ...): the next save
        // leaves them out (LauncherForm.Normalize saves at once then).
        public bool HasSpeedLines;
        bool SpeedLinesInFile()
        {
            try
            {
                if (!File.Exists(path)) return false;
                foreach (var line in File.ReadAllLines(path))
                    if (line.Trim().StartsWith(SpeedMark, StringComparison.OrdinalIgnoreCase)) return true;
            }
            catch { }
            return false;
        }

        // The file's size and time when it was last read or written here. The game's own menu (F1) writes the same
        // file while the launcher waits, so the launcher reads it again when the game has ended and whenever its
        // window comes to the front (ChangedOnDisk, Reload).
        string stamp;
        string Stamp()
        {
            try { var f = new FileInfo(path); return f.Exists ? f.Length + "/" + f.LastWriteTimeUtc.Ticks : ""; } catch { return ""; }
        }
        public bool ChangedOnDisk() { return Stamp() != stamp; }

        // The file as it is now: every key takes the file's value (or its default when the file has none), except one
        // the launcher changed and hasn't written yet (there is none normally: each change is written at once).
        // True when any value changed.
        public bool Reload()
        {
            Dictionary<string, string> disk;
            try { if (!File.Exists(path)) return false; disk = Read(path); } catch { return false; }
            var now = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var d in Defaults) now[d[0]] = d[1];
            foreach (var kv in disk) now[kv.Key] = kv.Value;
            var next = new Dictionary<string, string>(now, StringComparer.OrdinalIgnoreCase);
            foreach (var kv in V) { string b; if (!Base.TryGetValue(kv.Key, out b) || b != kv.Value) next[kv.Key] = kv.Value; }
            bool changed = next.Count != V.Count;
            foreach (var kv in next) { string v; if (!V.TryGetValue(kv.Key, out v) || v != kv.Value) { changed = true; break; } }
            V = next;
            Base = now;
            stamp = Stamp();
            HasSpeedLines = SpeedLinesInFile();
            return changed;
        }

        static bool IsSpeedKey(string k)
        {
            foreach (var s in SpeedKeys) if (s.Equals(k, StringComparison.OrdinalIgnoreCase)) return true;
            return false;
        }
        // Lines the launcher writes itself (now or in earlier versions), so not the player's own.
        static bool LauncherLine(string t)
        {
            return t.Length == 0 || t.Equals("@echo off", StringComparison.OrdinalIgnoreCase)
                || t.StartsWith("REM Written by the GT5 Launcher", StringComparison.OrdinalIgnoreCase)
                || t.StartsWith("REM launcher, or edit them here", StringComparison.OrdinalIgnoreCase)
                || t.StartsWith("REM Lines of your own", StringComparison.OrdinalIgnoreCase)
                || t.StartsWith(OwnMark, StringComparison.OrdinalIgnoreCase)
                || t.StartsWith(SpeedMark, StringComparison.OrdinalIgnoreCase);
        }

        static Dictionary<string, string> Read(string path) { return Read(path, null); }

        // The set lines of a settings.bat; any other line of the player's own goes to `own` as written.
        // A key set twice takes the later value, as cmd does: a line added at the end of the file wins.
        // The Speed lines older launchers added after SpeedMark are the launcher's own and left out, but reading
        // goes on past them (before v0.1.86 it stopped there, and a player's lines added below them were dropped).
        // v0.1.20-v0.1.22 wrote the Speed lines with no marker, so they were read back as LD_WATCH=2 and
        // 30 fps stayed on after Speed was set back: for those keys the first value counts, and
        // LD_WATCH_PRIORITY that came with such a repeated LD_WATCH is dropped.
        static Dictionary<string, string> Read(string path, List<string> own)
        {
            var r = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            bool speedSection = false, speedLines = false;
            foreach (var line in File.ReadAllLines(path))
            {
                string t = line.Trim();
                if (t.StartsWith(SpeedMark, StringComparison.OrdinalIgnoreCase)) { speedSection = true; continue; }
                var m = SetLine.Match(line);
                if (!m.Success) { if (own != null && !LauncherLine(t)) own.Add(line.TrimEnd()); continue; }
                bool quoted = m.Groups[1].Success;
                string k = quoted ? m.Groups[1].Value : m.Groups[3].Value;
                string v = quoted ? m.Groups[2].Value : m.Groups[4].Value.TrimEnd();
                if (IsSpeedKey(k))
                {
                    if (speedSection) continue;
                    if (r.ContainsKey(k)) { if (k.Equals("LD_WATCH", StringComparison.OrdinalIgnoreCase)) speedLines = true; continue; }
                    if (speedLines && k.Equals("LD_WATCH_PRIORITY", StringComparison.OrdinalIgnoreCase)) continue;
                }
                r[k] = v;
            }
            return r;
        }

        static bool Managed(string k)
        {
            foreach (var d in Defaults) if (d[0].Equals(k, StringComparison.OrdinalIgnoreCase)) return true;
            return false;
        }

        // Takes in what was edited by hand since the last read or write: a key the launcher changed keeps
        // the launcher's value, any other key takes the file's, and a line of the player's own that was
        // deleted by hand stays deleted.
        void Merge()
        {
            Dictionary<string, string> disk;
            try { if (!File.Exists(path)) return; disk = Read(path); } catch { return; }
            var keys = new HashSet<string>(V.Keys, StringComparer.OrdinalIgnoreCase);
            keys.UnionWith(disk.Keys);
            keys.UnionWith(Base.Keys);
            var merged = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var k in keys)
            {
                string v, b, d;
                bool inV = V.TryGetValue(k, out v), inB = Base.TryGetValue(k, out b);
                if (inV != inB || (inV && v != b)) { if (inV) merged[k] = v; }
                else if (disk.TryGetValue(k, out d)) merged[k] = d;
                else if (inV && Managed(k)) merged[k] = v;
            }
            V = merged;
        }

        public string Get(string k) { string v; return V.TryGetValue(k, out v) ? v : ""; }
        public void Set(string k, string v) { V[k] = v ?? ""; }
        public void Save()
        {
            Merge();
            // Lines of the player's own that aren't settings (comments, anything else) are kept as written.
            var own = new List<string>();
            try { if (File.Exists(path)) Read(path, own); } catch { }
            var sb = new StringBuilder();
            sb.AppendLine("@echo off");
            sb.AppendLine("REM Written by the GT5 Launcher; play.bat runs it after its own defaults. Change these in the");
            sb.AppendLine("REM launcher, or edit them here: one set \"NAME=value\" per line, an empty value means not set.");
            sb.AppendLine("REM Lines of your own (like PS3_WHEEL_PEDALS) are kept, wherever you add them.");
            var keys = new List<string>(V.Keys);
            keys.Sort(StringComparer.OrdinalIgnoreCase);
            foreach (var k in keys)
                sb.AppendLine(string.Format("set \"{0}={1}\"", k, V[k].Replace("\"", "")));
            if (own.Count > 0)
            {
                sb.AppendLine(OwnMark + ", as you wrote them:");
                foreach (var l in own) sb.AppendLine(l);
            }
            // Frame rate (GT5_SPEED) is read by the game itself now (its half rate, libs\video\rsx_frame_rate.c): no
            // Speed lines after the keys any more. A file that has them from an older launcher loses them here
            // (Read leaves them out), and the game reads such a file without them meanwhile (gt5_settings.c).
            File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
            Base = new Dictionary<string, string>(V, StringComparer.OrdinalIgnoreCase);
            stamp = Stamp();
            HasSpeedLines = false;
        }
    }

    class ModInfo
    {
        public string Name, Description, Switch;
        public bool Default;
    }

    // ---- Keyboard keys: PS3_KEYS, as the game reads it (ps3recomp libs\input\cellPad.c, kb_init) -------
    // "button:key[/key...]" pairs split by commas. A bound button takes only its keys; the others keep
    // their default key unless a binding took it. The launcher writes only the buttons that differ from
    // the defaults, so with nothing changed PS3_KEYS stays empty and the game keeps its old keys.
    static class KeyBinds
    {
        public static readonly string[] Order = { "cross", "square", "r2", "l2", "circle", "triangle", "l1", "r1", "l3", "r3",
            "select", "start", "up", "down", "left", "right",
            "lstick_up", "lstick_down", "lstick_left", "lstick_right", "rstick_up", "rstick_down", "rstick_left", "rstick_right" };
        static readonly Dictionary<string, string> Def = new Dictionary<string, string>
        {
            { "up", "Up" }, { "down", "Down" }, { "left", "Left" }, { "right", "Right" }, { "cross", "Z" }, { "circle", "X" },
            { "square", "A" }, { "triangle", "S" }, { "l1", "Q" }, { "r1", "W" }, { "l2", "1" }, { "r2", "2" },
            { "start", "Enter" }, { "select", "Tab" },
        };
        // Windows virtual-key code -> the name the game takes. Letters, digits, F-keys and Num0-9 are made below.
        static readonly Dictionary<int, string> Vk = new Dictionary<int, string>
        {
            { 0x20, "Space" }, { 0x0D, "Enter" }, { 0x09, "Tab" }, { 0x08, "Backspace" }, { 0x26, "Up" }, { 0x28, "Down" },
            { 0x25, "Left" }, { 0x27, "Right" }, { 0x2D, "Insert" }, { 0x2E, "Delete" }, { 0x24, "Home" }, { 0x23, "End" },
            { 0x21, "PageUp" }, { 0x22, "PageDown" }, { 0x14, "CapsLock" }, { 0x6B, "NumPlus" }, { 0x6D, "NumMinus" },
            { 0x6A, "NumMultiply" }, { 0x6F, "NumDivide" }, { 0x6E, "NumDecimal" }, { 0xBC, "Comma" }, { 0xBE, "Period" },
            { 0xBF, "Slash" }, { 0xBA, "Semicolon" }, { 0xDE, "Apostrophe" }, { 0xDB, "LBracket" }, { 0xDD, "RBracket" },
            { 0xDC, "Backslash" }, { 0xBD, "Minus" }, { 0xBB, "Equals" }, { 0xC0, "Grave" }, { 0xE2, "IntlBackslash" },
            { 0xA0, "LShift" }, { 0xA1, "RShift" }, { 0xA2, "LCtrl" }, { 0xA3, "RCtrl" }, { 0xA4, "LAlt" }, { 0xA5, "RAlt" },
            { 0x05, "Mouse4" }, { 0x06, "Mouse5" },
        };
        static readonly List<string> Known = MakeKnown();
        static List<string> MakeKnown()
        {
            var l = new List<string>(Vk.Values);
            for (char c = 'A'; c <= 'Z'; c++) l.Add(c.ToString());
            for (char c = '0'; c <= '9'; c++) { l.Add(c.ToString()); l.Add("Num" + c); }
            for (int i = 1; i <= 24; i++) l.Add("F" + i);
            l.Add("Shift"); l.Add("Ctrl"); l.Add("Alt");
            return l;
        }

        // A key name in the game's spelling, or null for one it doesn't know (and Esc, the quit key).
        public static string Canon(string name)
        {
            foreach (var k in Known) if (string.Equals(k, name, StringComparison.OrdinalIgnoreCase)) return k;
            return null;
        }

        // The name of a key pressed in the launcher (a WM_KEYDOWN/WM_SYSKEYDOWN), or null.
        public static string FromKey(int vk, long lParam)
        {
            bool ext = (lParam & 0x01000000) != 0;
            int scan = (int)((lParam >> 16) & 0xFF);
            if (vk == 0x10) vk = scan == 0x36 ? 0xA1 : 0xA0;   // Shift: the right one has its own scan code
            if (vk == 0x11) vk = ext ? 0xA3 : 0xA2;
            if (vk == 0x12) vk = ext ? 0xA5 : 0xA4;
            if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return ((char)vk).ToString();
            if (vk >= 0x70 && vk <= 0x87) return "F" + (vk - 0x6F);
            if (vk >= 0x60 && vk <= 0x69) return "Num" + (vk - 0x60);
            string n;
            return Vk.TryGetValue(vk, out n) ? n : null;
        }

        public static string Pretty(string k)
        {
            switch (k)
            {
                case "LAlt": return "Left Alt";
                case "RAlt": return "Right Alt";
                case "LCtrl": return "Left Ctrl";
                case "RCtrl": return "Right Ctrl";
                case "LShift": return "Left Shift";
                case "RShift": return "Right Shift";
                default: return k;
            }
        }

        public static List<string> Default(string b)
        {
            var l = new List<string>();
            string k;
            if (Def.TryGetValue(b, out k)) l.Add(k);
            return l;
        }

        public static Dictionary<string, List<string>> Defaults()
        {
            var m = new Dictionary<string, List<string>>();
            foreach (var b in Order) m[b] = Default(b);
            return m;
        }

        public static Dictionary<string, List<string>> Parse(string s)
        {
            var map = Defaults();
            var bound = new Dictionary<string, List<string>>();
            foreach (var item in (s ?? "").Split(','))
            {
                int c = item.IndexOf(':');
                if (c < 0) continue;
                string b = item.Substring(0, c).Trim().ToLowerInvariant();
                if (Array.IndexOf(Order, b) < 0) continue;
                if (!bound.ContainsKey(b)) bound[b] = new List<string>();
                foreach (var part in item.Substring(c + 1).Split('/'))
                {
                    string k = Canon(part.Trim());
                    if (k != null && !bound[b].Contains(k) && bound[b].Count < 4) bound[b].Add(k);
                }
            }
            foreach (var kv in bound) map[kv.Key] = kv.Value;
            foreach (var b in Order)
            {
                if (bound.ContainsKey(b)) continue;
                foreach (var kv in bound) foreach (var k in kv.Value) map[b].Remove(k);
            }
            return map;
        }

        public static string Format(Dictionary<string, List<string>> map)
        {
            var parts = new List<string>();
            foreach (var b in Order)
            {
                var cur = map[b];
                var def = Default(b);
                bool same = cur.Count == def.Count;
                for (int i = 0; same && i < cur.Count; i++) same = cur[i] == def[i];
                if (!same) parts.Add(b + ":" + (cur.Count == 0 ? "none" : string.Join("/", cur.ToArray())));
            }
            return string.Join(",", parts.ToArray());
        }
    }

    // ---- Look ---------------------------------------------------------------------------------------
    static class Look
    {
        public static readonly Color Bg = Color.FromArgb(14, 16, 22);
        public static readonly Color Panel = Color.FromArgb(24, 27, 36);
        public static readonly Color Nav = Color.FromArgb(19, 21, 29);
        public static readonly Color NavHot = Color.FromArgb(34, 38, 50);
        public static readonly Color Text = Color.FromArgb(232, 234, 240);
        public static readonly Color Dim = Color.FromArgb(150, 156, 172);
        public static readonly Color Accent = Color.FromArgb(214, 32, 40);
        public static readonly Color AccentHot = Color.FromArgb(236, 56, 62);
        public static readonly Color Blue = Color.FromArgb(64, 140, 230);
        public static readonly Color Line = Color.FromArgb(44, 48, 62);
        // Segoe UI has no Chinese: Windows then mixes fonts character by character, in different weights. In Chinese
        // the whole launcher uses Microsoft YaHei UI (every Windows since 8 has it), as the update window already did.
        public static Font F(float size, FontStyle st) { return new Font(T.Zh ? "Microsoft YaHei UI" : "Segoe UI", size, st, GraphicsUnit.Point); }
    }

    class FlatButton : Button
    {
        public Color Normal, Hot;
        public FlatButton(string text, Color normal, Color hot)
        {
            Text = text; Normal = normal; Hot = hot; BackColor = normal; ForeColor = Look.Text;
            FlatStyle = FlatStyle.Flat; FlatAppearance.BorderSize = 0; Cursor = Cursors.Hand;
            FlatAppearance.MouseOverBackColor = hot; FlatAppearance.MouseDownBackColor = hot;
            Font = Look.F(10f, FontStyle.Regular); UseVisualStyleBackColor = false;
        }
    }

    // ---- The update window (#39) -----------------------------------------------------------------------
    // "Update now" used to open update.ps1 in a console window. The launcher can't show the progress
    // itself, as it closes so the build can replace GT5Launcher.exe; it starts a copy of itself
    // (GT5Updater.exe in %TEMP%) with --update-window, and the copy runs update.ps1 without a console:
    // the player's own Photo Mode pictures behind it (the GT logo when there are none), a car that drives
    // along one bar following the real steps (the build's own [n/total] count), the full text one click
    // away and in GT5\logs\update.log, and the new launcher opened at the end.
    class UpdateForm : Form
    {
        [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr h, int attr, ref int v, int size);
        static readonly Regex NinjaLine = new Regex(@"^\[(\d+)/(\d+)\]");
        static readonly Regex SwitchLine = new Regex(@"^Switching from (.+) to (.+)$");
        static readonly Regex AddProgress = new Regex(@"^GT5_ADD (\w+) (\d+|-) ?(.*)$");
        // gt5.exe --prepare-shaders: "GT5_SHADERS done total" (rsx_shader_prep.inc)
        internal static readonly Regex ShaderLine = new Regex(@"^GT5_SHADERS (\d+) (\d+)$");
        string addPrev;   // the step's words while an add-on runs
        readonly Paths P;
        readonly string relaunch;
        readonly string[] updArgs;
        Process proc;
        readonly object qlock = new object();
        readonly List<string> queue = new List<string>();
        readonly List<string> tail = new List<string>();   // the last lines, for Details (the log has them all)
        bool tailChanged;
        StreamWriter log;
        readonly System.Windows.Forms.Timer tick = new System.Windows.Forms.Timer();
        readonly DateTime t0 = DateTime.Now;
        DateTime stepT0 = DateTime.Now, exitSeen = DateTime.MinValue, doneAt = DateTime.MinValue;
        // Each step owns a slice [lo, hi] of the bar. A step that prints no count creeps through its slice
        // over about `secs`; the build follows ninja's own count.
        double lo, hi = 0.05, within, shown, secs = 20;
        bool counted, relifted, finished, failed, upToDate, opened;
        string step, sub = "", raw = "", label = "", failMsg = "";
        readonly List<string> photos = new List<string>();
        Image bg, logo;
        int photoIdx;
        DateTime photoT = DateTime.Now;
        float wheel;
        readonly TextBox details;
        readonly FlatButton detailsBtn, openBtn, closeBtn;

        public UpdateForm(string root, string launcher, string[] args)
        {
            P = new Paths(root);
            relaunch = launcher;
            updArgs = args;
            try { T.Zh = new Settings(P.Settings).Get("GT5_LAUNCHER_LANG") == "zh"; } catch { }
            step = T.Get("uw.start");
            Text = T.Get("uw.title");
            ClientSize = new Size(960, 540);
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.FixedSingle; MaximizeBox = false;
            BackColor = Look.Bg; ForeColor = Look.Text;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint, true);
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }
            try { if (File.Exists(P.Icon0)) logo = Image.FromStream(new MemoryStream(File.ReadAllBytes(P.Icon0))); } catch { }
            FindPhotos();
            NextPhoto();

            details = new TextBox
            {
                Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, WordWrap = false,
                BackColor = Color.FromArgb(10, 11, 15), ForeColor = Look.Dim, BorderStyle = BorderStyle.None,
                Font = new Font("Consolas", 9f), Location = new Point(24, 24), Size = new Size(912, 330), Visible = false
            };
            Controls.Add(details);
            detailsBtn = new FlatButton(T.Get("uw.details"), Look.Panel, Look.NavHot) { Size = new Size(130, 34), Location = new Point(806, 490) };
            detailsBtn.Click += delegate { ShowDetails(!details.Visible); };
            Controls.Add(detailsBtn);
            openBtn = new FlatButton(T.Get("uw.open"), Look.Blue, Color.FromArgb(90, 160, 240)) { Size = new Size(170, 34), Location = new Point(482, 490), Visible = false };
            openBtn.Click += delegate { OpenLauncher(); Close(); };
            Controls.Add(openBtn);
            closeBtn = new FlatButton(T.Get("uw.close"), Look.Panel, Look.NavHot) { Size = new Size(140, 34), Location = new Point(660, 490), Visible = false };
            closeBtn.Click += delegate { Close(); };
            Controls.Add(closeBtn);
            foreach (var bt in new[] { detailsBtn, openBtn, closeBtn }) bt.Font = UF(10f, FontStyle.Regular);

            tick.Interval = 40;
            tick.Tick += delegate { OnTick(); };
            Shown += delegate { Start(); };
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            try { int on = 1; if (DwmSetWindowAttribute(Handle, 20, ref on, 4) != 0) DwmSetWindowAttribute(Handle, 19, ref on, 4); } catch { }
        }

        // Photo Mode saves to hdd0\game\<disc>\USRDIR\photo: the newest pictures first.
        void FindPhotos()
        {
            try
            {
                var found = new List<FileInfo>();
                var games = Path.Combine(P.Gt5, "hdd0", "game");
                if (Directory.Exists(games))
                    foreach (var d in Directory.GetDirectories(games))
                    {
                        var ph = Path.Combine(d, "USRDIR", "photo");
                        if (!Directory.Exists(ph)) continue;
                        foreach (var f in new DirectoryInfo(ph).GetFiles("*.jpg"))
                            if (!f.Name.EndsWith("_t.jpg", StringComparison.OrdinalIgnoreCase)) found.Add(f);   // not the thumbnails
                    }
                found.Sort(delegate(FileInfo a, FileInfo b) { return b.LastWriteTimeUtc.CompareTo(a.LastWriteTimeUtc); });
                for (int i = 0; i < found.Count && i < 40; i++) photos.Add(found[i].FullName);
            }
            catch { }
        }

        void NextPhoto()
        {
            for (int tries = 0; tries < photos.Count; tries++)
            {
                string p = photos[photoIdx % photos.Count]; photoIdx++;
                try
                {
                    var img = Image.FromStream(new MemoryStream(File.ReadAllBytes(p)));
                    if (bg != null) bg.Dispose();
                    bg = img;
                    return;
                }
                catch { }
            }
        }

        void Start()
        {
            if (!File.Exists(P.Update)) { Fail(T.Get("upd.nogit")); return; }
            try
            {
                Directory.CreateDirectory(P.Logs);
                string lp = Path.Combine(P.Logs, "update.log");
                try { if (File.Exists(lp)) File.Copy(lp, Path.Combine(P.Logs, "update.prev.log"), true); } catch { }
                log = new StreamWriter(lp, false, new UTF8Encoding(false));
                log.AutoFlush = true;
                log.WriteLine("update window " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + ", in " + P.Root);
            }
            catch { log = null; }
            var a = new StringBuilder("-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + P.Update + "\"");
            foreach (var x in updArgs) a.Append(" \"" + x.Replace("\"", "") + "\"");
            var psi = new ProcessStartInfo("powershell.exe", a.ToString());
            psi.WorkingDirectory = P.Root;
            psi.UseShellExecute = false; psi.CreateNoWindow = true;
            psi.RedirectStandardOutput = true; psi.RedirectStandardError = true; psi.RedirectStandardInput = true;
            psi.StandardOutputEncoding = Encoding.UTF8; psi.StandardErrorEncoding = Encoding.UTF8;
            psi.EnvironmentVariables["GIT_TERMINAL_PROMPT"] = "0";
            try
            {
                proc = new Process { StartInfo = psi };
                proc.OutputDataReceived += Got;
                proc.ErrorDataReceived += Got;
                proc.Start();
                // Below normal, and so is everything it starts (git, the compiler): the PC stays usable meanwhile.
                try { proc.PriorityClass = ProcessPriorityClass.BelowNormal; } catch { }
                proc.StandardInput.Close();
                proc.BeginOutputReadLine(); proc.BeginErrorReadLine();
            }
            catch (Exception e) { Fail(e.Message); return; }
            tick.Start();
        }

        void Got(object s, DataReceivedEventArgs e)
        {
            if (e.Data == null) return;
            lock (qlock) queue.Add(e.Data);
        }

        void Step(double l, double h, string text, double expect)
        {
            lo = Math.Max(l, shown); hi = Math.Max(h, lo); step = text; sub = ""; secs = expect; addPrev = null;
            within = 0; counted = false; stepT0 = DateTime.Now;
        }

        void Line(string l)
        {
            if (log != null) { try { log.WriteLine(l); } catch { } }
            tail.Add(l); if (tail.Count > 1500) tail.RemoveRange(0, 300); tailChanged = true;
            // The script's own lines are read for the steps, never shown under the bar (Details has them all).
            string t = l.Trim();
            Match m = NinjaLine.Match(t);
            if (m.Success)
            {
                double n = double.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                double of = double.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture);
                if (of > 0)
                {
                    if (n >= of) { Step(0.97, 0.995, T.Get("uw.finish"), 15); return; }
                    if (!counted) { counted = true; stepT0 = DateTime.Now; }
                    within = n / of;
                    sub = string.Format(T.Get("uw.files"), n, of);
                }
                return;
            }
            // The parts update.ps1 builds with the game or next to it (GT5\scripts\addon_common.ps1: "GT5_ADD phase
            // percent words"): shown as what the step is doing, then the step's own words come back.
            m = AddProgress.Match(t);
            if (m.Success)
            {
                string ph = m.Groups[1].Value;
                if (ph == "start") { addPrev = step; step = string.Format(T.Get("uw.addon"), m.Groups[3].Value.Trim()); sub = ""; }
                else if (ph == "done" || ph == "skip" || ph == "fail") { if (addPrev != null) step = addPrev; addPrev = null; sub = ""; }
                else if (addPrev != null && m.Groups[2].Value != "-") sub = m.Groups[2].Value + "%";
                return;
            }
            // The shaders, made again for the new version before the game starts: "Preparing shaders 1,234 of 9,876".
            m = ShaderLine.Match(t);
            if (m.Success)
            {
                double n = double.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                double of = double.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture);
                if (of > 0)
                {
                    if (step != T.Get("uw.shaders")) { Step(0.97, 0.995, T.Get("uw.shaders"), 60); counted = true; }
                    within = Math.Min(1, n / of);
                    sub = string.Format(T.Get("uw.shcount"), n, of);
                }
                return;
            }
            m = SwitchLine.Match(t);
            if (m.Success) { label = m.Groups[2].Value.Trim(); Step(0.02, 0.08, T.Get("uw.fetch"), 15); return; }
            if (t.StartsWith("Reinstalling ")) { label = t.Substring(13).Trim(); Step(0.02, 0.08, T.Get("uw.fetch"), 15); return; }
            if (t.StartsWith("Already on ")) { upToDate = true; return; }
            if (t.StartsWith("ps3recomp:")) { Step(0.08, 0.12, T.Get("uw.prep"), 40); return; }
            if (t.StartsWith("Re-lifting")) { relifted = true; Step(0.12, 0.55, T.Get("uw.relift"), 600); return; }
            if (t.StartsWith("Building (only what changed)")) { Step(relifted ? 0.55 : 0.12, 0.97, T.Get("uw.build"), 300); return; }
            if (t.StartsWith("The build stopped. Trying again")) { counted = false; return; }   // a retry is Details' business
            if (t.StartsWith("update: ")) { failMsg = t.Substring(8); return; }
        }

        void OnTick()
        {
            List<string> got = null;
            lock (qlock) { if (queue.Count > 0) { got = new List<string>(queue); queue.Clear(); } }
            if (got != null) foreach (var l in got) Line(l);

            if (!finished && proc != null && proc.HasExited)
            {
                // The last lines arrive just after the exit.
                if (exitSeen == DateTime.MinValue) exitSeen = DateTime.Now;
                else if ((DateTime.Now - exitSeen).TotalSeconds > 1.5) Finish(proc.ExitCode);
            }
            if (!finished && !counted)
            {
                double el = (DateTime.Now - stepT0).TotalSeconds;
                within = Math.Min(0.95, 1 - Math.Exp(-el / Math.Max(1, secs)));
            }
            double target = finished && !failed ? 1.0 : lo + (hi - lo) * within;
            if (target > shown) { double d = (target - shown) * 0.12; shown += Math.Max(d, Math.Min(0.0005, target - shown)); }
            wheel += finished ? 0 : (float)(4 + 40 * Math.Max(0, target - shown) * 50);
            if (photos.Count > 1 && (DateTime.Now - photoT).TotalSeconds > 9) { photoT = DateTime.Now; NextPhoto(); }
            if (details.Visible && tailChanged) RefreshDetails();
            if (finished && !failed && !opened && (DateTime.Now - doneAt).TotalSeconds > 2.5)
            {
                opened = true; OpenLauncher(); Close(); return;
            }
            Invalidate();
        }

        void Finish(int code)
        {
            finished = true; doneAt = DateTime.Now;
            if (log != null) { try { log.WriteLine("update.ps1 ended with exit code " + code); } catch { } }
            if (code != 0)
            {
                // update.ps1's own words ("update: close the game first") are for the log; the window says it plainly.
                string f = failMsg.ToLowerInvariant();
                Fail(T.Get(f.Contains("close the game") ? "uw.f.game" : f.Contains("couldn't reach") || f.Contains("couldn't clone") ? "uw.f.net" : "uw.f.other"));
                return;
            }
            within = 1; shown = Math.Max(shown, 0.999);
            step = upToDate ? T.Get("uw.uptodate") : label != "" ? string.Format(T.Get("uw.done"), label) : T.Get("uw.donegen");
            sub = T.Get("uw.opening");
        }

        // Segoe UI has no Chinese: Windows then mixes fonts character by character.
        static Font UF(float size, FontStyle st) { return Look.F(size, st); }

        void Fail(string why)
        {
            finished = true; failed = true;
            step = T.Get("uw.failed");
            sub = why.Length > 0 ? char.ToUpperInvariant(why[0]) + why.Substring(1) : why;
            raw = T.Get("uw.log");
            openBtn.Visible = File.Exists(relaunch);
            closeBtn.Visible = true;
            ShowDetails(true);
            Invalidate();
        }

        void ShowDetails(bool on)
        {
            details.Visible = on;
            detailsBtn.Text = T.Get(on ? "uw.hide" : "uw.details");
            if (on) RefreshDetails();
        }

        void RefreshDetails()
        {
            tailChanged = false;
            details.Text = string.Join("\r\n", tail.ToArray());
            details.SelectionStart = details.TextLength; details.ScrollToCaret();
        }

        void OpenLauncher()
        {
            try
            {
                if (!File.Exists(relaunch)) return;
                var psi = new ProcessStartInfo(relaunch) { WorkingDirectory = P.Root, UseShellExecute = true };
                Process.Start(psi);
            }
            catch { }
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (!finished && proc != null && !proc.HasExited && e.CloseReason == CloseReason.UserClosing)
            {
                if (MessageBox.Show(this, T.Get("uw.stop"), T.Get("uw.title"), MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
                { e.Cancel = true; return; }
                try
                {
                    // update.ps1 and everything it started (git, the build).
                    var k = new ProcessStartInfo("taskkill.exe", "/T /F /PID " + proc.Id) { CreateNoWindow = true, UseShellExecute = false };
                    Process.Start(k).WaitForExit(5000);
                }
                catch { }
                if (log != null) { try { log.WriteLine("stopped from the update window"); } catch { } }
            }
            tick.Stop();
            if (log != null) { try { log.Dispose(); } catch { } log = null; }
            base.OnFormClosing(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
            int W = ClientSize.Width, H = ClientSize.Height;
            // The picture, filling the window.
            if (bg != null)
            {
                float s = Math.Max((float)W / bg.Width, (float)H / bg.Height);
                float iw = bg.Width * s, ih = bg.Height * s;
                g.InterpolationMode = InterpolationMode.HighQualityBilinear;
                g.DrawImage(bg, (W - iw) / 2, (H - ih) / 2, iw, ih);
            }
            else
            {
                using (var b = new LinearGradientBrush(new Rectangle(0, 0, W, H), Color.FromArgb(28, 32, 44), Look.Bg, 90f)) g.FillRectangle(b, 0, 0, W, H);
                if (logo != null && !details.Visible)
                {
                    float lw = 480, lh = lw * logo.Height / logo.Width;
                    g.DrawImage(logo, (W - lw) / 2, 70, lw, lh);
                }
            }
            // A dark band at the bottom for the text.
            var band = new Rectangle(0, H - 210, W, 210);
            using (var b = new LinearGradientBrush(band, Color.FromArgb(0, 8, 9, 12), Color.FromArgb(235, 8, 9, 12), 90f))
                g.FillRectangle(b, band.X, band.Y + 1, band.Width, band.Height);

            using (var f = UF(9.5f, FontStyle.Bold))
            using (var br = new SolidBrush(Color.FromArgb(200, Look.Text)))
                if (!details.Visible) g.DrawString(T.Get("uw.title").ToUpperInvariant(), f, br, 24, 20);

            // The step, its detail and the time so far.
            using (var f = UF(15f, FontStyle.Bold))
            using (var br = new SolidBrush(failed ? Look.AccentHot : Look.Text))
                g.DrawString(step, f, br, 22, H - 190);
            using (var f = UF(10f, FontStyle.Regular))
            using (var br = new SolidBrush(Look.Text))
                g.DrawString(sub, f, br, new RectangleF(24, H - 156, W - 48, 20), new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap });
            var el = DateTime.Now - t0;
            string right = (finished ? "" : ((int)(shown * 100)).ToString(CultureInfo.InvariantCulture) + "%   ") +
                           string.Format(CultureInfo.InvariantCulture, "{0}:{1:00}", (int)el.TotalMinutes, el.Seconds);
            using (var f = UF(10f, FontStyle.Regular))
            using (var br = new SolidBrush(Look.Dim))
            {
                var sz = g.MeasureString(right, f);
                g.DrawString(right, f, br, W - 24 - sz.Width, H - 184);
            }

            // The road and the car.
            float rx = 24, rw = W - 48, ry = H - 92, rh = 10;
            using (var b = new SolidBrush(Color.FromArgb(200, 40, 44, 56))) FillRound(g, b, rx, ry, rw, rh);
            float carLen = 66, x = rx + carLen + (float)shown * (rw - carLen);
            using (var b = new LinearGradientBrush(new RectangleF(rx, ry, rw, rh), Look.Blue, Color.FromArgb(120, 190, 255), 0f))
                if (x - rx > 4) FillRound(g, b, rx, ry, x - rx - 6, rh);
            using (var pen = new Pen(Color.FromArgb(110, 255, 255, 255), 2f) { DashPattern = new float[] { 6f, 6f } })
                g.DrawLine(pen, x + 8, ry + rh / 2, rx + rw - 4, ry + rh / 2);
            DrawCar(g, x, ry, failed ? Look.Dim : Look.Accent);

            using (var f = UF(8.5f, FontStyle.Regular))
            using (var br = new SolidBrush(Look.Dim))
                g.DrawString(raw, f, br, new RectangleF(24, H - 70, W - 48 - 290, 18), new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap });
        }

        static void FillRound(Graphics g, Brush b, float x, float y, float w, float h)
        {
            if (w < h) w = h;
            using (var p = new GraphicsPath())
            {
                p.AddArc(x, y, h, h, 90, 180);
                p.AddArc(x + w - h, y, h, h, 270, 180);
                p.CloseFigure();
                g.FillPath(b, p);
            }
        }

        // A low coupe seen from the side, its nose at x, its wheels on the road at y.
        void DrawCar(Graphics g, float x, float y, Color body)
        {
            float b = y - 1;
            using (var p = new GraphicsPath())
            {
                p.AddBezier(x - 66, b - 6, x - 66, b - 13, x - 60, b - 15, x - 50, b - 16);
                p.AddBezier(x - 50, b - 16, x - 42, b - 26, x - 26, b - 27, x - 16, b - 17);
                p.AddBezier(x - 16, b - 17, x - 6, b - 16, x, b - 12, x, b - 7);
                p.AddLine(x, b - 7, x - 2, b - 3);
                p.AddLine(x - 2, b - 3, x - 64, b - 3);
                p.CloseFigure();
                using (var br = new SolidBrush(body)) g.FillPath(br, p);
            }
            using (var p = new GraphicsPath())   // the side window
            {
                p.AddBezier(x - 46, b - 17, x - 40, b - 24, x - 28, b - 25, x - 21, b - 18);
                p.AddLine(x - 21, b - 18, x - 46, b - 17);
                p.CloseFigure();
                using (var br = new SolidBrush(Color.FromArgb(220, 20, 24, 34))) g.FillPath(br, p);
            }
            foreach (float wx in new[] { x - 52, x - 15 })
            {
                using (var br = new SolidBrush(Color.FromArgb(18, 18, 22))) g.FillEllipse(br, wx - 6.5f, b - 9, 13, 13);
                using (var br = new SolidBrush(Color.FromArgb(170, 176, 190))) g.FillEllipse(br, wx - 3.5f, b - 6, 7, 7);
                using (var pen = new Pen(Color.FromArgb(18, 18, 22), 1.5f))
                {
                    double a = wheel * Math.PI / 180;
                    float dx = (float)(Math.Cos(a) * 3.5), dy = (float)(Math.Sin(a) * 3.5);
                    g.DrawLine(pen, wx - dx, b - 2.5f - dy, wx + dx, b - 2.5f + dy);
                }
            }
        }
    }

    // ---- Add-ons: rows on the Display page that add something to the game -------------------------------
    // One row each: a name, one line of small text (with an optional link), and one button: Add, or "Added" with
    // Remove beside it. It is there when all its Files exist (read again each time the page is shown and after each
    // run). Add and Remove run its script hidden, with the progress in the launcher (TaskStrip). NVIDIA DLSS is the
    // first; AMD FSR 3 frame generation (add_fsr.ps1) and DLSS Frame Generation (add_dlss.ps1 again, plus one more
    // file) are each one more Addon in LauncherForm.Addons() and their texts in T.
    class Addon
    {
        public string Id;                              // "dlss"
        public string ForKey;                          // the setting it serves (LD_AA): greyed with that setting's row
        public string NameKey;                         // the row's label
        public string NoteKey, NoteAddedKey;           // the small line before and after adding; [[...]] is the link
        public string Link;                            // where that link goes (null: no link)
        public string Script;                          // its script in GT5\scripts
        public string[] AddArgs, RemoveArgs;           // the script's arguments for Add and for Remove
        public string[] Files;                         // all there = added
        public string AddTitleKey, RemoveTitleKey;     // the strip's title while it runs
        public string RemoveAskKey, RemoveTextKey;     // the one question before Remove
        public string AddedKey, NotAddedKey, RemovedKey, NotRemovedKey;   // the status line afterwards
        public string FirstKey;                        // the status line when a choice that needs it is picked
        public string Log;                             // GT5\logs\<Log>.log (Remove: <Log>-remove.log)
        public string WatchPath, CleanOnStop;          // see HelperJob
        public bool Present()
        {
            foreach (var f in Files) if (!File.Exists(f)) return false;
            return true;
        }
    }

    // ---- Helpers: scripts run with no window, their progress inside the launcher ---------------------------
    // One helper run: the script, its arguments, and how the launcher tells that it worked. The add-ons, Vulkan,
    // joining GT.VOL and installing .pkg files are each one of these, run and shown by the TaskStrip.
    class HelperJob
    {
        public string TitleKey;                        // a few plain words: "Adding NVIDIA DLSS"
        public string StartKey = "st.start";           // what the strip says before the script says more
        public string Script;                          // the .ps1
        public List<string> Args = new List<string>();
        public Dictionary<string, string> Env = new Dictionary<string, string>();
        public string[] DoneFiles;                     // all there at the end: it worked (null: exit code 0 says so)
        public bool DoneWhenGone;                      // a removal: it worked when none of DoneFiles is left
        public string Log;                             // GT5\logs\<Log>.log
        public string DoneKey;                         // the sentence after "Done."
        public string WatchPath;                       // a folder or file that grows while it downloads or joins: its size is the figure
        public long WatchTotal;                        // that size when it's finished, if known (then the bar follows it)
        public string CleanOnStop;                     // deleted when Cancel stops it while it downloads (a half fetch would stop the next try)
        public bool AutoHide = true;                   // the strip goes away by itself a few seconds after "Done."
        public Func<string, bool, string> Explain;     // (an output line, it worked) -> the text key for the end, or null
        public Action<bool> Ended;                     // on the window's thread, once the end is shown
    }

    // A script run by powershell.exe with no window at all: CreateNoWindow, its output and errors read on two threads
    // (the window never waits for it), stdin closed so nothing can wait for a key, and the whole process tree in a job
    // object, so Cancel ends all of it (git, cmake, the compiler) and so does the launcher closing or crashing.
    class HelperProcess
    {
        [StructLayout(LayoutKind.Sequential)]
        struct BasicLimits
        {
            public long PerProcessUserTimeLimit, PerJobUserTimeLimit; public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize; public uint ActiveProcessLimit;
            public UIntPtr Affinity; public uint PriorityClass, SchedulingClass;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct IoCounters { public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes; }
        [StructLayout(LayoutKind.Sequential)]
        struct ExtendedLimits
        {
            public BasicLimits Basic; public IoCounters Io;
            public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateJobObject(IntPtr attrs, string name);
        [DllImport("kernel32.dll")] static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref ExtendedLimits info, int size);
        [DllImport("kernel32.dll")] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll")] static extern bool TerminateJobObject(IntPtr job, uint code);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);

        // Kind 0: a line. 1: text ended by a lone \r (a progress line that the next one writes over, as in a console).
        // 2: that \r was followed by \n after all, so the text before it was a whole line.
        public struct Item { public int Stream, Kind; public string Text; }
        Process proc;
        IntPtr job = IntPtr.Zero;
        readonly object qlock = new object();
        List<Item> queue = new List<Item>();
        int open;
        DateTime exitSeen = DateTime.MinValue;

        public int Id { get { try { return proc.Id; } catch { return 0; } } }

        // One argument as the command line takes it (quotes, and backslashes before a quote or the end doubled).
        public static string Quote(string a)
        {
            if (a.Length > 0 && a.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return a;
            var sb = new StringBuilder("\"");
            int bs = 0;
            foreach (char c in a)
            {
                if (c == '\\') { bs++; continue; }
                if (c == '"') { sb.Append('\\', bs * 2 + 1); sb.Append('"'); bs = 0; continue; }
                sb.Append('\\', bs); bs = 0; sb.Append(c);
            }
            sb.Append('\\', bs * 2); sb.Append('"');
            return sb.ToString();
        }

        public string CommandLine(string script, IList<string> args)
        {
            var a = new StringBuilder("-NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + Quote(script));
            foreach (var x in args) a.Append(' ').Append(Quote(x));
            return a.ToString();
        }

        // Throws when powershell.exe can't be started.
        public void Start(string script, IList<string> args, string dir, IDictionary<string, string> env)
        {
            var psi = new ProcessStartInfo("powershell.exe", CommandLine(script, args));
            psi.WorkingDirectory = dir;
            psi.UseShellExecute = false; psi.CreateNoWindow = true; psi.WindowStyle = ProcessWindowStyle.Hidden;
            psi.RedirectStandardOutput = true; psi.RedirectStandardError = true; psi.RedirectStandardInput = true;
            psi.StandardOutputEncoding = Encoding.UTF8; psi.StandardErrorEncoding = Encoding.UTF8;
            psi.EnvironmentVariables["GIT_TERMINAL_PROMPT"] = "0";   // git never waits for a password
            psi.EnvironmentVariables["GCM_INTERACTIVE"] = "never";   // nor opens a sign-in window
            if (env != null) foreach (var kv in env) psi.EnvironmentVariables[kv.Key] = kv.Value;
            proc = new Process { StartInfo = psi };
            proc.Start();
            // Below normal, inherited by what it starts (git, the compiler): the PC stays usable while it builds.
            try { proc.PriorityClass = ProcessPriorityClass.BelowNormal; } catch { }
            try
            {
                job = CreateJobObject(IntPtr.Zero, null);
                var info = new ExtendedLimits();
                info.Basic.LimitFlags = 0x2000;   // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
                if (job != IntPtr.Zero && !(SetInformationJobObject(job, 9, ref info, Marshal.SizeOf(typeof(ExtendedLimits))) &&
                                            AssignProcessToJobObject(job, proc.Handle)))
                { CloseHandle(job); job = IntPtr.Zero; }
            }
            catch { job = IntPtr.Zero; }
            try { proc.StandardInput.Close(); } catch { }
            open = 2;
            Read(proc.StandardOutput, 0);
            Read(proc.StandardError, 1);
        }

        void Read(StreamReader r, int stream)
        {
            var th = new Thread(delegate ()
            {
                var buf = new char[4096];
                var sb = new StringBuilder();
                bool cr = false;
                try
                {
                    int n;
                    while ((n = r.Read(buf, 0, buf.Length)) > 0)
                        for (int i = 0; i < n; i++)
                        {
                            char c = buf[i];
                            if (cr) { cr = false; if (c == '\n') { Post(stream, 2, ""); continue; } }
                            if (c == '\r') { Post(stream, 1, sb.ToString()); sb.Length = 0; cr = true; }
                            else if (c == '\n') { Post(stream, 0, sb.ToString()); sb.Length = 0; }
                            else sb.Append(c);
                        }
                }
                catch { }
                if (sb.Length > 0) Post(stream, 0, sb.ToString());
                Interlocked.Decrement(ref open);
            });
            th.IsBackground = true;
            th.Start();
        }

        void Post(int s, int k, string t) { lock (qlock) queue.Add(new Item { Stream = s, Kind = k, Text = t }); }

        public List<Item> Take()
        {
            lock (qlock) { var q = queue; queue = new List<Item>(); return q; }
        }

        // Over: the script has exited and all its output is read (or 3 s after the exit, if something it left
        // behind still holds the pipe).
        public bool Ended
        {
            get
            {
                if (proc == null) return true;
                bool exited;
                try { exited = proc.HasExited; } catch { exited = true; }
                if (!exited) return false;
                if (exitSeen == DateTime.MinValue) exitSeen = DateTime.Now;
                return Thread.VolatileRead(ref open) <= 0 || (DateTime.Now - exitSeen).TotalSeconds > 3;
            }
        }

        public int ExitCode { get { try { return proc.ExitCode; } catch { return -1; } } }

        // Ends the script and everything it started.
        public void Stop()
        {
            if (job != IntPtr.Zero && TerminateJobObject(job, 1)) return;
            try
            {
                var k = new ProcessStartInfo("taskkill.exe", "/T /F /PID " + proc.Id) { CreateNoWindow = true, UseShellExecute = false };
                Process.Start(k).WaitForExit(5000);
            }
            catch { }
            try { if (!proc.HasExited) proc.Kill(); } catch { }
        }

        public void Close()
        {
            if (job != IntPtr.Zero) { CloseHandle(job); job = IntPtr.Zero; }
            try { proc.Dispose(); } catch { }
        }
    }

    // ---- The task strip: questions and progress inside the launcher ---------------------------------------
    // Before, the helpers (DLSS, Vulkan, joining GT.VOL, .pkg files) opened console windows over the launcher, with
    // their own questions in them. Now they run with no window and this strip shows them, docked at the bottom of the
    // page area: on every page, so the rest of the launcher stays usable while one runs. It asks (a question with two
    // buttons), runs one HelperJob at a time, says in a few plain words what is happening with a bar that shows a
    // real figure when the output has one (git's percentages, the build's [n/m], bytes of a download or a joined file;
    // a moving bar when not), has the full text under Details and in GT5\logs\<name>.log, and Cancel. It ends with
    // "Done." or one sentence of what went wrong, and "Open the log". A strip rather than a window of its own: it can't
    // be lost behind the launcher or pop up over the game, and a long build doesn't hold the rest of the launcher.
    class TaskStrip : Panel
    {
        enum Mode { Hidden, Ask, Note, Run, End, Confirm }
        static readonly Regex GitLine = new Regex(@"^(remote:\s*)?(Receiving objects|Resolving deltas|Updating files|Checking out files|Counting objects|Compressing objects|Enumerating objects|Unpacking objects):\s+(\d+)%");
        static readonly Regex GitSize = new Regex(@",\s*([\d.]+)\s*(KiB|MiB|GiB)");
        static readonly Regex NinjaLine = new Regex(@"^\[(\d+)/(\d+)\]");
        static readonly Regex BytesLine = new Regex(@"([\d.]+)\s*(MB|MiB)\s*(?:of|/)\s*([\d.]+)\s*(MB|MiB)", RegexOptions.IgnoreCase);
        static readonly Regex AddProgress = new Regex(@"^GT5_ADD (\w+) (\d+|-) ?(.*)$");
        bool addFmt;       // the script prints GT5_ADD lines: they lead the bar
        string addEnd;     // the words of its last "fail" or "skip" line
        const int Pad = 16;

        public string Logs, Gt5;   // GT5\logs, and the folder the scripts run in
        Mode mode = Mode.Hidden;
        HelperJob job;
        HelperProcess hp;
        string titleKey = "", textKey = "", headKey = "", phase = "st.start", figure = "";
        bool ok, stopped, cleaning;
        double pct = -1, watchPct = -1, shown;
        float marquee;
        DateTime t0, endAt;
        readonly List<string> lines = new List<string>();
        readonly string[] live = new string[2];
        bool linesChanged;
        DateTime detailsT;
        StreamWriter log;
        string logPath;
        Action yes, no, onStopped;
        long watched = -1;
        bool[] watchAlive;
        readonly FlatButton[] btn = new FlatButton[3];
        readonly Action[] act = new Action[3];
        readonly TextBox details;
        readonly System.Windows.Forms.Timer tick = new System.Windows.Forms.Timer();
        readonly Font fTitle = Look.F(10.5f, FontStyle.Bold), fText = Look.F(9.5f, FontStyle.Regular), fBold = Look.F(9.5f, FontStyle.Bold);
        int textY = 40, textH = 20, headW;

        public TaskStrip()
        {
            Dock = DockStyle.Bottom; Visible = false; BackColor = Look.Panel; DoubleBuffered = true;
            for (int i = 0; i < btn.Length; i++)
            {
                int k = i;
                btn[i] = new FlatButton("", Look.NavHot, Look.NavHot) { Height = 30, Visible = false };
                btn[i].Click += delegate { if (act[k] != null) act[k](); };
                Controls.Add(btn[i]);
            }
            details = new TextBox
            {
                Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, WordWrap = true, Visible = false,
                BackColor = Color.FromArgb(10, 11, 15), ForeColor = Look.Dim, BorderStyle = BorderStyle.None, Font = new Font("Consolas", 9f)
            };
            Controls.Add(details);
            tick.Interval = 40;
            tick.Tick += delegate { OnTick(); };
            Resize += delegate { Arrange(); };
        }

        public TextBox Details { get { return details; } }
        // A script runs (or its partial download is being removed): nothing else can start meanwhile.
        public bool Busy { get { return mode == Mode.Run || mode == Mode.Confirm || cleaning; } }
        public string Showing { get { return mode.ToString(); } }

        // A question with two buttons; nothing runs until the first is pressed. A question still open is answered
        // with its second button first.
        public bool Ask(string title, string text, string yesKey, string noKey, Action onYes, Action onNo)
        {
            if (Busy) return false;
            DropAsk();
            mode = Mode.Ask; titleKey = title; textKey = text; yes = onYes; no = onNo;
            Buttons(new[] { yesKey, noKey }, new[] { 1, 0 }, new Action[] {
                delegate { var y = yes; yes = null; no = null; Close2(); if (y != null) y(); },
                delegate { var n = no; yes = null; no = null; Close2(); if (n != null) n(); } });
            Open();
            return true;
        }

        // One calm sentence and Close (nothing was started: the game is running, say).
        public void Note(string title, string sentence)
        {
            if (Busy) return;
            DropAsk();
            mode = Mode.Note; titleKey = title; textKey = sentence;
            Buttons(new[] { "uw.close" }, new[] { 0 }, new Action[] { Close2 });
            Open();
        }

        void DropAsk()
        {
            if (mode != Mode.Ask) return;
            var n = no; yes = null; no = null;
            if (n != null) n();
        }

        public bool Run(HelperJob j)
        {
            if (Busy) return false;
            DropAsk();
            job = j; mode = Mode.Run; titleKey = j.TitleKey; phase = j.StartKey; figure = "";
            pct = -1; watchPct = -1; shown = 0; stopped = false; ok = false; onStopped = null; addFmt = false; addEnd = null;
            lines.Clear(); live[0] = live[1] = null; linesChanged = true; watched = -1; t0 = DateTime.Now;
            RunButtons();
            StartLog();
            hp = new HelperProcess();
            if (log != null) { try { log.WriteLine("powershell.exe " + hp.CommandLine(j.Script, j.Args)); } catch { } }
            try { hp.Start(j.Script, j.Args, Gt5, j.Env); }
            catch (Exception e)
            {
                hp = null;
                AddLine("powershell.exe could not be started: " + e.Message);
                Open();
                End(false, "st.failed", "fail.start");
                return true;
            }
            if (j.WatchPath != null) StartWatch(j.WatchPath);
            Open();
            return true;
        }

        void RunButtons()
        {
            Buttons(new[] { details.Visible ? "uw.hide" : "uw.details", "st.cancel" }, new[] { 0, 0 }, new Action[] { ToggleDetails, Cancel });
        }

        // The launcher is closing while a script runs: ask here, in the strip.
        public void AskClose(Action closeNow)
        {
            if (!Busy) { closeNow(); return; }
            if (mode == Mode.Confirm) return;
            mode = Mode.Confirm; textKey = "st.closeask";
            Buttons(new[] { "st.stopclose", "st.keepgoing" }, new[] { 2, 0 }, new Action[] {
                delegate { mode = Mode.Run; RunButtons(); onStopped = closeNow; Cancel(); Arrange(); Invalidate(); },
                delegate { mode = Mode.Run; RunButtons(); Arrange(); Invalidate(); } });
            Open();
        }

        // The launcher closes for another reason (Windows shutting down): end the script and all it started.
        public void Kill() { if (hp != null) hp.Stop(); }

        void Cancel()
        {
            if (hp == null || stopped) return;
            stopped = true;
            btn[1].Enabled = false;
            AddLine("[launcher] stopped by the player");
            hp.Stop();
            Invalidate();
        }

        void Open()
        {
            Visible = true;
            Arrange();
            Invalidate();
            tick.Start();
        }

        // Hides the strip (not while something runs).
        public void Close2()
        {
            if (Busy) return;
            mode = Mode.Hidden;
            details.Visible = false;
            Visible = false;
            tick.Stop();
        }

        // Done and shown: hide now (Play goes on after joining GT.VOL, say).
        public void HideIfDone() { if (mode == Mode.End && ok) Close2(); }

        void StartLog()
        {
            CloseLog();
            logPath = null;
            try
            {
                Directory.CreateDirectory(Logs);
                logPath = Path.Combine(Logs, job.Log + ".log");
                log = new StreamWriter(logPath, false, new UTF8Encoding(false));
                log.AutoFlush = true;
                log.WriteLine("GT5 Launcher, " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture) + ": " + T.Get(job.TitleKey));
            }
            catch { log = null; }
        }

        void CloseLog()
        {
            if (log == null) return;
            try { log.Dispose(); } catch { }
            log = null;
        }

        void ShowLogFile()
        {
            if (logPath == null || !File.Exists(logPath)) return;
            try { Process.Start(new ProcessStartInfo(logPath) { UseShellExecute = true }); }
            catch
            {
                try { Process.Start(new ProcessStartInfo("notepad.exe", HelperProcess.Quote(logPath)) { UseShellExecute = true }); } catch { }
            }
        }

        void ToggleDetails()
        {
            details.Visible = !details.Visible;
            for (int i = 0; i < btn.Length; i++)
                if (act[i] == (Action)ToggleDetails) { btn[i].Text = T.Get(details.Visible ? "uw.hide" : "uw.details"); FitButton(btn[i]); }
            if (details.Visible) RefreshDetails();
            Arrange();
            Invalidate();
        }

        void RefreshDetails()
        {
            linesChanged = false;
            detailsT = DateTime.Now;
            var sb = new StringBuilder();
            for (int i = Math.Max(0, lines.Count - 600); i < lines.Count; i++) sb.Append(lines[i]).Append("\r\n");
            foreach (var l in live) if (l != null) sb.Append(l).Append("\r\n");
            details.Text = sb.ToString();
            details.SelectionStart = details.TextLength;
            details.ScrollToCaret();
        }

        void AddLine(string l)
        {
            lines.Add(l);
            if (lines.Count > 5000) lines.RemoveRange(0, 1000);
            if (log != null) { try { log.WriteLine(l); } catch { } }
            linesChanged = true;
        }

        void Take(HelperProcess.Item it)
        {
            int s = it.Stream;
            if (it.Kind == 1) { live[s] = it.Text; Parse(it.Text); linesChanged = true; return; }
            if (it.Kind == 2) { if (live[s] != null) { AddLine(live[s]); live[s] = null; } return; }
            live[s] = null;   // a new line over a progress line: that one is gone, as in a console
            AddLine(it.Text);
            Parse(it.Text);
        }

        void Phase(string key)
        {
            if (phase == key) return;
            phase = key; pct = -1; watchPct = -1; shown = 0; figure = "";
        }

        static string PhaseOf(string step)
        {
            string l = step.ToLowerInvariant();
            if (l.Contains("fetching") || l.Contains("download")) return "st.download";
            if (l.Contains("building gt5") || l.Contains("building the game")) return "st.buildgame";
            if (l.Contains("configure") || l.Contains("turning")) return "st.prepare";
            if (l.Contains("build")) return "st.build";
            if (l.Contains("remov")) return "st.remove";
            return "st.working";
        }

        static double Mb(double v, string unit)
        {
            return unit.StartsWith("G", StringComparison.OrdinalIgnoreCase) ? v * 1024 : unit.StartsWith("K", StringComparison.OrdinalIgnoreCase) ? v / 1024 : v;
        }

        // What the script is doing, from its own output: the "== step" lines the GT5 scripts print, git's progress,
        // the build's [n/m] count, "x MB of y MB".
        void Parse(string text)
        {
            string t = text.Trim();
            if (t.Length == 0) return;
            // The add scripts' own progress lines (GT5\scripts\addon_common.ps1): the phase, and the whole run's
            // percent, which then leads the bar (git's and the build's own figures only fill in the words).
            Match m = UpdateForm.ShaderLine.Match(t);   /* gt5.exe --prepare-shaders */
            if (m.Success)
            {
                double n = double.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                double of = double.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture);
                if (of > 0)
                {
                    if (phase != "st.shaders") Phase("st.shaders");
                    pct = Math.Min(100, n * 100 / of);
                    figure = string.Format(T.Get("uw.shcount"), n, of);
                }
                return;
            }
            m = AddProgress.Match(t);
            if (m.Success)
            {
                addFmt = true;
                string ph = m.Groups[1].Value;
                string key = ph == "fetch" ? "st.download" : ph == "check" ? "st.prepare" : ph == "build" ? "st.build" :
                             ph == "game" ? "st.buildgame" : ph == "remove" ? "st.remove" : null;
                if (key != null && key != phase) { phase = key; figure = ""; }
                if (m.Groups[2].Value != "-") pct = Math.Min(100, double.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture));
                if (ph == "fail" || ph == "skip") addEnd = m.Groups[3].Value.Trim();
                return;
            }
            if (addFmt)
            {
                m = NinjaLine.Match(t);
                if (m.Success) figure = string.Format(T.Get("uw.files"), double.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture), double.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture));
                return;
            }
            m = GitLine.Match(t);
            if (m.Success)
            {
                if (m.Groups[1].Length > 0) { Phase("st.download"); return; }   // the server counting: no figure yet
                string what = m.Groups[2].Value;
                Phase(what == "Receiving objects" || what == "Unpacking objects" ? "st.download" : "st.unpack");
                pct = double.Parse(m.Groups[3].Value, CultureInfo.InvariantCulture);
                Match sz = GitSize.Match(t);
                double v;
                if (sz.Success && double.TryParse(sz.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out v))
                    figure = string.Format(CultureInfo.InvariantCulture, "{0:0} MB", Mb(v, sz.Groups[2].Value));
                return;
            }
            m = NinjaLine.Match(t);
            if (m.Success)
            {
                double n = double.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture), of = double.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture);
                if (of > 0)
                {
                    if (phase != "st.build" && phase != "st.buildgame") Phase("st.build");
                    pct = Math.Min(100, n * 100 / of);
                    figure = string.Format(T.Get("uw.files"), n, of);
                }
                return;
            }
            m = BytesLine.Match(t);
            if (m.Success)
            {
                double a, b;
                if (double.TryParse(m.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out a) &&
                    double.TryParse(m.Groups[3].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out b) && b > 0)
                {
                    a = Mb(a, m.Groups[2].Value); b = Mb(b, m.Groups[4].Value);
                    pct = Math.Min(100, a * 100 / b);
                    figure = string.Format(CultureInfo.InvariantCulture, "{0:0} MB", a);
                }
                return;
            }
            if (t.StartsWith("== ")) { Phase(PhaseOf(t.Substring(3))); return; }
            if (t.StartsWith("Installing ")) { Phase("st.install"); return; }
            if (t.StartsWith("Copying ")) { Phase("st.copy"); return; }
            if (t.IndexOf("joining them", StringComparison.OrdinalIgnoreCase) >= 0) { Phase("st.join"); return; }
        }

        // The size of WatchPath, on a thread of its own once a second (a folder being downloaded into can hold
        // thousands of files).
        void StartWatch(string path)
        {
            var alive = new[] { true };
            watchAlive = alive;
            var th = new Thread(delegate ()
            {
                while (alive[0])
                {
                    Interlocked.Exchange(ref watched, SizeOf(path));
                    Thread.Sleep(1000);
                }
            });
            th.IsBackground = true;
            th.Start();
        }

        static long SizeOf(string path)
        {
            try
            {
                if (File.Exists(path)) return new FileInfo(path).Length;
                if (!Directory.Exists(path)) return -1;
                long n = 0;
                foreach (var f in Directory.GetFiles(path, "*", SearchOption.AllDirectories)) { try { n += new FileInfo(f).Length; } catch { } }
                return n;
            }
            catch { return -1; }
        }

        void OnTick()
        {
            if (hp != null)
            {
                foreach (var it in hp.Take()) Take(it);
                if (job.WatchPath != null && pct < 0 && (phase == "st.download" || phase == "st.join"))
                {
                    long w = Interlocked.Read(ref watched);
                    if (w > 0)
                    {
                        figure = string.Format(CultureInfo.InvariantCulture, "{0:0} MB", w / 1048576.0);
                        if (job.WatchTotal > 0) watchPct = Math.Min(100, w * 100.0 / job.WatchTotal);
                    }
                }
                if (hp.Ended)
                {
                    foreach (var it in hp.Take()) Take(it);
                    Finish();
                }
            }
            double bar = pct >= 0 ? pct : watchPct;
            if (bar >= 0) shown += (bar / 100.0 - shown) * 0.2;
            else marquee = (marquee + 0.01f) % 1.3f;
            if (details.Visible && linesChanged && (DateTime.Now - detailsT).TotalMilliseconds > 250) RefreshDetails();
            if (mode == Mode.End && ok && job != null && job.AutoHide && !details.Visible && (DateTime.Now - endAt).TotalSeconds > 6 &&
                !RectangleToScreen(ClientRectangle).Contains(Cursor.Position))
                Close2();
            if (mode == Mode.Run || mode == Mode.Confirm) Invalidate();
        }

        void Finish()
        {
            if (watchAlive != null) watchAlive[0] = false;
            for (int s = 0; s < 2; s++) if (live[s] != null) { AddLine(live[s]); live[s] = null; }
            int code = hp.ExitCode;
            hp.Close();
            hp = null;
            AddLine("[launcher] the script ended with exit code " + code + (stopped ? " (stopped)" : ""));
            bool worked;
            if (job.DoneFiles != null)
            {
                int there = 0;
                foreach (var f in job.DoneFiles) if (File.Exists(f)) there++;
                worked = job.DoneWhenGone ? there == 0 : there == job.DoneFiles.Length;
            }
            else worked = code == 0;
            if (worked) { End(true, "st.done", Explained(true) ?? job.DoneKey); return; }
            if (!stopped) { End(false, "st.failed", Explained(false) ?? ByPhase()); return; }
            // Stopped while downloading: what was half fetched goes, so the next try starts clean.
            string clean = job.CleanOnStop;
            if (clean == null || phase != "st.download" || !Inside(clean, Gt5)) { End(false, "st.stopped", "st.again"); return; }
            cleaning = true;
            AddLine("[launcher] removing the part-downloaded " + clean);
            var th = new Thread(delegate ()
            {
                DeleteTree(clean);
                try { BeginInvoke((MethodInvoker)delegate { cleaning = false; End(false, "st.stopped", "st.again"); }); } catch { }
            });
            th.IsBackground = true;
            th.Start();
        }

        static bool Inside(string path, string root)
        {
            try
            {
                string p = Path.GetFullPath(path).TrimEnd('\\'), r = Path.GetFullPath(root).TrimEnd('\\') + "\\";
                return p.StartsWith(r, StringComparison.OrdinalIgnoreCase);
            }
            catch { return false; }
        }

        // git leaves its files read-only, which Directory.Delete won't remove; a process that was just ended can
        // hold a file a moment longer.
        static void DeleteTree(string dir)
        {
            for (int tries = 0; tries < 6 && Directory.Exists(dir); tries++)
            {
                try
                {
                    foreach (var f in Directory.GetFiles(dir, "*", SearchOption.AllDirectories)) { try { File.SetAttributes(f, FileAttributes.Normal); } catch { } }
                    Directory.Delete(dir, true);
                }
                catch { Thread.Sleep(500); }
            }
        }

        // The sentence for the end: the job's own reading of the output first, then what the GT5 scripts say
        // when they stop (in their own words, which stay in Details and the log).
        string Explained(bool worked)
        {
            int from = Math.Max(0, lines.Count - 60);
            if (job.Explain != null)
            {
                if (worked) { for (int i = from; i < lines.Count; i++) { string k = job.Explain(lines[i].Trim(), true); if (k != null) return k; } }
                else for (int i = lines.Count - 1; i >= from; i--) { string k = job.Explain(lines[i].Trim(), false); if (k != null) return k; }
            }
            if (worked) return null;
            // The add scripts say why in their last GT5_ADD line (addon_common.ps1).
            switch (addEnd ?? "")
            {
                case "Game running": return "fail.game";
                case "No internet": return "fail.net";
                case "Update first": return "fail.update";
                case "No game build": return "fail.notbuilt";
                case "No compiler": return "fail.tools";
                case "Build failed": return "fail.build";
                case "No NVIDIA RTX card": return "fail.nonvidia";
            }
            for (int i = lines.Count - 1; i >= from; i--)
            {
                string l = lines[i];
                if (Has(l, "The game is running")) return "fail.game";
                if (Has(l, "internet connection") || Has(l, "Could not resolve host") || Has(l, "unable to access") || Has(l, "Failed to connect")) return "fail.net";
                if (Has(l, "update first")) return "fail.update";
                if (Has(l, "no game build")) return "fail.notbuilt";
                if (Has(l, "set up the compiler") || Has(l, "find clang-cl")) return "fail.tools";
                if (Has(l, "Free some space") || Has(l, "not enough space")) return "fail.space";
                if (Has(l, "No NVIDIA graphics card")) return "fail.nonvidia";
            }
            return null;
        }

        static bool Has(string l, string what) { return l.IndexOf(what, StringComparison.OrdinalIgnoreCase) >= 0; }

        string ByPhase()
        {
            if (phase == "st.download") return "fail.net";
            if (phase == "st.build" || phase == "st.buildgame" || phase == "st.prepare") return "fail.build";
            return "fail.other";
        }

        void End(bool good, string head, string sentence)
        {
            CloseLog();
            mode = Mode.End; ok = good; headKey = head; textKey = sentence; endAt = DateTime.Now;
            if (good) shown = 1;
            var keys = new List<string>(); var acts = new List<Action>();
            if (logPath != null && File.Exists(logPath)) { keys.Add("st.log"); acts.Add(ShowLogFile); }
            keys.Add(details.Visible ? "uw.hide" : "uw.details"); acts.Add(ToggleDetails);
            keys.Add("uw.close"); acts.Add(Close2);
            Buttons(keys.ToArray(), new int[keys.Count], acts.ToArray());
            if (details.Visible) RefreshDetails();
            Arrange();
            Invalidate();
            var j = job;
            if (j != null && j.Ended != null) j.Ended(good);
            if (onStopped != null) { var a = onStopped; onStopped = null; a(); }
        }

        // Styles: 0 plain, 1 blue (the answer that goes ahead), 2 red (stops something).
        void Buttons(string[] keys, int[] styles, Action[] acts)
        {
            for (int i = 0; i < btn.Length; i++)
            {
                bool on = i < keys.Length;
                act[i] = on ? acts[i] : null;
                btn[i].Visible = on;
                if (!on) continue;
                var b = btn[i];
                b.Text = T.Get(keys[i]); b.Enabled = true;
                Color n = styles[i] == 1 ? Look.Blue : styles[i] == 2 ? Look.Accent : Look.NavHot;
                Color h = styles[i] == 1 ? Color.FromArgb(90, 160, 240) : styles[i] == 2 ? Look.AccentHot : Color.FromArgb(52, 57, 74);
                b.Normal = n; b.Hot = h; b.BackColor = n;
                b.FlatAppearance.MouseOverBackColor = h; b.FlatAppearance.MouseDownBackColor = h;
                b.Font = styles[i] == 1 ? Look.F(10f, FontStyle.Bold) : Look.F(10f, FontStyle.Regular);
                FitButton(b);
            }
        }

        static void FitButton(FlatButton b) { b.Width = Math.Max(90, TextRenderer.MeasureText(b.Text, b.Font).Width + 30); }

        string Title()
        {
            if (mode == Mode.Confirm || mode == Mode.Run || mode == Mode.End) return T.Get(job != null ? job.TitleKey : titleKey);
            return T.Get(titleKey);
        }

        // Places the text, buttons and Details for the mode, and sets the strip's height.
        void Arrange()
        {
            int W = Math.Max(320, ClientSize.Width), y;
            if (mode == Mode.Run)
            {
                PlaceButtons(W, 34);
                y = 92;
            }
            else
            {
                headW = mode == Mode.End ? TextRenderer.MeasureText(T.Get(headKey), fBold).Width : 0;
                int w = W - 2 * Pad - (headW > 0 ? headW + 4 : 0);
                textY = 40;
                textH = TextRenderer.MeasureText(T.Get(textKey), fText, new Size(w, 2000), TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl).Height;
                int by = textY + textH + 12;
                PlaceButtons(W, by);
                y = by + 30 + 14;
            }
            if (details.Visible && (mode == Mode.Run || mode == Mode.End || mode == Mode.Confirm))
            {
                details.SetBounds(Pad, y - 4, W - 2 * Pad, 170);
                y += 180;
            }
            else details.Visible = false;
            if (Height != y)
            {
                Height = y;
                // A new height set during the page area's own layout (this runs on Resize) isn't placed until the
                // next one: without this the strip's bottom went under the edge of the page area.
                if (IsHandleCreated) BeginInvoke((MethodInvoker)delegate { if (Parent != null) Parent.PerformLayout(); });
            }
        }

        void PlaceButtons(int W, int y)
        {
            int total = 0;
            foreach (var b in btn) if (b.Visible) total += b.Width + 8;
            int x = W - Pad - total + 8;
            foreach (var b in btn) if (b.Visible) { b.Location = new Point(x, y); x += b.Width + 8; }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            var g = e.Graphics;
            int W = ClientSize.Width;
            using (var pen = new Pen(Look.Line)) g.DrawLine(pen, 0, 0, W, 0);
            var flags = TextFormatFlags.NoPrefix | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis;
            TextRenderer.DrawText(g, Title(), fTitle, new Rectangle(Pad, 11, W - 2 * Pad - 120, 24), Look.Text, flags);
            if (mode == Mode.Run || mode == Mode.End)
            {
                var el = (mode == Mode.End ? endAt : DateTime.Now) - t0;
                double bar = pct >= 0 ? pct : watchPct;
                string right = (mode == Mode.Run && bar >= 0 ? ((int)bar).ToString(CultureInfo.InvariantCulture) + "%    " : "") +
                               string.Format(CultureInfo.InvariantCulture, "{0}:{1:00}", (int)el.TotalMinutes, el.Seconds);
                TextRenderer.DrawText(g, right, fText, new Rectangle(W - Pad - 160, 12, 160, 22), Look.Dim, flags | TextFormatFlags.Right);
            }
            if (mode == Mode.Run)
            {
                string now = T.Get(stopped ? "st.stopping" : phase);
                TextRenderer.DrawText(g, now, fText, new Point(Pad, 39), Look.Text, TextFormatFlags.NoPrefix);
                if (figure != "" && !stopped)
                {
                    int nw = TextRenderer.MeasureText(g, now, fText, new Size(1000, 30), TextFormatFlags.NoPrefix).Width;
                    TextRenderer.DrawText(g, "·  " + figure, fText, new Point(Pad + nw + 4, 39), Look.Dim, TextFormatFlags.NoPrefix);
                }
                DrawBar(g, Pad, 74, W - 2 * Pad, 6);
                return;
            }
            var wrap = TextFormatFlags.NoPrefix | TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl;
            int x = Pad;
            if (mode == Mode.End)
            {
                TextRenderer.DrawText(g, T.Get(headKey), fBold, new Point(Pad, textY), headKey == "st.failed" ? Look.AccentHot : Look.Text, TextFormatFlags.NoPrefix);
                x += headW + 4;
            }
            TextRenderer.DrawText(g, T.Get(textKey), fText, new Rectangle(x, textY, W - Pad - x, textH + 4), Look.Text, wrap);
        }

        void DrawBar(Graphics g, float x, float y, float w, float h)
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using (var b = new SolidBrush(Color.FromArgb(40, 44, 56))) Round(g, b, x, y, w, h);
            double bar = pct >= 0 ? pct : watchPct;
            if (stopped) return;
            using (var b = new LinearGradientBrush(new RectangleF(x, y, w, h), Look.Blue, Color.FromArgb(120, 190, 255), 0f))
            {
                if (bar >= 0) { float fw = (float)(w * Math.Max(0, Math.Min(1, shown))); if (fw > 1) Round(g, b, x, y, fw, h); }
                else
                {
                    // No figure: a piece of the bar moving along it.
                    float seg = w * 0.22f, sx = x + (marquee - 0.22f) * w;
                    float a = Math.Max(x, sx), z = Math.Min(x + w, sx + seg);
                    if (z - a > 1) Round(g, b, a, y, z - a, h);
                }
            }
        }

        static void Round(Graphics g, Brush b, float x, float y, float w, float h)
        {
            if (w < h) w = h;
            using (var p = new GraphicsPath())
            {
                p.AddArc(x, y, h, h, 90, 180);
                p.AddArc(x + w - h, y, h, h, 270, 180);
                p.CloseFigure();
                g.FillPath(b, p);
            }
        }
    }

    // ---- The window -----------------------------------------------------------------------------------
    class LauncherForm : Form
    {
        [DllImport("user32.dll")] static extern int GetWindowLong(IntPtr h, int i);
        [DllImport("user32.dll")] static extern int SetWindowLong(IntPtr h, int i, int v);
        [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hh, uint f);
        [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int cmd);
        [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
        const int GWL_STYLE = -16; const int WS_POPUP = unchecked((int)0x80000000); const int WS_VISIBLE = 0x10000000;
        const int WS_OVERLAPPEDWINDOW = 0x00CF0000;

        readonly Paths P = new Paths();
        Settings S;
        readonly Panel content = new Panel();
        readonly Dictionary<string, Panel> pages = new Dictionary<string, Panel>();
        readonly Dictionary<string, FlatButton> navButtons = new Dictionary<string, FlatButton>();
        FlatButton playButton;
        Label status, versionLabel, updateBadge, latestLabel, installedLabel;
        TextBox notes;
        FlatButton updateButton;
        string page = "nav.play";
        volatile bool running;
        string latestTag = "";
        Image logo;
        // Helpers (add-ons, Vulkan, GT.VOL, .pkg): their questions and progress, at the bottom of the page area.
        TaskStrip strip;
        readonly Dictionary<string, ComboBox> combos = new Dictionary<string, ComboBox>();
        readonly Dictionary<string, AddonRow> addonRows = new Dictionary<string, AddonRow>();
        List<Addon> addons = new List<Addon>();
        string busyAddon;     // the add-on whose script runs now
        bool busyRemove;       // ... to remove it
        bool comboQuiet;       // a list or tick box being set by the launcher itself (nothing saved, nothing asked)
        bool closeNow;         // "Stop and close" was pressed
        GpuInfo Gpu;           // what this PC's graphics cards can run (NVIDIA DLSS needs an RTX card)
        // The line after a crash or a freeze (ProblemReport): the run it is about, its sentence, what the line said
        // before it, and its buttons.
        RunVerdict repRun;
        string repText, repBefore;
        bool repSaved;
        FlatButton repSave, repPage, repHide;
        // Each setting's row, by its key (label, list or tick box, help line; an add-on row joins the row of the
        // setting it serves): a row can be greyed as a whole, and hidden as a whole with the rows below moving up.
        class Row { public string Key; public Panel Page; public int Top, Bottom; public readonly List<Control> Ctl = new List<Control>(); }
        readonly Dictionary<string, Row> rows = new Dictionary<string, Row>();
        // Every page control's place as the page was built (rows hidden above it move it up by their height).
        readonly Dictionary<Control, int> builtTop = new Dictionary<Control, int>();
        // Each list, tick box and number set again from the settings (after the game's own menu changed the file).
        readonly List<Action> refreshers = new List<Action>();
        Action playRefresh;
        // Settings the Vulkan renderer does nothing with yet: their rows are greyed while Vulkan is chosen, with one
        // line under Renderer. The rows the game's own menu hides on Vulkan (rsx_vulkan_backend.c, vk_menu_start, after
        // the Vulkan menu's first stage); take a key out when Vulkan gains that feature.
        static readonly string[] VulkanLacks = { "LD_FG_METHOD", "LD_FRAMEGEN_MULT", "PS3_TEX_DUMP", "LD_SHADERS" };
        // New shaders (LD_SHADERS) when not chosen ("" = this): false = wait for them, true = build in the background.
        // The game has the same constant (gt5_settings.h, GT5_SHADERS_BG_DEFAULT): flip both together.
        static readonly bool ShadersBgDefault = true;
        // The smallest window (its inside): the widest page row (the add-on rows, 700) beside the page list, and
        // room for a few rows above Play.
        const int MinClientW = 960, MinClientH = 540;

        readonly bool shots;
        public void SaveScreenshots(string dir)
        {
            Directory.CreateDirectory(dir);
            foreach (var k in new List<string>(pages.Keys))
            {
                ShowPage(k); Application.DoEvents();
                using (var bmp = new Bitmap(Width, Height))
                {
                    DrawToBitmap(bmp, new Rectangle(0, 0, Width, Height));
                    bmp.Save(Path.Combine(dir, "launcher_" + k.Substring(4) + ".png"), System.Drawing.Imaging.ImageFormat.Png);
                }
            }
        }

        // (A first start used to pick 720p from the processor count: a graphics choice made from the CPU, while the
        // Play page went on saying 1080p. Every install now starts with the game's own defaults.)
        public LauncherForm(bool screenshots)
        {
            shots = screenshots;
            S = new Settings(P.Settings);
            // v0.1.20-v0.1.22 chose 30 fps by themselves for small CPUs and laptops; it rarely helps, so
            // those installs go back to every frame once (it can be chosen again on the Display page).
            if (S.Get("GT5_LAUNCHER_REV") == "")
            {
                if (!S.Fresh && S.Get("GT5_SPEED") == "30") S.Set("GT5_SPEED", "");
                // Saved again by those versions, the Speed lines became settings of their own (LD_WATCH=2,
                // which the launcher never offers), so 30 fps stayed on with Speed set back.
                if (!S.Fresh && S.Get("LD_WATCH") == "2") { S.Set("LD_WATCH", ""); S.V.Remove("LD_WATCH_PRIORITY"); }
                S.Set("GT5_LAUNCHER_REV", "23");
                if (!S.Fresh) { try { S.Save(); } catch { } }
            }
            T.Zh = S.Get("GT5_LAUNCHER_LANG") == "zh";
            Text = "Gran Turismo 5 — PC";
            BackColor = Look.Bg; ForeColor = Look.Text;
            // A window like any other: its border sizes it, it maximises, and it opens where it was left. The
            // smallest size still shows a whole row of settings (the pages scroll up and down).
            FormBorderStyle = FormBorderStyle.Sizable; MaximizeBox = true;
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(980, 620);
            MinimumSize = SizeFromClientSize(new Size(MinClientW, MinClientH));
            StartPosition = FormStartPosition.CenterScreen;
            if (!shots) LoadPlacement();
            Gpu = GpuInfo.Read();
            Normalize();
            Font = Look.F(9.5f, FontStyle.Regular);
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }
            try { if (File.Exists(P.Icon0)) logo = Image.FromFile(P.Icon0); } catch { }
            DoubleBuffered = true;
            strip = new TaskStrip();   // after the language is known: its fonts are made with it
            strip.Logs = P.Logs; strip.Gt5 = P.Gt5;
            DarkScroll(strip.Details);
            Build();
            ShowPage("nav.play");
            // After the window exists: the check answers on the UI thread through BeginInvoke. Unticked ("" or "0")
            // means no check: the tick box writes "1" or nothing.
            Shown += delegate { if (S.Get("GT5_LAUNCHER_AUTOCHECK") == "1" && !shots) CheckForUpdates(false); };
            // The last run, when it crashed or froze and its line wasn't dismissed or used. Screenshots: the line itself
            // with GT5_LAUNCHER_REPORT_TEST=crash / freeze / nostart / saved.
            if (!shots) Shown += delegate { CheckLastRun(); };
            else
            {
                string rt = Environment.GetEnvironmentVariable("GT5_LAUNCHER_REPORT_TEST") ?? "";
                if (rt != "") ShowReportLine(new RunVerdict { Kind = rt == "freeze" ? RunEnd.Freeze : rt == "nostart" ? RunEnd.NoStart : RunEnd.Crash });
                if (rt == "saved") { repSaved = true; repSave.Visible = false; SetReportText("rep.saved"); }
            }
            // The game's own menu (F1) writes the same settings: read them again whenever the launcher comes to the front.
            Activated += delegate { if (!shots) ReloadSettings(); };
        }

        // Values this launcher can't show as they are, put the way it writes them. At start and after each read.
        void Normalize()
        {
            bool changed = false;
            // Settings this PC's graphics card can't use, from another PC or an older launcher: back to their default.
            if (!Gpu.Rtx && S.Get("LD_AA") != "") { S.Set("LD_AA", ""); changed = true; }
            if (S.Get("LD_FG_METHOD") == "dlss" && !(Gpu.Rtx && Gpu.RtxFrameGen)) { S.Set("LD_FG_METHOD", ""); changed = true; }
            // One Resolution. The game is always told 1080p (its mode with the sharp HUD) and the renderer scales from
            // there; an older "720p mode" setting becomes the 720p choice.
            if (S.Get("PS3_VIDEO_MODE") == "720") { S.Set("PS3_VIDEO_MODE", "1080"); if (S.Get("LD_RES_TARGET") == "") S.Set("LD_RES_TARGET", "720"); changed = true; }
            // Vulkan saved as the renderer but not in gt5.exe (an older launcher's choice, or a build that left it
            // out): DirectX 12, which is what play.bat starts then anyway.
            if (S.Get("GT5_RENDERER") == "vulkan" && !VulkanBuilt()) { S.Set("GT5_RENDERER", ""); changed = true; }
            if (NormalizeFrameGen()) changed = true;
            // Triggers: "ps3" was v0.1.108's name for the game's own layout, which is the default ("") again.
            if (S.Get("PAD_TRIGGERS").Trim().ToLowerInvariant() == "ps3") { S.Set("PAD_TRIGGERS", ""); changed = true; }
            // The half-rate mod's tick (Game page, GT5_MOD_HALF_RATE) is Frame rate's 30 now: a tick that was on
            // becomes 30 where Frame rate was left unset, and the line goes either way.
            if (S.V.ContainsKey("GT5_MOD_HALF_RATE"))
            {
                if (S.Get("GT5_MOD_HALF_RATE") == "1" && S.Get("GT5_SPEED") == "") S.Set("GT5_SPEED", "30");
                S.V.Remove("GT5_MOD_HALF_RATE");
                changed = true;
            }
            // Frame rate is 60 ("") or 30. A test build's other values: "30fg" is 30, "60" and "auto" are 60.
            string sp = S.Get("GT5_SPEED").Trim().ToLowerInvariant();
            if (sp == "30fg") { S.Set("GT5_SPEED", "30"); changed = true; }
            else if (sp == "60" || sp == "auto") { S.Set("GT5_SPEED", ""); changed = true; }
            // Speed lines an older launcher left after the keys (its 30 fps): saved again without them.
            if (S.HasSpeedLines) changed = true;
            if (changed) SaveQuiet();
        }

        // The settings file again, when it changed since the launcher last read or wrote it (the game's F1 menu writes
        // it too): every list and tick box shows what is in it now.
        void ReloadSettings()
        {
            if (S == null || !S.ChangedOnDisk() || !S.Reload()) return;
            Normalize();
            bool zh = S.Get("GT5_LAUNCHER_LANG") == "zh";
            if (zh != T.Zh) { T.Zh = zh; string pg = page; Build(); ShowPage(pg); return; }
            RefreshControls();
        }

        void RefreshControls()
        {
            comboQuiet = true;
            try { foreach (var a in refreshers) a(); }
            finally { comboQuiet = false; }
            if (keyMap != null) { keyMap = KeyBinds.Parse(S.Get("PS3_KEYS")); ShowKeys(); }
            ApplyRows();
            if (playRefresh != null) playRefresh();
        }

        // -- layout --
        void Build()
        {
            Controls.Clear(); pages.Clear(); navButtons.Clear(); combos.Clear(); addonRows.Clear(); rows.Clear();
            builtTop.Clear(); refreshers.Clear(); playRefresh = null; keyMap = null;
            // The pages of the Build before (the launcher's language changed) go. They used to stay in the page area,
            // and the one that was showing kept covering the new pages.
            var old = new List<Control>();
            foreach (Control c in content.Controls) if (c != strip) old.Add(c);
            content.Controls.Clear();
            foreach (var c in old) c.Dispose();
            var header = new Panel { Dock = DockStyle.Top, Height = 118, BackColor = Look.Bg };
            header.Paint += PaintHeader;
            var title = new Label { Text = "GRAN TURISMO 5", Font = Look.F(22f, FontStyle.Bold), ForeColor = Look.Text, AutoSize = true, BackColor = Color.Transparent, Location = new Point(logo != null ? 230 : 28, 26) };
            var sub = new Label { Text = T.Get("subtitle"), Font = Look.F(11f, FontStyle.Regular), ForeColor = Look.Dim, AutoSize = true, BackColor = Color.Transparent, Location = new Point(title.Left + 3, 68) };
            versionLabel = new Label { Font = Look.F(9.5f, FontStyle.Regular), ForeColor = Look.Dim, AutoSize = true, BackColor = Color.Transparent };
            updateBadge = new Label { Font = Look.F(9.5f, FontStyle.Bold), ForeColor = Color.White, BackColor = Look.Blue, AutoSize = true, Padding = new Padding(8, 4, 8, 4), Cursor = Cursors.Hand, Visible = false };
            updateBadge.Click += delegate { ShowPage("nav.updates"); };
            header.Controls.Add(title); header.Controls.Add(sub); header.Controls.Add(versionLabel); header.Controls.Add(updateBadge);
            header.Resize += delegate { PlaceHeaderRight(header); };

            var nav = new Panel { Dock = DockStyle.Left, Width = 180, BackColor = Look.Nav, Padding = new Padding(0, 8, 0, 0) };
            string[] names = { "nav.play", "nav.display", "nav.controls", "nav.game", "nav.updates", "nav.help" };
            int y = 10;
            foreach (var n in names)
            {
                var b = new FlatButton("   " + T.Get(n), Look.Nav, Look.NavHot) { TextAlign = ContentAlignment.MiddleLeft, Font = Look.F(11f, FontStyle.Regular), Size = new Size(180, 44), Location = new Point(0, y) };
                string key = n;
                b.Click += delegate { ShowPage(key); };
                nav.Controls.Add(b); navButtons[n] = b; y += 46;
            }

            var bottom = new Panel { Dock = DockStyle.Bottom, Height = 84, BackColor = Look.Panel };
            playButton = new FlatButton(T.Get("play"), Look.Accent, Look.AccentHot) { Font = Look.F(18f, FontStyle.Bold), Size = new Size(230, 56), Anchor = AnchorStyles.Right | AnchorStyles.Top };
            playButton.Click += delegate { Play(); };
            status = new Label { ForeColor = Look.Dim, AutoSize = false, Location = new Point(24, 18), Size = new Size(500, 48), Text = T.Get("ready"), TextAlign = ContentAlignment.MiddleLeft };
            bottom.Controls.Add(playButton); bottom.Controls.Add(status);
            bottom.Resize += delegate { playButton.Location = new Point(bottom.Width - playButton.Width - 24, 14); status.Width = Math.Max(100, playButton.Left - 48); };
            BuildReportLine(bottom);

            content.Dock = DockStyle.Fill; content.BackColor = Look.Bg; content.Padding = new Padding(28, 18, 28, 10);
            Controls.Add(content); Controls.Add(nav); Controls.Add(bottom); Controls.Add(header);

            addons = Addons();
            pages["nav.play"] = BuildPlayPage();
            pages["nav.display"] = BuildDisplayPage();
            pages["nav.controls"] = BuildControlsPage();
            pages["nav.game"] = BuildGamePage();
            pages["nav.updates"] = BuildUpdatesPage();
            pages["nav.help"] = BuildHelpPage();
            foreach (var p in pages.Values)
            {
                foreach (Control c in p.Controls) builtTop[c] = c.Top;   // where each one was put, before any row hides
                p.Dock = DockStyle.Fill; p.Visible = false;
                // Up and down only: the pages are never wider than the window, but when the task strip below made a
                // page shorter, WinForms added a horizontal scroll bar that then stayed.
                p.HorizontalScroll.Maximum = 0; p.HorizontalScroll.Visible = false;
                p.AutoScroll = true;
                DarkScroll(p); content.Controls.Add(p);
            }
            // The strip is docked first (last in the list), so the page fills what's left above it.
            content.Controls.Add(strip);
            strip.SendToBack();
            DarkScroll(notes);
            RefreshVersion();
            PlaceHeaderRight(header);
            bottom.Width = ClientSize.Width;
            ApplyRows();
            RefreshPlayButton();
        }

        // -- the window's size and place, kept in .gt5\launcher-window.txt ("x y width height max") --
        string PlacementFile { get { return Path.Combine(P.Root ?? P.Gt5, ".gt5", "launcher-window.txt"); } }

        void LoadPlacement()
        {
            try
            {
                var f = File.ReadAllText(PlacementFile).Split(new[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
                if (f.Length < 4) return;
                var r = new Rectangle(int.Parse(f[0], CultureInfo.InvariantCulture), int.Parse(f[1], CultureInfo.InvariantCulture),
                                      int.Parse(f[2], CultureInfo.InvariantCulture), int.Parse(f[3], CultureInfo.InvariantCulture));
                r.Width = Math.Max(r.Width, MinimumSize.Width); r.Height = Math.Max(r.Height, MinimumSize.Height);
                // Only where it can be seen and dragged: its title bar on a screen there now (a second screen may be gone).
                var bar = new Rectangle(r.X + 40, r.Y, Math.Max(1, r.Width - 80), 30);
                bool seen = false;
                foreach (var s in Screen.AllScreens) if (s.WorkingArea.IntersectsWith(bar)) seen = true;
                if (!seen) return;
                StartPosition = FormStartPosition.Manual;
                Bounds = r;
                if (f.Length > 4 && f[4] == "max") WindowState = FormWindowState.Maximized;
            }
            catch { }
        }

        void SavePlacement()
        {
            if (shots) return;
            try
            {
                var r = WindowState == FormWindowState.Normal ? Bounds : RestoreBounds;
                if (r.Width < 100 || r.Height < 100) return;
                Directory.CreateDirectory(Path.GetDirectoryName(PlacementFile));
                File.WriteAllText(PlacementFile, string.Format(CultureInfo.InvariantCulture, "{0} {1} {2} {3}{4}", r.X, r.Y, r.Width, r.Height,
                                  WindowState == FormWindowState.Maximized ? " max" : ""));
            }
            catch { }
        }

        // -- rows: greyed for the renderer chosen, hidden while they mean nothing (as in the game's menu) --
        // The controls added to page p since `from` belong to the row of `key`, which spans y0..y1 of the page.
        void RowControls(string key, Panel p, int from, int y0, int y1)
        {
            Row r;
            if (!rows.TryGetValue(key, out r)) { rows[key] = r = new Row { Key = key, Page = p, Top = y0, Bottom = y1 }; }
            r.Top = Math.Min(r.Top, y0); r.Bottom = Math.Max(r.Bottom, y1);
            for (int i = from; i < p.Controls.Count; i++) r.Ctl.Add(p.Controls[i]);
        }

        // Vulkan chosen (and in this gt5.exe): its row is greyed for each setting it does nothing with yet.
        bool RowOff(string key)
        {
            return key != null && VulkanChosen() && Array.IndexOf(VulkanLacks, key) >= 0;
        }
        bool VulkanChosen() { return S.Get("GT5_RENDERER") == "vulkan" && VulkanBuilt(); }

        static bool IsOn(string v)
        {
            v = (v ?? "").Trim().ToLowerInvariant();
            return v != "" && v != "0" && v != "off" && v != "no" && v != "false";
        }
        // Frame generation as the game reads it (rsx_framegen.c): LD_FRAMEGEN set, or a multiplier of 2 or more.
        bool FgOn()
        {
            int m;
            return IsOn(S.Get("LD_FRAMEGEN")) || (int.TryParse(S.Get("LD_FRAMEGEN_MULT"), out m) && m >= 2);
        }
        // AMD's and NVIDIA's generators make one frame between two (2x): the one named, or the one Auto takes here
        // (rsx_framegen.c, fg_method_auto: NVIDIA's where it runs and is added, then AMD's when added, else built-in).
        bool FgOneFrame()
        {
            string m = S.Get("LD_FG_METHOD").ToLowerInvariant();
            if (m == "fsr" || m == "dlss") return true;
            if (m != "") return false;
            var dlss = addons.Find(delegate (Addon a) { return a.Id == "dlss"; });
            var fsr = addons.Find(delegate (Addon a) { return a.Id == "fsr"; });
            return (dlss != null && Gpu.RtxFrameGen && dlss.Present()) || (fsr != null && fsr.Present());
        }

        // A row that means nothing while another setting has some value is not shown, as in the game's menu
        // (gt5_menu_ui.cpp, row_idle_reason): nothing greyed out, no reasons to read.
        bool RowIdle(string key)
        {
            switch (key)
            {
                case "renderer.note": return !VulkanChosen();
                // 30 is the half-rate mod's switch: no row without it (as the game's menu leaves it out then).
                case "GT5_SPEED": return !File.Exists(Path.Combine(P.Mods, "half-rate", "mod.json"));
                case "LD_FG_METHOD": return !FgOn();
                case "LD_FRAMEGEN_MULT": return !FgOn() || FgOneFrame();
                case "PS3_WINDOW_SIZE":
                    {
                        string w = S.Get("GT5_LAUNCH_WINDOW");
                        return w != "" && !w.Equals("windowed", StringComparison.OrdinalIgnoreCase);
                    }
                case "PS3_MOUSE_STEER_SENS": case "PS3_MOUSE_STEER_CENTER": return !IsOn(S.Get("PS3_MOUSE_STEER"));
                case "PS3_WHEEL_RANGE": case "PS3_WHEEL_FFB": case "PS3_WHEEL_FFB_INVERT":
                    {
                        string w = S.Get("PS3_WHEEL").Trim().ToLowerInvariant();
                        return w == "off" || w == "0" || w == "no" || IsOn(S.Get("PS3_MOUSE_STEER"));
                    }
            }
            return false;
        }

        // Every row as the settings are now: hidden or shown, greyed or not, and the rows below a hidden one moved up.
        void ApplyRows()
        {
            var addonCtl = new HashSet<Control>();
            foreach (var a in addonRows.Values) { addonCtl.Add(a.Add); addonCtl.Add(a.Tick); addonCtl.Add(a.Added); addonCtl.Add(a.Remove); }
            foreach (var r in rows.Values)
            {
                bool hide = RowIdle(r.Key), off = RowOff(r.Key);
                foreach (var c in r.Ctl)
                {
                    if (hide) c.Visible = false;
                    else if (!addonCtl.Contains(c)) c.Visible = true;   // Add or Added/Remove: ShowAddonRow
                    if (!(c is FlatButton)) c.Enabled = !off;            // add-on buttons: ShowAddonRow
                }
            }
            foreach (var a in addonRows.Values) { Row r; if (!rows.TryGetValue(a.A.ForKey ?? "", out r) || !RowIdle(r.Key)) ShowAddonRow(a); }
            foreach (var p in pages.Values) Relayout(p);
            foreach (var cb in combos.Values) cb.Invalidate();
        }

        // Each control at its built place, less the height of every hidden row above it on its page. A page that has
        // been scrolled counts from its scroll position (a hidden control isn't moved when the page scrolls).
        void Relayout(Panel p)
        {
            var hidden = new List<Row>();
            foreach (var r in rows.Values) if (r.Page == p && RowIdle(r.Key)) hidden.Add(r);
            int sy = p.AutoScrollPosition.Y;
            p.SuspendLayout();
            foreach (Control c in p.Controls)
            {
                int b;
                if (!builtTop.TryGetValue(c, out b)) continue;
                int up = 0;
                foreach (var r in hidden) if (r.Top <= b && !r.Ctl.Contains(c)) up += r.Bottom - r.Top;
                int top = b - up + sy;
                if (c.Top != top) c.Top = top;
            }
            p.ResumeLayout();
        }

        void PlaceHeaderRight(Panel header)
        {
            versionLabel.Location = new Point(header.Width - versionLabel.Width - 28, 30);
            updateBadge.Location = new Point(header.Width - updateBadge.Width - 28, 62);
        }

        void PaintHeader(object sender, PaintEventArgs e)
        {
            var g = e.Graphics; var r = ((Control)sender).ClientRectangle;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using (var br = new LinearGradientBrush(r, Color.FromArgb(30, 34, 48), Look.Bg, LinearGradientMode.Vertical)) g.FillRectangle(br, r);
            // A few racing stripes on the right.
            using (var p1 = new SolidBrush(Color.FromArgb(40, Look.Accent)))
            using (var p2 = new SolidBrush(Color.FromArgb(28, Look.Blue)))
            {
                for (int i = 0; i < 3; i++)
                {
                    int x = r.Width - 420 + i * 46;
                    g.FillPolygon(i % 2 == 0 ? (Brush)p1 : p2, new[] { new Point(x, r.Height), new Point(x + 30, r.Height), new Point(x + 150, 0), new Point(x + 120, 0) });
                }
            }
            if (logo != null)
            {
                int h = 96, w = (int)(logo.Width * (h / (float)logo.Height));
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.DrawImage(logo, new Rectangle(24, 11, w, h));
            }
            using (var pen = new Pen(Look.Accent, 3)) g.DrawLine(pen, 0, r.Height - 2, r.Width, r.Height - 2);
        }

        void ShowPage(string key)
        {
            page = key;
            if (keyWait != null) { keyWait = null; ShowKeys(); }
            foreach (var kv in pages) kv.Value.Visible = kv.Key == key;
            foreach (var kv in navButtons)
            {
                bool on = kv.Key == key;
                kv.Value.Normal = on ? Look.NavHot : Look.Nav; kv.Value.BackColor = kv.Value.Normal;
                kv.Value.ForeColor = on ? Color.White : Look.Dim;
                kv.Value.Font = Look.F(11f, on ? FontStyle.Bold : FontStyle.Regular);
            }
        }

        // -- page helpers --
        Panel NewPage() { return new Panel { BackColor = Look.Bg }; }
        int AddHeading(Panel p, string text, int y)
        {
            p.Controls.Add(new Label { Text = text, Font = Look.F(13f, FontStyle.Bold), ForeColor = Look.Text, AutoSize = true, Location = new Point(0, y) });
            return y + 34;
        }
        int AddHelp(Panel p, string text, int y, int x)
        {
            if (string.IsNullOrEmpty(text)) return y + 6;   // nothing to say: the label is enough
            var l = new Label { Text = text, ForeColor = Look.Dim, Font = Look.F(8.75f, FontStyle.Regular), Location = new Point(x, y), MaximumSize = new Size(700 - x, 0), AutoSize = true };
            p.Controls.Add(l);
            return y + l.PreferredHeight + 12;
        }
        // Frame generation as older versions wrote it (a multiplier alone meant "on", and LD_FRAMEGEN could name
        // the generator), put the way the game's menu writes it: LD_FRAMEGEN = 1 for on with a multiplier of 2 or
        // more, LD_FG_METHOD for the generator (empty = automatic). On stays on and off stays off. True: changed.
        bool NormalizeFrameGen()
        {
            string on = S.Get("LD_FRAMEGEN"), mult = S.Get("LD_FRAMEGEN_MULT"), how = S.Get("LD_FG_METHOD");
            bool multOn = mult == "2" || mult == "3" || mult == "4", changed = false;
            if (on != "" && on != "1")
            {
                if (how == "" && (on == "2" || on == "fsr" || on == "dlss")) S.Set("LD_FG_METHOD", on);
                S.Set("LD_FRAMEGEN", IsOn(on) || on == "2" || on == "fsr" || on == "dlss" ? "1" : ""); on = S.Get("LD_FRAMEGEN"); changed = true;
            }
            if (on == "" && multOn) { S.Set("LD_FRAMEGEN", "1"); on = "1"; changed = true; }
            if (on == "1" && !multOn) { S.Set("LD_FRAMEGEN_MULT", "2"); changed = true; }
            return changed;
        }

        // A value the list doesn't have (written by hand, or by a newer game menu): shown as it is, not as the first choice.
        static string Unlisted(string key, string v)
        {
            if (key == "LD_AA" && v.Equals("dlss_c", StringComparison.OrdinalIgnoreCase)) return T.Get("aa.c");
            Match m = Regex.Match(v, @"^(\d+)\s*[xX]\s*(\d+)$");
            return m.Success ? m.Groups[1].Value + " x " + m.Groups[2].Value : v;
        }
        // Two spellings of one value: a target of 1080 lines is the game's own 1080p mode (scale 1), an empty screen
        // shape is Auto.
        static string Same(string key, string v)
        {
            if (key == "LD_RES_TARGET" && v == "1080") return "";
            if (key == "PS3_ASPECT" && v == "") return "auto";
            return v;
        }
        static int SelectValue(ComboBox cb, string key, string cur)
        {
            cur = Same(key, cur);
            for (int i = 0; i < cb.Items.Count; i++) if (string.Equals(Same(key, ((Choice)cb.Items[i]).Value), cur, StringComparison.OrdinalIgnoreCase)) return i;
            if (cur == "") return 0;
            cb.Items.Add(new Choice(cur, Unlisted(key, cur)));
            return cb.Items.Count - 1;
        }

        int AddCombo(Panel p, string labelKey, string helpKey, string key, Choice[] choices, int y)
        {
            int c0 = p.Controls.Count, y0 = y;
            y = AddComboRow(p, labelKey, helpKey, key, choices, y);
            RowControls(key, p, c0, y0, y);
            return y;
        }
        int AddComboRow(Panel p, string labelKey, string helpKey, string key, Choice[] choices, int y)
        {
            p.Controls.Add(new Label { Text = T.Get(labelKey), AutoSize = true, Location = new Point(0, y + 4), ForeColor = Look.Text });
            var cb = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, FlatStyle = FlatStyle.Flat, BackColor = Look.Panel, ForeColor = Look.Text, Location = new Point(250, y), Width = 300,
                                    DrawMode = DrawMode.OwnerDrawFixed, ItemHeight = 22 };
            cb.DrawItem += DrawDarkItem;
            foreach (var c in choices) cb.Items.Add(c);
            var prev = new[] { SelectValue(cb, key, S.Get(key)) };
            cb.SelectedIndex = prev[0];
            combos[key] = cb;
            refreshers.Add(delegate { prev[0] = SelectValue(cb, key, S.Get(key)); if (cb.SelectedIndex != prev[0]) cb.SelectedIndex = prev[0]; });
            cb.SelectedIndexChanged += delegate
            {
                if (comboQuiet) return;
                var ch = (Choice)cb.SelectedItem;
                // A choice whose add-on isn't there (a DLSS mode without NVIDIA DLSS) is greyed: picking it asks
                // nothing; the choice goes back and the add-on's row is pointed at.
                if (ch.Off) { PutBack(cb, prev[0]); PointAt(ch.Needs); return; }
                prev[0] = cb.SelectedIndex;
                // Frame generation is one switch, as in the game's menu: On gives 2x unless a multiplier is chosen,
                // Off clears both.
                if (key == "LD_FRAMEGEN")
                {
                    int m;
                    if (ch.Value == "") S.Set("LD_FRAMEGEN_MULT", "");
                    else if (!int.TryParse(S.Get("LD_FRAMEGEN_MULT"), out m) || m < 2) S.Set("LD_FRAMEGEN_MULT", "2");
                }
                S.Set(key, ch.Value); SaveQuiet();
                if (key == "GT5_LAUNCHER_LANG")   // the launcher's own language: redraw everything in it
                {
                    BeginInvoke((MethodInvoker)delegate { T.Zh = S.Get(key) == "zh"; string pg = page; Build(); ShowPage(pg); if (latestTag != "") CheckForUpdates(false); });
                    return;
                }
                // Rows that depend on this one appear or go, other lists follow (Frame generation sets the multiplier).
                BeginInvoke((MethodInvoker)RefreshControls);
            };
            p.Controls.Add(cb);
            y += 34;
            return helpKey != null ? AddHelp(p, T.Get(helpKey), y - 2, 250) : y + 6;
        }
        // A number typed or stepped with the arrows (Up/Down, the mouse wheel); the default value saves as nothing.
        int AddNumber(Panel p, string labelKey, string helpKey, string key, decimal min, decimal max, decimal step, decimal def, int decimals, string unit, int y)
        {
            int c0 = p.Controls.Count, y0 = y;
            p.Controls.Add(new Label { Text = T.Get(labelKey), AutoSize = true, Location = new Point(0, y + 4), ForeColor = Look.Text });
            var n = new NumericUpDown { Minimum = min, Maximum = max, Increment = step, DecimalPlaces = decimals, BackColor = Look.Panel, ForeColor = Look.Text,
                                        BorderStyle = BorderStyle.FixedSingle, Location = new Point(250, y), Width = 90, TextAlign = HorizontalAlignment.Right };
            Func<decimal> stored = delegate
            {
                decimal cur;
                string s = S.Get(key).Trim().Replace(',', '.');
                if (s == "" || !decimal.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out cur)) cur = def;
                return Math.Max(min, Math.Min(max, cur));
            };
            n.Value = stored();
            n.ValueChanged += delegate
            {
                if (comboQuiet) return;
                S.Set(key, n.Value == def ? "" : n.Value.ToString(decimals > 0 ? "0.#" : "0", CultureInfo.InvariantCulture)); SaveQuiet();
            };
            refreshers.Add(delegate { decimal v = stored(); if (n.Value != v) n.Value = v; });
            p.Controls.Add(n);
            p.Controls.Add(new Label { Text = unit, AutoSize = true, Location = new Point(346, y + 4), ForeColor = Look.Dim });
            y += 34;
            y = helpKey != null ? AddHelp(p, T.Get(helpKey), y - 2, 250) : y + 6;
            RowControls(key, p, c0, y0, y);
            return y;
        }
        int AddCheck(Panel p, string labelKey, string helpKey, string key, string onValue, int y)
        {
            int c0 = p.Controls.Count, y0 = y;
            var c = new CheckBox { Text = T.Get(labelKey), AutoSize = true, Location = new Point(0, y), ForeColor = Look.Text, Checked = S.Get(key) == onValue };
            c.CheckedChanged += delegate
            {
                if (comboQuiet) return;
                S.Set(key, c.Checked ? onValue : ""); SaveQuiet();
                BeginInvoke((MethodInvoker)RefreshControls);   // rows that depend on it (mouse steering's speed)
            };
            refreshers.Add(delegate { bool on = S.Get(key) == onValue; if (c.Checked != on) c.Checked = on; });
            p.Controls.Add(c);
            y += 30;
            y = helpKey != null ? AddHelp(p, T.Get(helpKey), y - 4, 20) : y + 4;
            RowControls(key, p, c0, y0, y);
            return y;
        }
        // A heading or a line of its own that no setting owns, hidden by RowIdle(key) like a row.
        void Own(string key, Panel p, int c0, int y0, int y1) { RowControls(key, p, c0, y0, y1); }
        void SaveQuiet() { try { S.Save(); } catch { } }

        // A combo set back by the launcher, without saving or asking anything.
        void PutBack(ComboBox cb, int i)
        {
            comboQuiet = true;
            try { cb.SelectedIndex = i; } finally { comboQuiet = false; }
        }

        // Sets a setting's combo to the choice with this value, as if picked (saved), or just the setting when it
        // has no combo now.
        void SetChoice(string key, string value)
        {
            ComboBox cb;
            if (combos.TryGetValue(key, out cb) && !cb.IsDisposed)
                for (int i = 0; i < cb.Items.Count; i++)
                    if (((Choice)cb.Items[i]).Value == value) { if (cb.SelectedIndex != i) cb.SelectedIndex = i; return; }
            S.Set(key, value); SaveQuiet();
        }

        // Dark dropdowns: Windows draws DropDownList boxes in light colours unless they're drawn by hand. A choice
        // whose add-on isn't there is drawn greyed.
        static void DrawDarkItem(object sender, DrawItemEventArgs e)
        {
            var cb = (ComboBox)sender;
            bool hot = (e.State & DrawItemState.Selected) != 0 && (e.State & DrawItemState.ComboBoxEdit) == 0;
            var ch = e.Index >= 0 ? cb.Items[e.Index] as Choice : null;
            bool off = (ch != null && ch.Off) || !cb.Enabled;
            // The highlighted choice in blue, as in the game's menu (red is kept for a real failure).
            using (var b = new SolidBrush(hot ? (off ? Look.NavHot : Look.Blue) : Look.Panel)) e.Graphics.FillRectangle(b, e.Bounds);
            if (e.Index >= 0)
                TextRenderer.DrawText(e.Graphics, cb.Items[e.Index].ToString(), cb.Font, new Rectangle(e.Bounds.X + 6, e.Bounds.Y, e.Bounds.Width - 6, e.Bounds.Height),
                                      off ? Color.FromArgb(98, 103, 118) : Look.Text, TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
        }

        [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr h, int attr, ref int v, int size);
        [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)] static extern int SetWindowTheme(IntPtr h, string app, string id);
        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            try { int on = 1; if (DwmSetWindowAttribute(Handle, 20, ref on, 4) != 0) DwmSetWindowAttribute(Handle, 19, ref on, 4); } catch { }
        }
        static void DarkScroll(Control c)
        {
            c.HandleCreated += delegate { try { SetWindowTheme(c.Handle, "DarkMode_Explorer", null); } catch { } };
        }

        // -- pages --
        Panel BuildPlayPage()
        {
            var p = NewPage(); int y = 0;
            y = AddHeading(p, T.Get("nav.play"), y);
            var summary = new Label { ForeColor = Look.Text, AutoSize = true, Location = new Point(0, y), MaximumSize = new Size(700, 0), Font = Look.F(10.5f, FontStyle.Regular) };
            p.Controls.Add(summary);
            // Under the settings: how to reach the game's own settings, and what would stop the game (not installed,
            // its data missing or in pieces), in plain text: Play is greyed while it can't start.
            var menu = new Label { ForeColor = Look.Dim, AutoSize = true, Location = new Point(0, y + 150), MaximumSize = new Size(700, 0), Font = Look.F(10f, FontStyle.Regular) };
            var warn = new Label { ForeColor = Look.Text, AutoSize = true, Location = new Point(0, y + 186), MaximumSize = new Size(700, 0), Font = Look.F(10f, FontStyle.Regular) };
            p.Controls.Add(menu); p.Controls.Add(warn);
            Action refresh = delegate { summary.Text = Summary(); menu.Text = string.Format(T.Get("menukey"), MenuKeys()); warn.Text = Warning(); RefreshPlayButton(); };
            p.VisibleChanged += delegate { if (p.Visible) refresh(); };
            playRefresh = refresh;
            refresh();
            return p;
        }

        // The languages of the game's own list (gt5_settings_table.h, k_ch_lang).
        static Choice[] LangChoices()
        {
            return new[] {
                new Choice("", T.Get("lang.system")), new Choice("en", "English (US)"), new Choice("en-gb", "English (UK)"),
                new Choice("fr", "Français"), new Choice("es", "Español"), new Choice("de", "Deutsch"), new Choice("it", "Italiano"),
                new Choice("nl", "Nederlands"), new Choice("pt", "Português"), new Choice("pt-br", "Português (Brasil)"),
                new Choice("ru", "Русский"), new Choice("pl", "Polski"), new Choice("tr", "Türkçe"), new Choice("fi", "Suomi"),
                new Choice("sv", "Svenska"), new Choice("da", "Dansk"), new Choice("no", "Norsk"), new Choice("ja", "日本語"),
                new Choice("ko", "한국어"), new Choice("zh-t", "中文（繁體）"), new Choice("zh-s", "中文（简体）") };
        }
        static string LabelOf(Choice[] list, string v)
        {
            foreach (var c in list) if (string.Equals(c.Value, v, StringComparison.OrdinalIgnoreCase)) return c.Label;
            return v;
        }

        // The menu key (GT5_MENU_KEY, F1 when not set) and the controller's buttons for it (GT5_MENU_PAD).
        string MenuKeys()
        {
            string k = S.Get("GT5_MENU_KEY").Trim();
            k = k == "" ? "F1" : KeyBinds.Pretty(KeyBinds.Canon(k) ?? k);
            string pad = S.Get("GT5_MENU_PAD").Trim().ToLowerInvariant();
            if (pad == "off") return k;
            return k + " (" + (pad == "l3r3" ? "L3 + R3" : "Select + Start") + ")";
        }

        // What the game will start with, as it reads the settings.
        string Summary()
        {
            bool vk = VulkanChosen();
            // Vulkan draws the game's own 1080p mode for now (Resolution is greyed there).
            string t = Same("LD_RES_TARGET", S.Get("LD_RES_TARGET"));
            string res = vk || t == "" ? "1080p" : t == "720" ? "720p" : t == "1440" ? "1440p" : t == "2160" ? "4K" : t == "2880" ? "5K" : t;
            string wm = S.Get("GT5_LAUNCH_WINDOW").ToLowerInvariant();
            string win = wm == "borderless" ? T.Get("window.borderless") : wm == "max" ? T.Get("window.max") : T.Get("window.windowed");
            string size = S.Get("PS3_WINDOW_SIZE");
            if (wm != "borderless" && wm != "max" && size != "") win += ", " + Unlisted("PS3_WINDOW_SIZE", size);
            string wheel = IsOn(S.Get("PS3_MOUSE_STEER")) ? T.Get("mouse.short") : S.Get("PS3_WHEEL").ToLowerInvariant() == "off" ? T.Get("off") : T.Get("wheel.on");
            var sb = new StringBuilder();
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("renderer"), T.Get(vk ? "renderer.vk" : "renderer.dx12")));
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("res"), res));
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("window"), win));
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("speed"), T.Get(S.Get("GT5_SPEED") == "30" ? "speed.30" : "speed.full")));
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("wheel"), wheel));
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("lang"), LabelOf(LangChoices(), S.Get("PS3_LANG"))));
            return sb.ToString();
        }

        string Warning()
        {
            if (!File.Exists(P.Exe)) return T.Get("notbuilt");
            if (!File.Exists(P.Elf)) return T.Get("noelf");
            if (!File.Exists(P.Vol)) return T.Get(P.VolInPieces() ? "volparts.page" : "novol");
            return "";
        }

        // Play only when the game can start (and nothing else runs): a missing part is said on the Play page.
        bool CanStart() { return File.Exists(P.Exe) && File.Exists(P.Elf) && (File.Exists(P.Vol) || (P.VolInPieces() && File.Exists(P.FixFiles))); }
        void RefreshPlayButton()
        {
            if (playButton == null || playButton.IsDisposed) return;
            playButton.Enabled = !running && CanStart();
        }

        Panel BuildDisplayPage()
        {
            var p = NewPage(); int y = 0;
            y = AddHeading(p, T.Get("nav.display"), y);
            // The renderers this gt5.exe has: Vulkan is built in by setup and updates (build\SDL2.dll says so); a game
            // built without it (its libraries couldn't be had) lists DirectX 12 alone until the next update adds it.
            if (VulkanBuilt())
            {
                y = AddCombo(p, "renderer", null, "GT5_RENDERER", new[] { new Choice("", T.Get("renderer.dx12")), new Choice("vulkan", T.Get("renderer.vk")) }, y);
                // While Vulkan is chosen, one line says why some rows below are greyed.
                int c0 = p.Controls.Count, y0 = y;
                y = AddHelp(p, T.Get("renderer.dx12only"), y - 4, 250) + 4;
                Own("renderer.note", p, c0, y0, y);
            }
            else
            {
                p.Controls.Add(new Label { Text = T.Get("renderer"), AutoSize = true, Location = new Point(0, y + 4), ForeColor = Look.Text });
                p.Controls.Add(new Label { Text = T.Get("renderer.dx12"), AutoSize = true, Location = new Point(256, y + 4), ForeColor = Look.Text });
                y += 40;
            }
            // The rows in the game's order (its Display page, then its Performance page's Frame rate). One Resolution:
            // the game is always told 1080p (its mode with the sharp HUD) and the renderer scales from there.
            y = AddCombo(p, "res", null, "LD_RES_TARGET", new[] { new Choice("720", "720p"), new Choice("", "1080p"), new Choice("1440", "1440p"), new Choice("2160", "4K"), new Choice("2880", "5K") }, y);
            // Anti-aliasing (NVIDIA DLSS), on a PC with an NVIDIA RTX card only. It never asks: without the add-on its
            // modes are greyed, and the row under it adds it. (The game's other entries can't be picked anywhere yet.)
            var dlss = addons.Find(delegate (Addon a) { return a.Id == "dlss"; });
            if (dlss != null)
            {
                y = AddCombo(p, "aa", null, "LD_AA", new[] { new Choice("", T.Get("aa.off")), new Choice("dlss_q", T.Get("aa.q")) { Needs = dlss },
                    new Choice("dlss_b", T.Get("aa.b")) { Needs = dlss }, new Choice("dlss_p", T.Get("aa.p")) { Needs = dlss }, new Choice("dlss_u", T.Get("aa.u")) { Needs = dlss },
                    new Choice("dlaa", T.Get("aa.dlaa")) { Needs = dlss } }, y);
                y = AddAddonRow(p, dlss, y);   // part of the Anti-aliasing row (Addon.ForKey)
            }
            // Shadows: DirectX 12 draws the sun's shadow map at 2x (High, the default) or 4x with soft edges.
            y = AddCombo(p, "shadows", null, "LD_SHADOWS", new[] { new Choice("standard", T.Get("shadows.standard")), new Choice("", T.Get("shadows.high")), new Choice("ultra", T.Get("shadows.ultra")) }, y);
            // New shaders: wait for them, or build them in the background (DirectX 12; Vulkan always builds in the background).
            y = AddCombo(p, "shaders", null, "LD_SHADERS", ShadersBgDefault
                ? new[] { new Choice("", T.Get("shaders.bg")), new Choice("wait", T.Get("shaders.wait")) }
                : new[] { new Choice("", T.Get("shaders.wait")), new Choice("bg", T.Get("shaders.bg")) }, y);
            p.VisibleChanged += delegate { if (p.Visible) RefreshAddons(); };
            // Frame generation, as in the game's menu: one switch; the generator (automatic unless one is named;
            // NVIDIA's and AMD's each need their add-on) and the multiplier only while it is on.
            var fsr = addons.Find(delegate (Addon a) { return a.Id == "fsr"; });
            y = AddCombo(p, "framegen", null, "LD_FRAMEGEN", new[] { new Choice("", T.Get("fg.off")), new Choice("1", T.Get("fg.on")) }, y);
            // NVIDIA's generator only on a card that runs it (an RTX 40 series or newer).
            var gens = new List<Choice> { new Choice("", T.Get("fgmethod.auto")) };
            if (dlss != null && Gpu.RtxFrameGen) gens.Add(new Choice("dlss", "NVIDIA DLSS") { Needs = dlss });
            gens.Add(new Choice("fsr", "AMD FSR 3") { Needs = fsr });
            gens.Add(new Choice("2", T.Get("fgmethod.builtin")));
            y = AddCombo(p, "fgmethod", null, "LD_FG_METHOD", gens.ToArray(), y);
            y = AddAddonRow(p, fsr, y);   // part of the Generator row (Addon.ForKey)
            // 3x and 4x: the built-in generator (the others make one frame between two: the row goes, as in the game).
            y = AddCombo(p, "fgmult", null, "LD_FRAMEGEN_MULT", new[] { new Choice("2", "2x"), new Choice("3", "3x"), new Choice("4", "4x") }, y);
            y = AddCombo(p, "window", "window.help", "GT5_LAUNCH_WINDOW", new[] { new Choice("windowed", T.Get("window.windowed")), new Choice("max", T.Get("window.max")), new Choice("borderless", T.Get("window.borderless")) }, y);
            y = AddCombo(p, "size", "size.help", "PS3_WINDOW_SIZE", WindowSizes(), y);
            y = AddCombo(p, "shape", "shape.help", "PS3_ASPECT", new[] { new Choice("auto", T.Get("shape.auto")), new Choice("16:9", "16:9"), new Choice("21:9", "21:9"), new Choice("32:9", "32:9") }, y);
            // Frame rate: 30 fps is the game's own 30 frames a second, at real speed (its half rate: the game's menu,
            // Performance page; libs\video\rsx_frame_rate.c). Frame generation, above, doubles either.
            y = AddCombo(p, "speed", null, "GT5_SPEED", new[] { new Choice("", T.Get("speed.full")), new Choice("30", T.Get("speed.30")) }, y);
            return p;
        }

        // The game's window sizes that fit on one of this PC's screens (a size larger than every screen can't be shown).
        static Choice[] WindowSizes()
        {
            int mw = 0, mh = 0;
            foreach (var s in Screen.AllScreens) { mw = Math.Max(mw, s.Bounds.Width); mh = Math.Max(mh, s.Bounds.Height); }
            var l = new List<Choice> { new Choice("", T.Get("size.default")) };
            foreach (var z in new[] { new[] { "1280", "720", "" }, new[] { "1600", "900", "" }, new[] { "1920", "1080", "" }, new[] { "2560", "1440", "" },
                                      new[] { "2560", "1080", " (21:9)" }, new[] { "3440", "1440", " (21:9)" }, new[] { "3840", "2160", "" } })
                if (mw == 0 || (int.Parse(z[0]) <= mw && int.Parse(z[1]) <= mh)) l.Add(new Choice(z[0] + "x" + z[1], z[0] + " x " + z[1] + z[2]));
            return l.ToArray();
        }

        // "As in the game", then each step labelled with the text for its direction.
        Choice[] CockpitChoices(int[] steps, string plusKey, string minusKey)
        {
            var c = new List<Choice> { new Choice("", T.Get("cockpit.game")) };
            foreach (int v in steps)
                c.Add(new Choice(v.ToString(), string.Format(T.Get(v > 0 ? plusKey : minusKey), Math.Abs(v))));
            return c.ToArray();
        }

        Panel BuildControlsPage()
        {
            var p = NewPage(); int y = 0;
            y = AddHeading(p, T.Get("nav.controls"), y);
            // A controller's triggers while driving (cellPad.c, PAD_TRIGGERS): as on a PS3 pad (the game's own layout,
            // the default), or throttle and brake.
            y = AddCombo(p, "triggers", null, "PAD_TRIGGERS", new[] { new Choice("", T.Get("triggers.ps3")), new Choice("drive", T.Get("triggers.drive")) }, y);
            // The game's button symbols (GT5\src\gt5_glyphs.cpp, GT5_BUTTON_SYMBOLS): the game's own (the default), an
            // Xbox pad's, the keys bound below, or by the device used last. The same row as in the game's menu.
            y = AddCombo(p, "symbols", null, "GT5_BUTTON_SYMBOLS", new[] { new Choice("", T.Get("symbols.game")), new Choice("xbox", T.Get("symbols.xbox")),
                new Choice("keys", T.Get("symbols.keys")), new Choice("auto", T.Get("symbols.auto")) }, y);
            y = AddCheck(p, "padbg", null, "PAD_BACKGROUND", "1", y);
            y = AddCheck(p, "clip", null, "PAD_CURSOR_CLIP", "1", y);
            y = AddCombo(p, "wheel", "wheel.help", "PS3_WHEEL", new[] { new Choice("", T.Get("wheel.on")), new Choice("off", T.Get("wheel.off")) }, y + 8);
            y = AddCombo(p, "range", "range.help", "PS3_WHEEL_RANGE", new[] { new Choice("", T.Get("size.default")), new Choice("270", "270°"), new Choice("540", "540°"), new Choice("720", "720°"), new Choice("900", "900°"), new Choice("1080", "1080°") }, y);
            var ffb = new List<Choice> { new Choice("", T.Get("size.default")) };
            for (int v = 100; v >= 0; v -= 10) ffb.Add(new Choice(v.ToString(), v + "%"));
            y = AddCombo(p, "ffb", null, "PS3_WHEEL_FFB", ffb.ToArray(), y);
            y = AddCheck(p, "ffbinv", null, "PS3_WHEEL_FFB_INVERT", "1", y);
            y = AddCheck(p, "mouse", "mouse.help", "PS3_MOUSE_STEER", "1", y + 10);
            y = AddNumber(p, "msens", "msens.help", "PS3_MOUSE_STEER_SENS", 1m, 1000m, 0.5m, 100m, 1, "%", y);
            y = AddCombo(p, "mcenter", "mcenter.help", "PS3_MOUSE_STEER_CENTER", new[] { new Choice("", T.Get("off")), new Choice("50", T.Get("mcenter.slow")), new Choice("100", T.Get("mcenter.mid")), new Choice("200", T.Get("mcenter.fast")) }, y);
            y = AddKeyRows(p, y + 14);
            return p;
        }

        // -- keyboard keys: one row per PS3 button, labelled with what it does in GT5 --
        Dictionary<string, List<string>> keyMap;
        readonly Dictionary<string, FlatButton> keyButtons = new Dictionary<string, FlatButton>();
        string keyWait;        // the button waiting for a key, or null
        bool keyAdd;           // the key goes next to the ones there (right-click) instead of replacing them
        int keySwallowUp;      // the key just taken: its key-up isn't passed on (Alt's would open the menu)
        KeyCatcher catcher;

        // Takes the next key (or Mouse4/5) for the button waiting, before the controls see it: Tab, the
        // arrows and Alt never move the focus or open a menu while a row waits.
        class KeyCatcher : IMessageFilter
        {
            readonly LauncherForm f;
            public KeyCatcher(LauncherForm form) { f = form; }
            public bool PreFilterMessage(ref Message m)
            {
                const int KEYDOWN = 0x100, KEYUP = 0x101, CHAR = 0x102, SYSKEYDOWN = 0x104, SYSKEYUP = 0x105, SYSCHAR = 0x106,
                          XDOWN = 0x20B, XUP = 0x20C;
                int msg = m.Msg;
                if (f.keySwallowUp != 0 && (msg == KEYDOWN || msg == SYSKEYDOWN) && f.keyWait == null &&
                    (int)(m.WParam.ToInt64() & 0xFFFF) == f.keySwallowUp)
                    return true;   // the taken key repeating while it's held
                if (f.keySwallowUp != 0 && (msg == KEYUP || msg == SYSKEYUP || msg == CHAR || msg == SYSCHAR))
                {
                    if (msg == KEYUP || msg == SYSKEYUP) f.keySwallowUp = 0;
                    return true;
                }
                if (f.keyWait == null) return false;
                if (msg == KEYDOWN || msg == SYSKEYDOWN)
                {
                    int vk = (int)(m.WParam.ToInt64() & 0xFFFF);
                    f.keySwallowUp = vk;
                    if (vk == 0x1B) { f.KeyTaken(null); return true; }   // Esc: cancel (it's the game's quit key)
                    string k = KeyBinds.FromKey(vk, m.LParam.ToInt64());
                    if (k != null) f.KeyTaken(k);
                    return true;
                }
                if (msg == XDOWN) { f.KeyTaken(((m.WParam.ToInt64() >> 16) & 0xFFFF) == 1 ? "Mouse4" : "Mouse5"); return true; }
                if (msg == XUP || msg == KEYUP || msg == SYSKEYUP || msg == CHAR || msg == SYSCHAR) return true;
                return false;
            }
        }

        int AddKeyRows(Panel p, int y)
        {
            keyMap = KeyBinds.Parse(S.Get("PS3_KEYS"));
            keyButtons.Clear();
            keyWait = null;
            if (catcher == null) { catcher = new KeyCatcher(this); Application.AddMessageFilter(catcher); }
            y = AddHeading(p, T.Get("kb.title"), y);
            y = AddHelp(p, T.Get("kb.help"), y - 4, 0);
            var reset = new FlatButton(T.Get("kb.reset"), Look.Panel, Look.NavHot) { Size = new Size(170, 30), Location = new Point(0, y) };
            reset.Click += delegate { keyWait = null; keyMap = KeyBinds.Defaults(); SaveKeys(); };
            p.Controls.Add(reset);
            y += 42;
            foreach (var b in KeyBinds.Order)
            {
                if (b == "lstick_up") { y = AddHelp(p, T.Get("kb.stick"), y + 6, 0) - 4; }
                string btn = b;
                var action = new Label { Text = T.Get("kb.a." + b), AutoSize = false, Size = new Size(250, 26), Location = new Point(0, y + 2), ForeColor = Look.Text, TextAlign = ContentAlignment.MiddleLeft, Cursor = Cursors.Hand };
                var pad = new Label { Text = T.Get("kb.b." + b), AutoSize = false, Size = new Size(150, 26), Location = new Point(255, y + 2), ForeColor = Look.Dim, TextAlign = ContentAlignment.MiddleLeft, Cursor = Cursors.Hand };
                var key = new FlatButton("", Look.Panel, Look.NavHot) { Size = new Size(190, 28), Location = new Point(410, y + 1), TextAlign = ContentAlignment.MiddleLeft };
                var clear = new FlatButton("×", Look.Panel, Look.NavHot) { Size = new Size(30, 28), Location = new Point(604, y + 1) };
                MouseEventHandler start = delegate(object s, MouseEventArgs e)
                {
                    if (e.Button != MouseButtons.Left && e.Button != MouseButtons.Right) return;
                    keyWait = btn; keyAdd = e.Button == MouseButtons.Right; ShowKeys();
                };
                action.MouseUp += start; pad.MouseUp += start; key.MouseUp += start;
                clear.Click += delegate { keyWait = null; keyMap[btn].Clear(); SaveKeys(); };
                p.Controls.Add(action); p.Controls.Add(pad); p.Controls.Add(key); p.Controls.Add(clear);
                keyButtons[b] = key;
                y += 32;
            }
            y = AddHelp(p, T.Get("kb.views"), y + 8, 0);
            ShowKeys();
            return y;
        }

        void KeyTaken(string k)
        {
            string b = keyWait;
            keyWait = null;
            if (b != null && k != null)
            {
                foreach (var kv in keyMap) if (kv.Key != b) kv.Value.Remove(k);   // a key used elsewhere moves here
                if (!keyAdd) keyMap[b].Clear();
                if (!keyMap[b].Contains(k) && keyMap[b].Count < 4) keyMap[b].Add(k);
                SaveKeys();
                return;
            }
            ShowKeys();
        }

        void SaveKeys()
        {
            S.Set("PS3_KEYS", KeyBinds.Format(keyMap));
            SaveQuiet();
            ShowKeys();
        }

        void ShowKeys()
        {
            foreach (var kv in keyButtons)
            {
                var names = new List<string>();
                foreach (var k in keyMap[kv.Key]) names.Add(KeyBinds.Pretty(k));
                bool wait = kv.Key == keyWait;
                kv.Value.Text = wait ? T.Get("kb.press") : names.Count == 0 ? T.Get("kb.none") : string.Join("  /  ", names.ToArray());
                kv.Value.Normal = wait ? Look.Blue : Look.Panel;   // waiting for a key: blue, not a failure's red
                kv.Value.BackColor = kv.Value.Normal;
                kv.Value.ForeColor = names.Count == 0 && !wait ? Look.Dim : Look.Text;
            }
        }

        Panel BuildGamePage()
        {
            var p = NewPage(); int y = 0;
            y = AddHeading(p, T.Get("nav.game"), y);
            // In the game's order (its Game page): the cockpit view, the language, the opening movie.
            // Cockpit view (GT5\src\gt5_cockpit.cpp): "" = the game's own camera.
            p.Controls.Add(new Label { Text = T.Get("cockpit"), Font = Look.F(11f, FontStyle.Bold), ForeColor = Look.Text, AutoSize = true, Location = new Point(0, y + 2) });
            y += 34;
            y = AddCombo(p, "cfov", "cfov.help", "PS3_COCKPIT_FOV", CockpitChoices(new[] { -10, -5, 5, 10, 15, 20, 25, 30, 40 }, "cfov.wider", "cfov.narrower"), y);
            y = AddCombo(p, "cseat", "cseat.help", "PS3_COCKPIT_SEAT", CockpitChoices(new[] { -30, -20, -10, -5, 5, 10, 20, 30 }, "cseat.fwd", "cseat.back"), y);
            y = AddCombo(p, "cheight", null, "PS3_COCKPIT_HEIGHT", CockpitChoices(new[] { -10, -5, 5, 10 }, "cheight.up", "cheight.down"), y);
            y = AddCombo(p, "lang", "lang.help", "PS3_LANG", LangChoices(), y + 8);
            y = AddCheck(p, "intro", "intro.help", "PS3_SKIP_INTRO", "1", y + 4);
            // Sound, as on the game's Sound page (cellAudio.c): the volume of what reaches the speakers, 0-100 % in steps
            // of 5 (100 = nothing written), and quiet while another window is in front.
            y = AddHeading(p, T.Get("sound"), y + 10);
            y = AddNumber(p, "volume", null, "GT5_VOLUME", 0m, 100m, 5m, 100m, 0, "%", y);
            y = AddCheck(p, "bgmute", null, "GT5_MUTE_BACKGROUND", "1", y);
            // Mods built from GT5\mods: a tick each (GT5_MOD_<NAME>, 1 / 0). The section only when there is one. The
            // unlock-fps example is not offered: the game never goes past 60 with it (and its text says otherwise);
            // half-rate is Frame rate's 30 (Display page).
            var mods = LoadMods();
            if (mods.Count > 0) y = AddHeading(p, T.Get("mods"), y + 10);
            foreach (var m in mods)
            {
                var c = new CheckBox { Text = m.Name, AutoSize = true, Location = new Point(0, y), ForeColor = Look.Text };
                var mod = m;
                Func<bool> stored = delegate { string cur = S.Get(mod.Switch); return cur == "" ? mod.Default : cur == "1"; };
                c.Checked = stored();
                c.CheckedChanged += delegate { if (comboQuiet) return; S.Set(mod.Switch, c.Checked ? "1" : "0"); SaveQuiet(); };
                refreshers.Add(delegate { bool on = stored(); if (c.Checked != on) c.Checked = on; });
                p.Controls.Add(c); y += 30;
            }
            y = AddHeading(p, T.Get("launcher"), y + 10);
            y = AddCombo(p, "ui", null, "GT5_LAUNCHER_LANG", new[] { new Choice("", "English"), new Choice("zh", "中文（简体）") }, y);
            y = AddCheck(p, "hide", null, "GT5_LAUNCHER_HIDE", "1", y);
            y = AddCheck(p, "autocheck", null, "GT5_LAUNCHER_AUTOCHECK", "1", y);
            return p;
        }

        List<ModInfo> LoadMods()
        {
            var list = new List<ModInfo>();
            try
            {
                if (!Directory.Exists(P.Mods)) return list;
                var js = new JavaScriptSerializer();
                foreach (var dir in Directory.GetDirectories(P.Mods))
                {
                    string f = Path.Combine(dir, "mod.json");
                    if (!File.Exists(f)) continue;
                    try
                    {
                        var d = js.Deserialize<Dictionary<string, object>>(File.ReadAllText(f));
                        object o;
                        string name = d.TryGetValue("name", out o) && o != null ? o.ToString() : Path.GetFileName(dir);
                        if (name.Equals("unlock-fps", StringComparison.OrdinalIgnoreCase)) continue;
                        // half-rate is the Frame rate setting's 30 (Display page; the game switches it while it runs).
                        if (name.Equals("half-rate", StringComparison.OrdinalIgnoreCase)) continue;
                        // The ones the port ships have a few words of their own; another shows its name (its own
                        // description is for MODDING.md readers, not a row's caption).
                        string label = T.Get("mod." + name.ToLowerInvariant());
                        var m = new ModInfo { Name = label != "mod." + name.ToLowerInvariant() ? label : name, Description = "" };
                        m.Default = d.TryGetValue("default", out o) && o is bool && (bool)o;
                        m.Switch = "GT5_MOD_" + name.Replace("-", "_").ToUpperInvariant();
                        list.Add(m);
                    }
                    catch { }
                }
            }
            catch { }
            return list;
        }

        Panel BuildUpdatesPage()
        {
            var p = NewPage(); int y = 0;
            y = AddHeading(p, T.Get("nav.updates"), y);
            installedLabel = new Label { AutoSize = true, Location = new Point(0, y), ForeColor = Look.Text, Font = Look.F(10.5f, FontStyle.Regular) };
            latestLabel = new Label { AutoSize = true, Location = new Point(0, y + 28), ForeColor = Look.Text, Font = Look.F(10.5f, FontStyle.Regular) };
            p.Controls.Add(installedLabel); p.Controls.Add(latestLabel);
            y += 66;
            var check = new FlatButton(T.Get("upd.check"), Look.Panel, Look.NavHot) { Location = new Point(0, y), Size = new Size(150, 38) };
            check.Click += delegate { CheckForUpdates(true); };
            updateButton = new FlatButton(T.Get("upd.now"), Look.Blue, Color.FromArgb(90, 160, 240)) { Location = new Point(162, y), Size = new Size(170, 38), Font = Look.F(10.5f, FontStyle.Bold) };
            updateButton.Click += delegate { RunUpdate(); };
            p.Controls.Add(check); p.Controls.Add(updateButton);
            SetUpdateButton(latestTag != "" && Newer(latestTag, P.InstalledVersion()));
            y += 50;
            p.Controls.Add(new Label { Text = T.Get("upd.notes"), AutoSize = true, Location = new Point(0, y + 4), Font = Look.F(10.5f, FontStyle.Bold), ForeColor = Look.Text });
            notes = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, BackColor = Look.Panel, ForeColor = Look.Text, BorderStyle = BorderStyle.None, Location = new Point(0, y + 32), Size = new Size(700, 170), Font = Look.F(9f, FontStyle.Regular) };
            p.Controls.Add(notes);
            // The release notes take the room the window gives them (at least as before).
            var nb = notes;
            p.Resize += delegate
            {
                if (nb.IsDisposed) return;
                nb.Width = Math.Min(1100, Math.Max(700, p.ClientSize.Width - 4));   // wider than that is hard to read
                nb.Height = Math.Max(170, p.ClientSize.Height - (nb.Top - p.AutoScrollPosition.Y) - 8);
            };
            return p;
        }

        // Update now is the blue button to press only when a newer release is there; otherwise it is greyed.
        void SetUpdateButton(bool newer)
        {
            if (updateButton == null || updateButton.IsDisposed) return;
            updateButton.Enabled = newer;
            updateButton.Normal = newer ? Look.Blue : Look.Panel;
            updateButton.BackColor = updateButton.Normal;
            updateButton.ForeColor = newer ? Look.Text : Look.Dim;
        }

        Panel BuildHelpPage()
        {
            var p = NewPage(); int y = 0;
            y = AddHeading(p, T.Get("nav.help"), y);
            string[][] links = {
                new[] { "help.save", "save" }, new[] { "help.logs", "logs" }, new[] { "help.report", "https://github.com/MormonFTP-hub/gt5-recomp/issues/new/choose" },
                new[] { "help.readme", "https://github.com/MormonFTP-hub/gt5-recomp#faq" },
                new[] { "help.changes", "https://github.com/MormonFTP-hub/gt5-recomp/blob/main/CHANGELOG.md" },
                new[] { "help.discord", "https://discord.gg/xvhGcuD7Kk" },
                new[] { "help.shaders", "shaders" }, new[] { "help.pkg", "pkg" },
            };
            foreach (var l in links)
            {
                var b = new FlatButton(T.Get(l[0]), Look.Panel, Look.NavHot) { Location = new Point(0, y), Size = new Size(300, 40), TextAlign = ContentAlignment.MiddleLeft, Padding = new Padding(10, 0, 0, 0) };
                string target = l[1];
                b.Click += delegate { HelpAction(target); };
                p.Controls.Add(b); y += 48;
            }
            y = AddHelp(p, T.Get("help.about"), y + 6, 0);
            return p;
        }

        void HelpAction(string what)
        {
            try
            {
                if (what == "logs") { Directory.CreateDirectory(P.Logs); Process.Start("explorer.exe", "\"" + P.Logs + "\""); return; }
                if (what == "shaders") { ClearShaderCache(); return; }
                if (what == "pkg")
                {
                    if (strip.Busy) { status.Text = T.Get("busy"); return; }
                    if (GameRunning() || running) { strip.Note("help.pkg", "fail.game"); return; }
                    if (!File.Exists(PkgScript())) { strip.Note("help.pkg", "fail.update"); return; }
                    var dlg = new OpenFileDialog();
                    dlg.Filter = "PS3 packages (*.pkg)|*.pkg"; dlg.Multiselect = true; dlg.Title = T.Get("help.pkg");
                    if (dlg.ShowDialog(this) != DialogResult.OK || dlg.FileNames.Length == 0) return;
                    InstallPkg(dlg.FileNames);
                    return;
                }
                if (what == "save")
                {
                    string desk = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
                    var saved = new List<string>();
                    // The logs as one report file with this PC's names and folders taken out (ProblemReport), no longer
                    // as copies of the logs themselves (those named the Windows user in every path).
                    string lastErr = Path.Combine(P.Logs, "play.err");
                    if (File.Exists(lastErr) || File.Exists(Path.Combine(P.Logs, "play.out")) || File.Exists(Path.Combine(P.Logs, "update.log")))
                    {
                        Cursor = Cursors.WaitCursor;
                        try { saved.Add(Path.GetFileName(ProblemReport.Save(P, ProblemReport.Classify(lastErr, null, -1), desk, null))); }
                        finally { Cursor = Cursors.Default; }
                    }
                    // The list of what this PC's game has drawn (keys only, no game data): with it, the port's own list
                    // gets other players' tracks and cars. As text, one record a line, so a report form takes it.
                    try
                    {
                        string keys = Path.Combine(P.Gt5, "scratch", "shader_keys", "shader_keys.lst");
                        if (File.Exists(keys))
                        {
                            byte[] all = File.ReadAllBytes(keys);
                            const int rec = 496;
                            var sb = new StringBuilder();
                            for (int o = 0; o + rec <= all.Length; o += rec) sb.AppendLine(Convert.ToBase64String(all, o, rec));
                            string dst = Path.Combine(desk, "gt5-shader-keys.txt");
                            File.WriteAllText(dst, sb.ToString(), new UTF8Encoding(false));
                            saved.Add(Path.GetFileName(dst));
                        }
                    }
                    catch { }
                    MessageBox.Show(this, saved.Count == 0 ? T.Get("help.nologs") : string.Format(T.Get("help.saved"), string.Join(", ", saved.ToArray())), "GT5");
                    return;
                }
                Process.Start(what);
            }
            catch (Exception e) { MessageBox.Show(this, e.Message, "GT5"); }
        }

        // The compiled-shader cache (ClearShaders.bat does the same): GT5\scratch\rsx_shader_cache_v*, and the Linux
        // build's GT5\shadercache. It only saves compile time (a shader is reused only for the exact same source), so
        // clearing it is safe; the game compiles what it needs again.
        void ClearShaderCache()
        {
            if (GameRunning() || running) { MessageBox.Show(this, T.Get("shaders.running"), "GT5"); return; }
            var dirs = new List<string>();
            string scratch = Path.Combine(P.Gt5, "scratch");
            if (Directory.Exists(scratch)) dirs.AddRange(Directory.GetDirectories(scratch, "rsx_shader_cache_v*"));
            string vk = Path.Combine(P.Gt5, "shadercache");
            if (Directory.Exists(vk)) dirs.Add(vk);
            long bytes = 0; int files = 0;
            foreach (var d in dirs)
                foreach (var f in Directory.GetFiles(d, "*", SearchOption.AllDirectories))
                {
                    files++;
                    try { bytes += new FileInfo(f).Length; } catch { }
                }
            if (files == 0) { MessageBox.Show(this, T.Get("shaders.empty"), "GT5"); return; }
            if (MessageBox.Show(this, string.Format(T.Get("shaders.ask"), files, bytes / 1048576.0), "GT5",
                    MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
            int left = 0;
            foreach (var d in dirs) { try { Directory.Delete(d, true); } catch { left++; } }
            MessageBox.Show(this, T.Get(left == 0 ? "shaders.done" : "shaders.partly"), "GT5");
        }

        // -- version and updates --
        void RefreshVersion()
        {
            string v = P.InstalledVersion();
            if (v.StartsWith("development build")) v = T.Get("devbuild") + " " + v.Substring("development build".Length).Trim();
            // No version recorded (a copy not made by the installer): nothing, rather than a question mark.
            versionLabel.Text = v == "" ? "" : T.Get("version") + ": " + v;
            if (installedLabel != null) installedLabel.Text = v == "" ? "" : T.Get("upd.installed") + ":  " + v;
        }

        static int[] ParseVersion(string s)
        {
            var m = Regex.Match(s ?? "", "v?(\\d+)\\.(\\d+)\\.(\\d+)");
            if (!m.Success) return null;
            return new[] { int.Parse(m.Groups[1].Value), int.Parse(m.Groups[2].Value), int.Parse(m.Groups[3].Value) };
        }
        static bool Newer(string a, string b)   // a newer than b
        {
            var x = ParseVersion(a); var y = ParseVersion(b);
            if (x == null) return false; if (y == null) return true;
            for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] > y[i];
            return false;
        }

        // Release notes are Markdown: show them as plain text.
        static string PlainText(string md)
        {
            string s = (md ?? "").Replace("\r\n", "\n");
            // The release's footer (how to update by hand, from ship.ps1) is for the GitHub page: the launcher has its button.
            int foot = s.IndexOf("\n---\n**Updating:**", StringComparison.Ordinal);
            if (foot >= 0) s = s.Substring(0, foot);
            s = Regex.Replace(s, "\\[([^\\]]+)\\]\\([^)]+\\)", "$1");          // [text](link) -> text
            s = Regex.Replace(s, "(?m)^#{1,6}\\s*", "");                        // headings
            s = s.Replace("**", "").Replace("`", "");
            s = Regex.Replace(s, "(?m)^- ", "• ");
            s = Regex.Replace(s, "\n{3,}", "\n\n");
            return s.Trim().Replace("\n", "\r\n");
        }

        void CheckForUpdates(bool showPage)
        {
            if (latestLabel != null) latestLabel.Text = T.Get("upd.latest") + ":  " + T.Get("upd.checking");
            var th = new Thread(delegate ()
            {
                string tag = "", body = "", date = "", err = "";
                try
                {
                    ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072;   // TLS 1.2
                    using (var wc = new WebClient())
                    {
                        wc.Headers[HttpRequestHeader.UserAgent] = "GT5-Launcher";
                        wc.Encoding = Encoding.UTF8;
                        string json = wc.DownloadString("https://api.github.com/repos/MormonFTP-hub/gt5-recomp/releases/latest");
                        var d = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(json);
                        object o;
                        if (d.TryGetValue("tag_name", out o) && o != null) tag = o.ToString();
                        if (d.TryGetValue("body", out o) && o != null) body = o.ToString();
                        if (d.TryGetValue("published_at", out o) && o != null) date = o.ToString();
                    }
                }
                catch (Exception e) { err = e.Message; }
                try
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        latestTag = tag;
                        string inst = P.InstalledVersion();
                        if (err != "")
                        {
                            // The reason (a .NET message, English) goes nowhere a player reads: a few plain words.
                            latestLabel.Text = T.Get("upd.latest") + ":  " + T.Get("upd.failed");
                            return;
                        }
                        DateTime when;
                        string d2 = DateTime.TryParse(date, CultureInfo.InvariantCulture, DateTimeStyles.AdjustToUniversal, out when) ? when.ToString("d MMM yyyy", CultureInfo.InvariantCulture) : "";
                        bool newer = Newer(tag, inst);
                        latestLabel.Text = T.Get("upd.latest") + ":  " + tag + (d2 != "" ? "  (" + d2 + ")" : "") + "   " + (newer ? "" : T.Get("upd.uptodate"));
                        notes.Text = PlainText(body);
                        updateBadge.Text = string.Format(T.Get("upd.available"), tag);
                        updateBadge.Visible = newer;
                        SetUpdateButton(newer);
                        PlaceHeaderRight((Panel)updateBadge.Parent);
                        if (showPage) ShowPage("nav.updates");
                    });
                }
                catch { }
            });
            th.IsBackground = true; th.Start();
        }

        bool GameRunning()
        {
            try
            {
                foreach (var pr in Process.GetProcessesByName("gt5"))
                {
                    try { if (string.Equals(pr.MainModule.FileName, P.Exe, StringComparison.OrdinalIgnoreCase)) return true; } catch { }
                }
            }
            catch { }
            return false;
        }

        // build\SDL2.dll next to gt5.exe: the exe has the Vulkan renderer (GT5's CMakeLists copies it in when it
        // does and removes it when it doesn't).
        bool VulkanBuilt() { return File.Exists(Path.Combine(Path.GetDirectoryName(P.Exe), "SDL2.dll")); }

        // -- helpers: scripts run hidden, shown in the task strip --

        // One helper at a time; no Play while it runs (it may be rebuilding gt5.exe or putting files next to it).
        void StartJob(HelperJob j)
        {
            var inner = j.Ended;
            j.Ended = delegate (bool ok)
            {
                running = false;
                RefreshPlayButton();
                if (inner != null) inner(ok);
                if (playRefresh != null) playRefresh();   // joining the game's data may have made Play possible
            };
            running = true;
            playButton.Enabled = false;
            status.Text = T.Get(j.TitleKey) + "...";
            strip.Run(j);
            RefreshAddons();
        }

        // The window closes while a helper runs: ask in the strip first (it would go on with no window at all);
        // when Windows closes it, the helper is ended with it.
        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (strip.Busy && !closeNow)
            {
                if (e.CloseReason == CloseReason.UserClosing)
                {
                    e.Cancel = true;
                    strip.AskClose(delegate { closeNow = true; BeginInvoke((MethodInvoker)delegate { Close(); }); });
                    return;
                }
                strip.Kill();
            }
            SavePlacement();
            base.OnFormClosing(e);
        }

        // The add-ons on the Display page. A new one (AMD FSR 3 frame generation with add_fsr.ps1; DLSS Frame
        // Generation: add_dlss.ps1 again plus its one more file) is one more entry here, a Choice's Needs if a list
        // has choices that need it, and its texts in T.
        List<Addon> Addons()
        {
            string scripts = Path.Combine(P.Gt5, "scripts"), build = Path.GetDirectoryName(P.Exe);
            var list = new List<Addon>();
            // NVIDIA DLSS: add_dlss.ps1 fetches NVIDIA's SDK at a fixed release and builds the small plug-in. The row's
            // own line says that adding it accepts NVIDIA's licence, with the link, so Add is the acceptance: the
            // script runs with -Accept and asks nothing. GT5_DLSS_SDK=<folder> passes -SdkDir: a copy of the same SDK
            // release already on this PC, used instead of fetching.
            string dlssScript = Path.Combine(scripts, "add_dlss.ps1");
            var dlssArgs = new List<string> { "-Accept", "-NoPause" };
            string sdk = Environment.GetEnvironmentVariable("GT5_DLSS_SDK");
            if (!string.IsNullOrEmpty(sdk) && Directory.Exists(sdk)) { dlssArgs.Add("-SdkDir"); dlssArgs.Add(sdk); }
            // Only on a PC with an NVIDIA RTX card: DLSS runs on no other, so elsewhere it isn't offered at all.
            if (Gpu.Rtx) list.Add(new Addon
            {
                Id = "dlss", ForKey = "LD_AA", NameKey = "dlss.name", NoteKey = "dlss.note", NoteAddedKey = "dlss.note.added", Link = DlssLicence(dlssScript),
                Script = dlssScript, AddArgs = dlssArgs.ToArray(), RemoveArgs = new[] { "-Remove", "-NoPause" },
                Files = new[] { Path.Combine(build, "gt5_upscale_dlss.dll"), Path.Combine(build, "nvngx_dlss.dll") },
                AddTitleKey = "dlss.title.add", RemoveTitleKey = "dlss.title.remove", RemoveAskKey = "dlss.ask.remove", RemoveTextKey = "dlss.ask.remove.text",
                AddedKey = "dlss.added", NotAddedKey = "dlss.notadded", RemovedKey = "dlss.removed", NotRemovedKey = "dlss.notremoved",
                FirstKey = "dlss.first", Log = "add_dlss",
                WatchPath = Path.Combine(P.Gt5, "deps", "dlss", "DLSS", ".git"), CleanOnStop = Path.Combine(P.Gt5, "deps", "dlss"),
            });
            // AMD FSR 3 (frame generation): add_fsr.ps1 fetches AMD's FidelityFX SDK at a fixed release, checks its
            // two DLLs and builds the small plug-in. No licence to accept.
            list.Add(new Addon
            {
                Id = "fsr", ForKey = "LD_FG_METHOD", NameKey = "fsr.name", NoteKey = "fsr.note", NoteAddedKey = "fsr.note.added", Link = null,
                Script = Path.Combine(scripts, "add_fsr.ps1"), AddArgs = new[] { "-NoPause" }, RemoveArgs = new[] { "-Remove", "-NoPause" },
                Files = new[] { Path.Combine(build, "gt5_upscale_fsr.dll"), Path.Combine(build, "amd_fidelityfx_loader_dx12.dll"), Path.Combine(build, "amd_fidelityfx_framegeneration_dx12.dll") },
                AddTitleKey = "fsr.title.add", RemoveTitleKey = "fsr.title.remove", RemoveAskKey = "fsr.ask.remove", RemoveTextKey = "fsr.ask.remove.text",
                AddedKey = "fsr.added", NotAddedKey = "fsr.notadded", RemovedKey = "fsr.removed", NotRemovedKey = "fsr.notremoved",
                FirstKey = "fsr.first", Log = "add_fsr",
                WatchPath = Path.Combine(P.Gt5, "deps", "fsr", "FidelityFX-SDK", ".git"), CleanOnStop = Path.Combine(P.Gt5, "deps", "fsr"),
            });
            return list;
        }

        // NVIDIA's licence for the DLSS release add_dlss.ps1 fetches. The tag is read from the script's own pin
        // ($Pin = @{ ... Tag = 'v310.9.16' ... }), so the link always names the release the script fetches; this
        // constant, the same tag, is only for a script that can't be read.
        const string DlssTag = "v310.9.16";
        static string DlssLicence(string script)
        {
            string tag = DlssTag;
            try
            {
                var m = Regex.Match(File.ReadAllText(script), @"\$Pin\s*=\s*@\{[^}]*?\bTag\s*=\s*'([^']+)'");
                if (m.Success) tag = m.Groups[1].Value;
            }
            catch { }
            return "https://github.com/NVIDIA/DLSS/blob/" + tag + "/LICENSE.txt";
        }

        // One add-on row: the name, Add (or a tick, "Added" and Remove), and the small line under it with its link.
        class AddonRow
        {
            public Addon A;
            public Label Name, Tick, Added;
            public FlatButton Add, Remove;
            public LinkLabel Note;
        }

        int AddAddonRow(Panel p, Addon a, int y)
        {
            int c0 = p.Controls.Count, y0 = y;
            y = AddAddonRowControls(p, a, y);
            if (a.ForKey != null) RowControls(a.ForKey, p, c0, y0, y);
            return y;
        }
        int AddAddonRowControls(Panel p, Addon a, int y)
        {
            var r = new AddonRow { A = a };
            r.Name = new Label { Text = T.Get(a.NameKey), AutoSize = true, Location = new Point(0, y + 6), ForeColor = Look.Text };
            p.Controls.Add(r.Name);
            r.Add = new FlatButton(T.Get("addon.add"), Look.Blue, Color.FromArgb(90, 160, 240)) { Location = new Point(250, y), Size = new Size(120, 30), Font = Look.F(10f, FontStyle.Bold) };
            r.Add.Click += delegate { AddonAdd(a); };
            r.Tick = new Label { Text = "✓", AutoSize = true, Location = new Point(250, y + 4), ForeColor = Look.Blue, Font = Look.F(11f, FontStyle.Bold) };
            r.Added = new Label { Text = T.Get("addon.added"), AutoSize = true, Location = new Point(250 + 20, y + 6), ForeColor = Look.Text, Font = Look.F(10f, FontStyle.Regular) };
            r.Remove = new FlatButton(T.Get("addon.remove"), Look.Panel, Look.NavHot) { Size = new Size(110, 28), Font = Look.F(9.5f, FontStyle.Regular) };
            r.Remove.Location = new Point(r.Added.Left + r.Added.PreferredWidth + 18, y + 1);
            r.Remove.Click += delegate { AddonRemove(a); };
            // Sized by hand: a LinkLabel's own AutoSize cut the Chinese line short of its last characters.
            r.Note = new LinkLabel
            {
                AutoSize = false, Size = new Size(450, 20), Location = new Point(250, y + 38), ForeColor = Look.Dim, Font = Look.F(8.75f, FontStyle.Regular),
                LinkColor = Look.Blue, ActiveLinkColor = Color.FromArgb(120, 190, 255), VisitedLinkColor = Look.Blue, LinkBehavior = LinkBehavior.HoverUnderline
            };
            r.Note.LinkClicked += delegate { OpenLink(a.Link); };
            p.Controls.Add(r.Add); p.Controls.Add(r.Tick); p.Controls.Add(r.Added); p.Controls.Add(r.Remove); p.Controls.Add(r.Note);
            addonRows[a.Id] = r;
            ShowAddonRow(r);
            // The same height before and after adding, so nothing below moves.
            int h = Math.Max(NoteHeight(a.NoteKey, r.Note.Font), NoteHeight(a.NoteAddedKey, r.Note.Font));
            return y + 38 + h + 20;
        }

        static string NoLink(string s) { return s.Replace("[[", "").Replace("]]", ""); }
        static int NoteHeight(string key, Font f)
        {
            return TextRenderer.MeasureText(NoLink(T.Get(key)), f, new Size(450, 2000), TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl).Height;
        }

        void SetNote(AddonRow r, string key)
        {
            string s = T.Get(key);
            int a0 = s.IndexOf("[["), a1 = s.IndexOf("]]");
            if (a0 >= 0 && a1 > a0 && r.A.Link != null)
            {
                r.Note.Text = NoLink(s);
                r.Note.LinkArea = new LinkArea(a0, a1 - a0 - 2);
            }
            else { r.Note.Text = NoLink(s); r.Note.LinkArea = new LinkArea(0, 0); }
            r.Note.Height = NoteHeight(key, r.Note.Font) + 16;   // a line to spare: its wrapping can differ from the measure
        }

        void OpenLink(string url)
        {
            if (string.IsNullOrEmpty(url)) return;
            try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); } catch (Exception e) { status.Text = e.Message; }
        }

        // The row as the files are now: Add; Adding...; a tick, Added and Remove; Removing...
        void ShowAddonRow(AddonRow r)
        {
            // Inside a row that is hidden now (Generator while frame generation is off): nothing of it shows.
            Row row;
            if (r.A.ForKey != null && rows.TryGetValue(r.A.ForKey, out row) && RowIdle(row.Key))
            {
                r.Add.Visible = r.Tick.Visible = r.Added.Visible = r.Remove.Visible = false;
                return;
            }
            bool have = r.A.Present(), mine = busyAddon == r.A.Id && strip.Busy, any = strip.Busy;
            bool adding = mine && !busyRemove, removing = mine && busyRemove;
            bool showAdd = adding || (!have && !removing);
            // Placed from the name on the left, which is shown with its row: a hidden control isn't moved when the
            // page scrolls, and Remove came back far below its row.
            int top = r.Name.Top - 6;
            r.Add.Location = new Point(250, top);
            r.Tick.Location = new Point(250, top + 4);
            r.Added.Location = new Point(270, top + 6);
            r.Remove.Location = new Point(270 + r.Added.PreferredWidth + 18, top + 1);
            r.Note.Location = new Point(250, top + 38);
            bool off = RowOff(r.A.ForKey);   // the renderer chosen does nothing with it yet
            r.Add.Visible = showAdd;
            r.Add.Text = T.Get(adding ? "addon.adding" : "addon.add");
            r.Add.Enabled = !any && !off;
            r.Add.BackColor = off ? Look.NavHot : Look.Blue;   // greyed with its row: not the blue of a button to press
            r.Tick.Visible = r.Added.Visible = r.Remove.Visible = !showAdd;
            r.Remove.Text = T.Get(removing ? "addon.removing" : "addon.remove");
            r.Remove.Enabled = !any && !off;
            SetNote(r, have && !adding ? r.A.NoteAddedKey : r.A.NoteKey);
        }

        // Every time the Display page is shown and after every helper: the rows from the files as they are, and a
        // choice whose add-on is gone (a DLSS mode after Remove) back to the list's first choice.
        void RefreshAddons()
        {
            // Vulkan saved as the renderer but not in gt5.exe (an older launcher's choice, or a build that left it
            // out): DirectX 12, which is what play.bat starts then anyway.
            if (S.Get("GT5_RENDERER") == "vulkan" && !VulkanBuilt()) { S.Set("GT5_RENDERER", ""); SaveQuiet(); }
            foreach (var cb in new List<ComboBox>(combos.Values))
            {
                var ch = cb.SelectedItem as Choice;
                if (ch != null && ch.Off && cb.Items.Count > 0) cb.SelectedIndex = 0;
                cb.Invalidate();
            }
            // The rows again: which add-on is there decides what Auto generates (and so the Multiplier row).
            RefreshControls();
        }

        // A greyed choice was picked: the status line says what to do, and the add-on's button is pointed at.
        void PointAt(Addon a)
        {
            status.Text = T.Get(a.FirstKey);
            AddonRow r;
            if (!addonRows.TryGetValue(a.Id, out r)) return;
            var b = r.Add.Visible ? r.Add : r.Remove;
            BeginInvoke((MethodInvoker)delegate
            {
                if (b.IsDisposed) return;
                Panel pg;
                if (pages.TryGetValue("nav.display", out pg)) pg.ScrollControlIntoView(b);
                if (Form.ActiveForm == this) b.Focus();   // never takes the focus from another program
                b.FlatAppearance.BorderSize = 2; b.FlatAppearance.BorderColor = Color.White;
                var t = new System.Windows.Forms.Timer { Interval = 1500 };
                t.Tick += delegate { t.Stop(); t.Dispose(); if (!b.IsDisposed) b.FlatAppearance.BorderSize = 0; };
                t.Start();
            });
        }

        // Add: the click is the go-ahead (and for DLSS the acceptance of NVIDIA's licence, as the row's line says).
        void AddonAdd(Addon a)
        {
            if (strip.Busy) { status.Text = T.Get("busy"); return; }
            if (running || GameRunning()) { strip.Note(a.NameKey, "fail.game"); return; }
            if (!File.Exists(P.Exe)) { strip.Note(a.NameKey, "fail.notbuilt"); return; }
            if (!File.Exists(a.Script)) { strip.Note(a.NameKey, "fail.update"); return; }
            var j = new HelperJob { TitleKey = a.AddTitleKey, Script = a.Script, DoneFiles = a.Files, Log = a.Log, DoneKey = a.AddedKey,
                                    WatchPath = a.WatchPath, CleanOnStop = a.CleanOnStop };
            j.Args.AddRange(a.AddArgs);
            j.Ended = delegate (bool ok) { busyAddon = null; RefreshAddons(); status.Text = T.Get(ok ? a.AddedKey : a.NotAddedKey); };
            busyAddon = a.Id; busyRemove = false;
            StartJob(j);
        }

        // Remove: one question, then its script with RemoveArgs; it worked when its files are gone.
        void AddonRemove(Addon a)
        {
            if (strip.Busy) { status.Text = T.Get("busy"); return; }
            strip.Ask(a.RemoveAskKey, a.RemoveTextKey, "addon.remove", "ask.keep", delegate
            {
                if (running || GameRunning()) { strip.Note(a.NameKey, "fail.game"); return; }
                if (!File.Exists(a.Script)) { strip.Note(a.NameKey, "fail.update"); return; }
                var j = new HelperJob { TitleKey = a.RemoveTitleKey, StartKey = "st.remove", Script = a.Script, DoneFiles = a.Files, DoneWhenGone = true,
                                        Log = a.Log + "-remove", DoneKey = a.RemovedKey };
                j.Args.AddRange(a.RemoveArgs);
                j.Ended = delegate (bool ok) { busyAddon = null; RefreshAddons(); status.Text = T.Get(ok ? a.RemovedKey : a.NotRemovedKey); };
                busyAddon = a.Id; busyRemove = true;
                StartJob(j);
            }, null);
        }

        string PkgScript() { return Path.Combine(P.Gt5, "scripts", "install_pkg.ps1"); }

        // install_pkg.ps1 does the work through RPCS3 (its own window, minimized, answered by the script) and
        // update_exe.ps1; before, it ran in a console window of its own and waited for Enter at the end.
        void InstallPkg(string[] files)
        {
            var j = new HelperJob { TitleKey = "pkg.title", Script = PkgScript(), Log = "install_pkg", DoneKey = "pkg.done", AutoHide = false };
            j.Args.AddRange(files);
            j.Env["GT5_PKG_NOPAUSE"] = "1";
            j.Explain = delegate (string l, bool worked)
            {
                if (worked)
                {
                    if (l.StartsWith("Next: run setup")) return "pkg.next";
                    if (l.StartsWith("DLC: the vouchers")) return "pkg.dlc";
                    return null;
                }
                if (l.Contains("RPCS3 wasn't found")) return "pkg.norpcs3";
                if (l.Contains("None of those is a PS3 package") || l.Contains("No .pkg files")) return "pkg.notpkg";
                if (l.StartsWith("Not installed:")) return "pkg.some";
                return null;
            };
            j.Ended = delegate (bool ok) { status.Text = T.Get(ok ? "pkg.done" : "pkg.notdone"); };
            StartJob(j);
        }

        void RunUpdate()
        {
            if (strip.Busy) { status.Text = T.Get("busy"); return; }
            if (GameRunning() || running) { strip.Note("upd.now", "upd.running"); return; }
            if (!File.Exists(P.Update) || !Directory.Exists(Path.Combine(P.Root, ".git"))) { strip.Note("upd.now", "upd.nogit"); return; }
            // The update window: a copy of this exe, since the launcher closes so the build can replace it. A copy
            // still open from an earlier update can't be written over: then a copy under another name (this used to
            // fall back to update.ps1 in a console window).
            Exception err = null;
            for (int i = 0; i < 6; i++)
            {
                string copy = Path.Combine(Path.GetTempPath(), i == 0 ? "GT5Updater.exe" : "GT5Updater-" + i + ".exe");
                try
                {
                    File.Copy(Application.ExecutablePath, copy, true);
                    var w = new ProcessStartInfo(copy, string.Format("--update-window \"{0}\" \"{1}\"", P.Root, Application.ExecutablePath));
                    w.WorkingDirectory = P.Root; w.UseShellExecute = false;
                    Process.Start(w);
                    Close();
                    return;
                }
                catch (Exception e) { err = e; }
            }
            MessageBox.Show(this, err != null ? err.Message : "GT5Updater.exe", "GT5");
        }

        // -- play --
        public void PlayNow() { Play(); }
        // gt5.exe --prepare-shaders before the game (rsx_shader_prep.inc). DirectX 12 only: the Vulkan renderer has its
        // own made-ahead pipeline list. Only when its stamp says something changed (PrepareWanted); its words go to
        // GT5\logs\prepare.log.
        Process prep;
        bool prepDone;
        // PLAY NOW while it works: stopped (cmd and gt5.exe), which is safe: each shader is saved when it is made, and
        // the next run carries on.
        void StopPrepare()
        {
            var pr = prep;
            prep = null; prepDone = true;
            playButton.Text = T.Get("play");
            if (pr == null) return;
            try
            {
                if (!pr.HasExited)
                {
                    var k = new ProcessStartInfo("taskkill.exe", "/T /F /PID " + pr.Id) { CreateNoWindow = true, UseShellExecute = false };
                    Process.Start(k).WaitForExit(5000);
                    pr.WaitForExit(5000);
                }
            }
            catch { }
        }
        // Only when something changed since the step last ran: its stamp (GT5\scratch\rsx_shader_cache_v2\prepared.txt,
        // second line, written by gt5.exe --prepare-shaders) names gt5.exe, settings.bat and the shipped shader list by
        // size and time. The same now: nothing to start. No file (a cleared cache, or the game found a new graphics
        // driver and removed it): the step runs.
        bool PrepareWanted()
        {
            try
            {
                string stamp = Path.Combine(P.Gt5, "scratch", "rsx_shader_cache_v2", "prepared.txt");
                if (!File.Exists(stamp)) return true;
                string[] lines = File.ReadAllLines(stamp);
                if (lines.Length < 2) return true;
                var exe = new FileInfo(P.Exe);
                var set = new FileInfo(P.Settings);
                var ship = new FileInfo(Path.Combine(P.Gt5, "shaders", "known_d3d12.lst"));
                string now = string.Format(CultureInfo.InvariantCulture, "launcher {0} {1} {2} {3} {4}",
                    exe.Exists ? (uint)(exe.Length & 0xFFFFFFFF) : 0u, exe.Exists ? exe.LastWriteTimeUtc.ToFileTimeUtc() : 0L,
                    set.Exists ? set.LastWriteTimeUtc.ToFileTimeUtc() : 0L,
                    ship.Exists ? (uint)(ship.Length & 0xFFFFFFFF) : 0u, ship.Exists ? ship.LastWriteTimeUtc.ToFileTimeUtc() : 0L);
                return lines[1].Trim() != now;
            }
            catch { return true; }
        }
        bool PrepareFirst()
        {
            if (prepDone || VulkanChosen() || !File.Exists(P.Exe)) return false;
            if (!PrepareWanted()) return false;
            Process me = Process.GetCurrentProcess();
            IntPtr wasAff = IntPtr.Zero;
            long good = GoodProcessors();
            if (good != 0) { try { wasAff = me.ProcessorAffinity; me.ProcessorAffinity = new IntPtr(good); } catch { wasAff = IntPtr.Zero; } }
            try
            {
                Directory.CreateDirectory(P.Logs);
                var psi = new ProcessStartInfo("cmd.exe", "/c \"\"" + P.Exe + "\" --prepare-shaders 2>\"" + Path.Combine(P.Logs, "prepare.log") + "\"\"");
                psi.WorkingDirectory = P.Gt5; psi.UseShellExecute = false; psi.CreateNoWindow = true;
                psi.WindowStyle = ProcessWindowStyle.Hidden;
                psi.RedirectStandardOutput = true; psi.StandardOutputEncoding = Encoding.UTF8;
                var pr = new Process { StartInfo = psi, EnableRaisingEvents = true };
                pr.OutputDataReceived += delegate (object s, DataReceivedEventArgs e)
                {
                    if (e.Data == null) return;
                    Match m = UpdateForm.ShaderLine.Match(e.Data.Trim());
                    if (!m.Success) return;
                    long n = long.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                    long of = long.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture);
                    if (of <= 0) return;   // nothing to make: no words at all
                    try
                    {
                        BeginInvoke((MethodInvoker)delegate
                        {
                            if (prep != pr) return;
                            status.Text = string.Format(T.Get("prep.status"), n, of);
                            playButton.Text = T.Get("prep.now");
                        });
                    }
                    catch { }
                };
                pr.Exited += delegate
                {
                    try
                    {
                        BeginInvoke((MethodInvoker)delegate
                        {
                            if (prep != pr) return;   // stopped by PLAY NOW: the game is already on its way
                            prep = null; prepDone = true;
                            playButton.Text = T.Get("play");
                            status.Text = "";
                            Play();
                        });
                    }
                    catch { }
                };
                pr.Start();
                prep = pr;
                try { pr.PriorityClass = ProcessPriorityClass.BelowNormal; } catch { }
                pr.BeginOutputReadLine();
            }
            catch { prep = null; prepDone = true; return false; }   // it couldn't start: the game starts as before
            finally { if (wasAff != IntPtr.Zero) { try { me.ProcessorAffinity = wasAff; } catch { } } }
            return true;
        }

        void Play()
        {
            if (strip.Busy) { status.Text = T.Get("busy"); return; }
            if (prep != null) StopPrepare();   // PLAY NOW while the shaders are being prepared
            if (running || GameRunning()) { status.Text = T.Get("already"); return; }
            // Play is greyed while the game can't start, and the Play page says why (--play gets here anyway).
            if (!CanStart()) { status.Text = Warning(); ShowPage("nav.play"); return; }
            if (!File.Exists(P.Vol) && P.VolInPieces() && File.Exists(P.FixFiles))
            {
                // GT.VOL in pieces: asked in the strip, joined by fix_game_files.ps1 with no window (the bar follows the
                // joined file's size), then Play goes on by itself.
                long total = 0;
                try { foreach (var f in Directory.GetFiles(Path.GetDirectoryName(P.Vol), Path.GetFileName(P.Vol) + ".666*")) total += new FileInfo(f).Length; } catch { }
                strip.Ask("vol.ask", "vol.text", "vol.join", "ask.notnow", delegate
                {
                    var j = new HelperJob { TitleKey = "vol.title", StartKey = "st.join", Script = P.FixFiles, Log = "fix_game_files", DoneKey = "vol.done",
                                            DoneFiles = new[] { P.Vol }, WatchPath = P.Vol + ".joining", WatchTotal = total };
                    j.Ended = delegate (bool ok)
                    {
                        if (!ok) { status.Text = T.Get("vol.failed"); return; }
                        BeginInvoke((MethodInvoker)delegate { strip.HideIfDone(); Play(); });
                    };
                    StartJob(j);
                }, null);
                return;
            }
            // Every change was written when it was made; this only catches one the file couldn't take then (it was open).
            SaveQuiet();
            // The shaders first (gt5.exe --prepare-shaders): after an update, everything this PC has drawn is made again
            // with the new version before the game starts. It ends at once when nothing changed; while it works, PLAY NOW
            // starts the game straight away (what isn't made yet is made when first drawn, as before).
            if (PrepareFirst()) return;
            Process cmd;
            // Processors the install's check found calculating wrongly (pc-check.ps1, .gt5\pc-check.json) are left out
            // of the game as they were out of the build: the launcher's own processor set, inherited by play.bat and
            // gt5.exe, for the moment it starts them.
            Process me = Process.GetCurrentProcess();
            IntPtr wasAff = IntPtr.Zero;
            long good = GoodProcessors();
            if (good != 0) { try { wasAff = me.ProcessorAffinity; me.ProcessorAffinity = new IntPtr(good); } catch { wasAff = IntPtr.Zero; } }
            try
            {
                var psi = new ProcessStartInfo("cmd.exe", "/c \"\"" + P.PlayBat + "\"\"");
                psi.WorkingDirectory = P.Gt5; psi.UseShellExecute = false; psi.CreateNoWindow = true;
                psi.WindowStyle = ProcessWindowStyle.Hidden;
                cmd = Process.Start(psi);
            }
            catch { status.Text = T.Get("nostart"); return; }
            finally { if (wasAff != IntPtr.Zero) { try { me.ProcessorAffinity = wasAff; } catch { } } }
            running = true;
            playButton.Text = T.Get("running"); playButton.Enabled = false;
            HideReportLine();   // a new run: the line was about the one before
            ProblemReport.Log(P, "play: the game is starting" + (VulkanChosen() ? " (Vulkan)" : ""));
            status.Text = T.Get("started");
            string mode = S.Get("GT5_LAUNCH_WINDOW");
            // The game places its own window (borderless at the desktop's size, a maximized window, or its last place)
            // when it makes it, on DirectX 12. The Vulkan window doesn't yet: there the launcher still sets the mode.
            bool place = VulkanChosen();
            // Ticked = "1"; unticked writes nothing (before, unticking did nothing: the test was for "0").
            bool hide = S.Get("GT5_LAUNCHER_HIDE") == "1";
            DateTime start = DateTime.Now;
            var th = new Thread(delegate ()
            {
                Process game = null;
                for (int i = 0; i < 300 && game == null && !cmd.HasExited; i++)
                {
                    Thread.Sleep(200);
                    foreach (var pr in Process.GetProcessesByName("gt5"))
                    {
                        try { if (string.Equals(pr.MainModule.FileName, P.Exe, StringComparison.OrdinalIgnoreCase)) { game = pr; break; } } catch { }
                    }
                }
                if (game != null)
                {
                    IntPtr hwnd = IntPtr.Zero;
                    for (int i = 0; i < 300 && !game.HasExited; i++)
                    {
                        game.Refresh(); hwnd = game.MainWindowHandle;
                        if (hwnd != IntPtr.Zero) break;
                        Thread.Sleep(200);
                    }
                    if (hwnd != IntPtr.Zero)
                    {
                        if (hide) try { BeginInvoke((MethodInvoker)delegate { WindowState = FormWindowState.Minimized; }); } catch { }
                        if (place) { Thread.Sleep(600); ApplyWindowMode(hwnd, mode); }
                    }
                    // While it runs, its log every 3 s: a freeze report ([HANG]: no picture for 8 s) puts the report line up
                    // at once, the launcher staying where it is (nothing comes over the game); the game drawing again takes
                    // the line away unless the player used it.
                    var hang = new HangWatch(Path.Combine(P.Logs, "play.err"));
                    try
                    {
                        while (!game.WaitForExit(3000))
                        {
                            int change = hang.Poll();
                            if (change != 0) try { BeginInvoke((MethodInvoker)delegate { FreezeChanged(change > 0); }); } catch { }
                        }
                    }
                    catch { }
                }
                // gt5.exe is play.bat's last command, so cmd's exit code is the game's.
                int code = int.MinValue;
                try { cmd.WaitForExit(); code = cmd.ExitCode; } catch { }
                // The game's own ends: 0 (the player quit), 20 (its graphics couldn't start) and 21 (they stopped while it
                // ran): for 20 and 21 the game has already said so in a message of its own, so nothing more is said here.
                bool own = code == 0 || code == 20 || code == 21 || code == int.MinValue;
                // Any other end (a crash its own handler couldn't report, or a virus scanner stopping it) is usually in
                // Windows' own records: that goes where a report will carry it, not on the screen.
                string why = own ? null : WindowsSaysWhy(start);
                try
                {
                    var tail = "\r\n[launcher] the game ended with exit code " + (code == int.MinValue ? "?" : code.ToString(CultureInfo.InvariantCulture)) +
                               (why != null ? "; Windows says: " + why : "") + "\r\n";
                    if (!own) File.AppendAllText(Path.Combine(P.Logs, "play.err"), tail);
                }
                catch { }
                // How the run ended, from its exit code and its log (ProblemReport.Classify): a crash, a freeze or a start
                // that never showed a picture gets the report line.
                RunVerdict verdict = null;
                double secs = (DateTime.Now - start).TotalSeconds;
                try { verdict = ProblemReport.Classify(Path.Combine(P.Logs, "play.err"), code == int.MinValue ? (int?)null : code, secs); } catch { }
                ProblemReport.Log(P, "play: the game ended, exit code " + (code == int.MinValue ? "?" : ProblemReport.CodeText(code)) +
                                     string.Format(CultureInfo.InvariantCulture, " after {0:0} s", secs) +
                                     (verdict == null ? "" : ": " + ProblemReport.KindText(verdict) + (verdict.Picture ? "" : ", no picture shown")));
                try
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        running = false;
                        prepDone = false;   /* the next Play looks at the shaders' stamp again (settings may change) */
                        playButton.Text = T.Get("play");
                        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
                        Activate();
                        status.Text = T.Get(own ? "stopped" : "crashed");
                        if (verdict != null && verdict.Kind != RunEnd.Fine && !ProblemReport.Seen(P, verdict.Key)) ShowReportLine(verdict);
                        // The game's own menu (F1) may have changed settings: every list shows the file as it is now.
                        ReloadSettings();
                        RefreshPlayButton();
                    });
                }
                catch { }
            });
            th.IsBackground = true; th.Start();
        }

        // The processor set the install's check left (pc-check.ps1 writes .gt5\pc-check.json: this processor's name,
        // the count, and the ones that failed), as an affinity mask; 0 = all of them (no record, a record for another
        // processor, nothing failed, more than 64, or too few left).
        long GoodProcessors()
        {
            try
            {
                string f = Path.Combine(P.Root ?? P.Gt5, ".gt5", "pc-check.json");
                if (!File.Exists(f)) return 0;
                var d = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(File.ReadAllText(f));
                object o;
                int n = Environment.ProcessorCount;
                if (n > 64 || !d.TryGetValue("logical", out o) || Convert.ToInt32(o, CultureInfo.InvariantCulture) != n) return 0;
                string cpu = "";
                try
                {
                    using (var k = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"HARDWARE\DESCRIPTION\System\CentralProcessor\0"))
                        if (k != null) cpu = Convert.ToString(k.GetValue("ProcessorNameString")).Trim();
                }
                catch { }
                if (d.TryGetValue("processor", out o) && o != null && cpu != "" && !string.Equals(Convert.ToString(o).Trim(), cpu, StringComparison.OrdinalIgnoreCase)) return 0;
                if (!d.TryGetValue("bad", out o) || o == null) return 0;
                var bad = new List<int>();
                var arr = o as System.Collections.IEnumerable;
                if (arr != null && !(o is string)) foreach (var x in arr) bad.Add(Convert.ToInt32(x, CultureInfo.InvariantCulture));
                else bad.Add(Convert.ToInt32(o, CultureInfo.InvariantCulture));   // ConvertTo-Json writes a list of one as a number
                long mask = 0; int left = 0;
                for (int i = 0; i < n; i++) if (!bad.Contains(i)) { mask |= 1L << i; left++; }
                return bad.Count == 0 || left < 2 ? 0 : mask;
            }
            catch { return 0; }
        }

        // Why Windows ended the game, when it recorded it while the game ran: an Application Error for
        // gt5.exe (the exception and the module it was in) or Microsoft Defender acting on a GT5 file.
        // Null when there's nothing, or the logs can't be read.
        static string WindowsSaysWhy(DateTime since)
        {
            var found = new List<string>();
            long ms = (long)(DateTime.Now - since).TotalMilliseconds + 15000;
            try
            {
                var q = new EventLogQuery("Application", PathType.LogName,
                    "*[System[Provider[@Name='Application Error'] and EventID=1000 and TimeCreated[timediff(@SystemTime) <= " + ms + "]]]");
                using (var r = new EventLogReader(q))
                    for (EventRecord e = r.ReadEvent(); e != null; e = r.ReadEvent())
                        using (e)
                        {
                            var p = e.Properties;
                            if (p.Count < 8 || !string.Equals(Convert.ToString(p[0].Value), "gt5.exe", StringComparison.OrdinalIgnoreCase)) continue;
                            string s = string.Format("gt5.exe crashed with exception 0x{0} in {1} at offset {2}",
                                Convert.ToString(p[6].Value), Convert.ToString(p[3].Value), Convert.ToString(p[7].Value));
                            if (!found.Contains(s)) found.Add(s);
                        }
            }
            catch { }
            try
            {
                var q = new EventLogQuery("Microsoft-Windows-Windows Defender/Operational", PathType.LogName,
                    "*[System[(EventID=1116 or EventID=1117) and TimeCreated[timediff(@SystemTime) <= " + ms + "]]]");
                using (var r = new EventLogReader(q))
                    for (EventRecord e = r.ReadEvent(); e != null; e = r.ReadEvent())
                        using (e)
                        {
                            string d = null;
                            try { d = e.FormatDescription(); } catch { }
                            if (d == null || d.IndexOf("gt5", StringComparison.OrdinalIgnoreCase) < 0) continue;
                            string s = "Microsoft Defender " + (e.Id == 1117 ? "acted on" : "flagged") + " a GT5 file (Windows Security, Protection history)";
                            if (!found.Contains(s)) found.Add(s);
                        }
            }
            catch { }
            return found.Count == 0 ? null : string.Join("; ", found.ToArray());
        }

        // -- the report line: after a crash or a freeze, one sentence at the bottom with "Save a report", "Open the report
        // page" and a small x; nothing red, nothing over the game. A run's line goes for good once one of them is used.
        void BuildReportLine(Panel bottom)
        {
            repSave = ReportButton(T.Get("rep.save")); repPage = ReportButton(T.Get("rep.page")); repHide = ReportButton("×");
            repHide.Width = 30; repHide.AccessibleName = T.Get("rep.hide");
            repSave.Click += delegate { SaveReport(); };
            repPage.Click += delegate { OpenReportPage(); };
            repHide.Click += delegate { DismissReport(); };
            foreach (var b in new[] { repSave, repPage, repHide }) { b.Visible = false; bottom.Controls.Add(b); b.BringToFront(); }
            // Another message in the line (busy, an add-on's result, the next run): the buttons go with the sentence.
            status.TextChanged += delegate { if (repRun != null && status.Text != repText) HideReportLine(); };
            bottom.Resize += delegate { PlaceReport(); };
            if (repRun != null) { var r = repRun; repRun = null; ShowReportLine(r); }   // built again (the language changed)
        }

        static FlatButton ReportButton(string text)
        {
            var b = new FlatButton(text, Look.NavHot, Look.Line) { Font = Look.F(9.5f, FontStyle.Regular), Height = 30 };
            b.Width = TextRenderer.MeasureText(text, b.Font).Width + 26;
            return b;
        }

        void ShowReportLine(RunVerdict v)
        {
            if (status == null || status.IsDisposed || repSave == null) return;
            if (repRun == null) repBefore = status.Text;
            repRun = v;
            repText = T.Get(v.Kind == RunEnd.Freeze ? "rep.freeze" : v.Kind == RunEnd.NoStart ? "rep.nostart" : "crashed");
            status.Text = repText;
            repSaved = false;
            repSave.Visible = repPage.Visible = repHide.Visible = true;
            repSave.Enabled = true;
            PlaceReport();
        }

        void HideReportLine()
        {
            repRun = null;
            foreach (var b in new[] { repSave, repPage, repHide }) if (b != null && !b.IsDisposed) b.Visible = false;
            if (status != null && !status.IsDisposed && playButton != null) status.Width = Math.Max(100, playButton.Left - 48);
        }

        // The buttons right after the sentence, the line's label no wider than its words (nothing under the buttons).
        void PlaceReport()
        {
            if (repRun == null || status == null || status.IsDisposed) return;
            status.Width = TextRenderer.MeasureText(status.Text, status.Font).Width + 8;
            int x = status.Left + status.Width + 6;
            int y = status.Top + (status.Height - repSave.Height) / 2;
            // (Not b.Visible: that says false for every control of a window not shown yet.)
            foreach (var b in repSaved ? new[] { repPage, repHide } : new[] { repSave, repPage, repHide })
            {
                b.Location = new Point(x, y); x += b.Width + 8;
            }
        }

        void SetReportText(string key) { repText = T.Get(key); status.Text = repText; PlaceReport(); }

        // One ZIP on the desktop, shown in Explorer (made on a thread: a long log takes a moment).
        void SaveReport()
        {
            var v = repRun;
            if (v == null) return;
            repSave.Enabled = false;
            var th = new Thread(delegate ()
            {
                string zip = null;
                try { zip = ProblemReport.Save(P, v, null, null); } catch { }
                try
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        if (repRun != v) return;
                        if (zip == null) { repSave.Enabled = true; SetReportText("rep.notsaved"); return; }
                        v.Used = true;
                        ProblemReport.MarkSeen(P, v.Key);
                        ProblemReport.Log(P, "report: saved as " + Path.GetFileName(zip));
                        repSaved = true;
                        repSave.Visible = false;
                        SetReportText("rep.saved");
                        try { Process.Start("explorer.exe", "/select,\"" + zip + "\""); } catch { }
                    });
                }
                catch { }
            });
            th.IsBackground = true; th.Start();
        }

        // GitHub's new-issue form in the browser, with a title, the version and the PC's parts (ProblemReport.IssueUrl):
        // nothing else is in the link and nothing is sent; the player attaches the ZIP there.
        void OpenReportPage()
        {
            var v = repRun;
            if (v == null) return;
            v.Used = true;
            ProblemReport.MarkSeen(P, v.Key);
            ProblemReport.Log(P, "report: the report page was opened");
            try { Process.Start(new ProcessStartInfo(ProblemReport.IssueUrl(P, v, Gpu)) { UseShellExecute = true }); } catch { }
        }

        void DismissReport()
        {
            var v = repRun;
            if (v == null) return;
            ProblemReport.MarkSeen(P, v.Key);
            string before = repBefore;
            HideReportLine();
            status.Text = before ?? "";
        }

        // The game froze (a [HANG] report in its log) or draws again, while it runs.
        void FreezeChanged(bool frozen)
        {
            if (!running) return;
            if (frozen)
            {
                if (repRun != null) return;
                var v = new RunVerdict { Kind = RunEnd.Freeze, Key = ProblemReport.RunKey(Path.Combine(P.Logs, "play.err")) };
                if (!ProblemReport.Seen(P, v.Key)) ShowReportLine(v);
            }
            else if (repRun != null && repRun.Kind == RunEnd.Freeze && !repRun.Used)
            {
                string before = repBefore;
                HideReportLine();
                status.Text = before ?? "";
            }
        }

        // At start: the last run, when it crashed, froze or showed nothing in the last three days and its line wasn't
        // dismissed or used.
        void CheckLastRun()
        {
            var th = new Thread(delegate ()
            {
                try
                {
                    string err = Path.Combine(P.Logs, "play.err");
                    if (!File.Exists(err) || (DateTime.Now - File.GetLastWriteTime(err)).TotalDays > 3 || GameRunning()) return;
                    var v = ProblemReport.Classify(err, null, -1);
                    if (v.Kind == RunEnd.Fine || ProblemReport.Seen(P, v.Key)) return;
                    BeginInvoke((MethodInvoker)delegate { if (!running && repRun == null) ShowReportLine(v); });
                }
                catch { }
            });
            th.IsBackground = true; th.Start();
        }

        void ApplyWindowMode(IntPtr hwnd, string mode)
        {
            try
            {
                if (mode == "max") { ShowWindow(hwnd, 3); return; }   // SW_MAXIMIZE
                if (mode != "borderless") return;
                var scr = Screen.FromHandle(hwnd).Bounds;
                int style = GetWindowLong(hwnd, GWL_STYLE);
                SetWindowLong(hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
                SetWindowPos(hwnd, IntPtr.Zero, scr.X, scr.Y, scr.Width, scr.Height, 0x0020 | 0x0040);   // FRAMECHANGED | SHOWWINDOW
                SetForegroundWindow(hwnd);
            }
            catch { }
        }
    }

    // ---- Problem reports: after a crash or a freeze ----------------------------------------------------
    // How a run ended, from its exit code and GT5\logs\play.err (one file a run: play.bat keeps the one before as
    // play.prev.err): a crash ([CRASH] / [ABORT] lines, or an exit code that isn't one of the game's own after it had
    // shown pictures), a freeze (a [HANG] report the game didn't recover from) or a start that never showed a picture.
    enum RunEnd { Fine, Crash, Freeze, NoStart }

    class RunVerdict
    {
        public RunEnd Kind = RunEnd.Fine;
        public string Key = "";          // which run (ProblemReport.RunKey)
        public bool Picture;             // it showed at least one picture
        public int? Code;                // its exit code, when known
        public double Seconds = -1;      // how long it ran, when known
        public string Windows = "";      // what Windows recorded about its end (the launcher's own line in play.err)
        public string Disc = "";         // the game's title ID (BCES00569, BCUS98114, ...)
        public bool Used;                // the player saved a report or opened the report page
        public readonly List<string> Lines = new List<string>();   // the log lines that say what happened
    }

    // play.err read as it grows while the game runs: +1 when a freeze report starts, -1 when the game draws again within
    // ProblemReport.LongFreeze s (a longer freeze keeps its line: it counts at the end too).
    class HangWatch
    {
        readonly string path;
        long at;
        string part = "";
        bool open;
        public HangWatch(string path) { this.path = path; }
        public int Poll()
        {
            bool was = open, longOne = false;
            try
            {
                using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                {
                    long len = f.Length;
                    if (len < at) { at = 0; part = ""; }                               // a new run started the file again
                    if (len - at > (8L << 20)) { at = len - (8L << 20); part = ""; }   // only the newest 8 MB
                    if (len > at)
                    {
                        f.Seek(at, SeekOrigin.Begin);
                        var b = new byte[len - at];
                        int n = 0, r;
                        while (n < b.Length && (r = f.Read(b, n, b.Length - n)) > 0) n += r;
                        at += n;
                        string text = part + Encoding.UTF8.GetString(b, 0, n);
                        int last = text.LastIndexOf('\n');
                        part = last < 0 ? text : text.Substring(last + 1);
                        if (part.Length > 65536) part = "";
                        if (last >= 0)
                            foreach (var l in text.Substring(0, last).Split('\n'))
                            {
                                if (!l.StartsWith("[HANG]", StringComparison.Ordinal)) continue;
                                if (ProblemReport.HangLine.IsMatch(l)) open = true;
                                else if (open && l.StartsWith("[HANG] flips again", StringComparison.Ordinal))
                                {
                                    open = false;
                                    if (ProblemReport.AgainSeconds(l) >= ProblemReport.LongFreeze) longOne = true;
                                }
                            }
                    }
                }
            }
            catch { }
            if (longOne && !open) return was ? 0 : 1;   // a long one (begun and ended between two looks, maybe): its line stays
            if (open == was) return 0;
            return open ? 1 : -1;
        }
    }

    // The report the player saves (one ZIP of a few MB at most) and the page they open. Nothing is ever sent by the
    // launcher. Everything in the ZIP passes through Scrubber first: no Windows user name, no PC name and no folder
    // outside the game's own.
    static class ProblemReport
    {
        const string Repo = "https://github.com/MormonFTP-hub/gt5-recomp";
        static readonly Encoding Utf8Strict = new UTF8Encoding(false, true);
        // A picture reached the screen: the renderers' first-frame lines, their 5-second counts, or a freeze report
        // (one comes only after pictures).
        static readonly Regex PictureLine = new Regex(@"^\[(live-draw\] (frame|flip) \d|present\] [1-9]|pacing\] |frame-time\] |stutter\] 5 s to t=\d+ s: [1-9]|framegen\] version|menu\] (settings|in-game) menu ready|HANG\] the game has asked|RSX vulkan\] (engine: async shaders|engine: depth read|\[slow-frame\]))", RegexOptions.CultureInvariant);
        internal static readonly Regex HangLine = new Regex(@"^\[HANG\] .*\bno (flip|picture) for\b", RegexOptions.CultureInvariant);
        static readonly Regex AgainLine = new Regex(@"^\[HANG\] flips again .*\bafter (\d+) s\b", RegexOptions.CultureInvariant);
        // A freeze the game came back from still counts when the picture stood still this long (the game's report
        // comes after 8 s).
        internal const int LongFreeze = 10;
        // "[HANG] flips again (flip 1826) after 18 s without one": 18 (0 when the line doesn't say).
        internal static int AgainSeconds(string l)
        {
            var m = AgainLine.Match(l);
            int s;
            return m.Success && int.TryParse(m.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out s) ? s : 0;
        }
        static readonly Regex EndLine = new Regex(@"^\[launcher\] the game ended with exit code (-?\d+)(?:; Windows says: (.*))?", RegexOptions.CultureInvariant);
        static readonly Regex DiscId = new Regex(@"\b(BC[AEJKU]S\d{5}|BL[AEJKU]S\d{5})\b", RegexOptions.CultureInvariant);
        // Lines worth keeping from the middle of a long log: the PC's parts and the settings in use.
        static readonly Regex KeyLine = new Regex(@"^\[(gpu|settings|launcher|exit-chain|mods|spu-native|res-scale|upscale|fsr|shaders|cellVideoOut|threads\] every|win\] |menu\] settings menu|framegen\] (NVIDIA|AMD|version)|cellAudio\] sound|wheel\] (found|no)|RSX vulkan\] (using|device)|present\] vertical)", RegexOptions.CultureInvariant);
        static readonly Regex Secret = new Regex(@"^(\s*set\s+""?)([A-Z0-9_]*(TOKEN|PASSWORD|PASSWD|SECRET|API_?KEY|AUTH)[A-Z0-9_]*)=([^""\r\n]*)", RegexOptions.Multiline | RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

        // Which run: play.err's first 4 KB (its thread ids and times differ every run), and its time when shorter.
        public static string RunKey(string err)
        {
            try
            {
                byte[] b;
                using (var f = new FileStream(err, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete)) b = ReadBytes(f, 4096);
                string k;
                using (var sha = System.Security.Cryptography.SHA1.Create()) k = BitConverter.ToString(sha.ComputeHash(b), 0, 10).Replace("-", "");
                if (b.Length < 4096) k += "-" + File.GetLastWriteTimeUtc(err).Ticks.ToString(CultureInfo.InvariantCulture);
                return k;
            }
            catch { return ""; }
        }

        public static RunVerdict Classify(string err, int? code, double seconds)
        {
            var v = new RunVerdict { Code = code, Seconds = seconds, Key = RunKey(err) };
            var lines = ReadLines(err);
            int crashAt = -1, hangAt = -1, longAt = -1, againAt = -1;
            bool hangOpen = false;
            int? logged = null;
            for (int i = 0; i < lines.Count; i++)
            {
                string l = lines[i];
                if (l.Length == 0 || l[0] != '[') continue;
                if (!v.Picture && PictureLine.IsMatch(l)) v.Picture = true;
                if (crashAt < 0 && (l.StartsWith("[CRASH]", StringComparison.Ordinal) || l.StartsWith("[ABORT]", StringComparison.Ordinal))) crashAt = i;
                if (l.StartsWith("[HANG]", StringComparison.Ordinal))
                {
                    if (HangLine.IsMatch(l)) { if (!hangOpen) hangAt = i; hangOpen = true; }
                    else if (hangOpen && l.StartsWith("[HANG] flips again", StringComparison.Ordinal))
                    {
                        // It drew again: a freeze all the same when the picture stood still for LongFreeze s or more.
                        hangOpen = false;
                        if (AgainSeconds(l) >= LongFreeze && longAt < 0) { longAt = hangAt; againAt = i; }
                    }
                }
                if (l.StartsWith("[launcher]", StringComparison.Ordinal))
                {
                    var m = EndLine.Match(l);
                    int c;
                    if (m.Success && int.TryParse(m.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out c)) { logged = c; v.Windows = m.Groups[2].Value; }
                }
                if (v.Disc == "" && i < 5000) { var d = DiscId.Match(l); if (d.Success) v.Disc = d.Groups[1].Value; }
            }
            if (!v.Code.HasValue) v.Code = logged;
            if (crashAt >= 0)
            {
                v.Kind = RunEnd.Crash;
                for (int i = crashAt; i < lines.Count && v.Lines.Count < 40; i++)
                    if (lines[i].StartsWith("[CRASH]", StringComparison.Ordinal) || lines[i].StartsWith("[ABORT]", StringComparison.Ordinal)) v.Lines.Add(lines[i]);
            }
            else if (hangOpen || longAt >= 0)
            {
                v.Kind = RunEnd.Freeze;
                // The report's first line, where the busiest threads were, and when it drew again.
                int at = hangOpen ? hangAt : longAt;
                for (int i = at; i < lines.Count && i < at + 400 && v.Lines.Count < 24; i++)
                {
                    string l = lines[i];
                    if (i == at || l.StartsWith("[HANG] host thread", StringComparison.Ordinal) || l.StartsWith("[HANG]     ", StringComparison.Ordinal)) v.Lines.Add(l);
                }
                if (!hangOpen) v.Lines.Add(lines[againAt]);
            }
            else if (v.Code.HasValue)
            {
                // The game's own ends: 0 (quit), 20 (its graphics couldn't start: it said so itself), 21 (the window was
                // closed while it ran). Any other code is a crash once it had shown pictures, else a start that failed.
                int c = v.Code.Value;
                if (c == 20) v.Kind = RunEnd.NoStart;
                else if (c == 21) v.Kind = !v.Picture && seconds >= 30 ? RunEnd.NoStart : RunEnd.Fine;   // closed while stuck at start
                else if (c == 0) v.Kind = v.Picture ? RunEnd.Fine : RunEnd.NoStart;
                else v.Kind = v.Picture ? RunEnd.Crash : RunEnd.NoStart;
            }
            if (v.Kind != RunEnd.Fine && v.Lines.Count == 0)
                for (int i = Math.Max(0, lines.Count - 12); i < lines.Count; i++) if (lines[i].Trim() != "") v.Lines.Add(lines[i]);
            for (int i = 0; i < v.Lines.Count; i++) if (v.Lines[i].Length > 400) v.Lines[i] = v.Lines[i].Substring(0, 400) + " [...]";
            return v;
        }

        public static string KindText(RunVerdict v)
        {
            return v.Kind == RunEnd.Crash ? "crash" : v.Kind == RunEnd.Freeze ? "freeze" : v.Kind == RunEnd.NoStart ? "didn't start" : "fine";
        }
        public static string CodeText(int c)
        {
            return c >= 0 && c < 0x10000 ? c.ToString(CultureInfo.InvariantCulture) : "0x" + ((uint)c).ToString("X8", CultureInfo.InvariantCulture);
        }

        // ---- reading logs ----
        static byte[] ReadBytes(Stream s, int n)
        {
            var b = new byte[n];
            int got = 0, r;
            while (got < n && (r = s.Read(b, got, n - got)) > 0) got += r;
            if (got < n) Array.Resize(ref b, got);
            return b;
        }

        // A log's lines: each one UTF-8 when it is, else in Windows' own code page (the game writes paths through
        // both), without NULs. Of a huge log, the first 16 MB and the last 48 MB. Open while the game writes it.
        public static List<string> ReadLines(string path)
        {
            var lines = new List<string>();
            if (!File.Exists(path)) return lines;
            const long H = 16L << 20, Tl = 48L << 20;
            byte[] head, tail = null;
            long len;
            using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            {
                len = f.Length;
                if (len <= H + Tl) head = ReadBytes(f, (int)len);
                else { head = ReadBytes(f, (int)H); f.Seek(len - Tl, SeekOrigin.Begin); tail = ReadBytes(f, (int)Tl); }
            }
            Split(head, lines, false);
            if (tail != null)
            {
                lines.Add("[... " + ((len - H - Tl) >> 20) + " MB of the log left out ...]");
                Split(tail, lines, true);
            }
            return lines;
        }

        static void Split(byte[] b, List<string> into, bool skipFirst)
        {
            int start = 0;
            if (skipFirst) { int nl = Array.IndexOf(b, (byte)'\n'); start = nl < 0 ? b.Length : nl + 1; }
            while (start < b.Length)
            {
                int nl = Array.IndexOf(b, (byte)'\n', start);
                int end = nl < 0 ? b.Length : nl, e = end;
                if (e > start && b[e - 1] == '\r') e--;
                into.Add(Decode(b, start, e - start));
                start = end + 1;
            }
        }

        static string Decode(byte[] b, int at, int n)
        {
            bool high = false, nul = false;
            for (int i = at; i < at + n; i++) { if (b[i] >= 0x80) high = true; else if (b[i] == 0) nul = true; }
            string s;
            if (!high) s = Encoding.ASCII.GetString(b, at, n);
            else
            {
                try { s = Utf8Strict.GetString(b, at, n); }
                catch (DecoderFallbackException) { s = Encoding.Default.GetString(b, at, n); }
            }
            return nul ? s.Replace("\0", "") : s;
        }

        static string ReadText(string path)
        {
            using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            using (var r = new StreamReader(f, Encoding.UTF8, true)) return r.ReadToEnd().Replace("\0", "");
        }

        static string Tail(string path, int count)
        {
            var lines = ReadLines(path);
            var sb = new StringBuilder();
            int from = Math.Max(0, lines.Count - count);
            if (from > 0) sb.Append("[... ").Append(from).Append(" lines left out ...]\r\n");
            for (int i = from; i < lines.Count; i++) sb.Append(lines[i].Length > 2000 ? lines[i].Substring(0, 2000) + " [...]" : lines[i]).Append("\r\n");
            return sb.ToString();
        }

        // The parts of a log that matter, when the whole is too long: the start (version, graphics card, settings),
        // every crash and freeze report whole, the lines naming the PC's parts and settings wherever they are, and the
        // end. What is left out is counted where it was.
        public static string Excerpt(List<string> lines, long fullChars, int head, int tail)
        {
            long total = 0;
            foreach (var l in lines) total += l.Length + 2;
            var keep = new bool[lines.Count];
            if (total <= fullChars) for (int i = 0; i < keep.Length; i++) keep[i] = true;
            else
            {
                for (int i = 0; i < Math.Min(head, lines.Count); i++) keep[i] = true;
                for (int i = Math.Max(0, lines.Count - tail); i < lines.Count; i++) keep[i] = true;
                for (int i = 0; i < lines.Count; i++)
                {
                    string l = lines[i];
                    if (l.Length == 0 || l[0] != '[') continue;
                    if (l.StartsWith("[CRASH] code=", StringComparison.Ordinal) || l.StartsWith("[ABORT]", StringComparison.Ordinal))
                        for (int j = Math.Max(0, i - 30); j < Math.Min(lines.Count, i + 200); j++) keep[j] = true;
                    else if (HangLine.IsMatch(l))
                        for (int j = i; j < lines.Count && j < i + 8000; j++)
                        {
                            keep[j] = true;
                            if (lines[j].StartsWith("[HANG] end of report", StringComparison.Ordinal)) break;
                        }
                    else if (l.StartsWith("[CRASH]", StringComparison.Ordinal) || l.StartsWith("[HANG]", StringComparison.Ordinal) || KeyLine.IsMatch(l)) keep[i] = true;
                }
            }
            var sb = new StringBuilder();
            int gap = 0;
            for (int i = 0; i < lines.Count; i++)
            {
                if (!keep[i]) { gap++; continue; }
                if (gap > 0) { sb.Append("[... ").Append(gap).Append(" lines left out ...]\r\n"); gap = 0; }
                string l = lines[i];
                sb.Append(l.Length > 2000 ? l.Substring(0, 2000) + " [...]" : l).Append("\r\n");
            }
            if (gap > 0) sb.Append("[... ").Append(gap).Append(" lines left out ...]\r\n");
            return sb.ToString();
        }

        // ---- the report ----
        // The folder a report keeps as %INSTALL%: the install folder, or a bare GT5 folder by itself.
        public static string InstallDir(Paths p)
        {
            return p.Root != null && (File.Exists(Path.Combine(p.Root, "update.ps1")) || File.Exists(Path.Combine(p.Root, "setup.ps1"))) ? p.Root : p.Gt5;
        }

        // The report as one ZIP named GT5-report-<date>-<time>.zip in dir (null: the desktop, else GT5\logs\reports);
        // its path. sc null: this PC's names and folders are taken out.
        public static string Save(Paths p, RunVerdict v, string dir, Scrubber sc)
        {
            if (sc == null) sc = Scrubber.ThisPc(InstallDir(p));
            if (string.IsNullOrEmpty(dir))
            {
                try { dir = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory); } catch { dir = ""; }
                if (dir == "" || !Directory.Exists(dir)) dir = Path.Combine(p.Logs, "reports");
            }
            Directory.CreateDirectory(dir);
            string name = "GT5-report-" + DateTime.Now.ToString("yyyy-MM-dd-HHmm", CultureInfo.InvariantCulture);
            string zip = Path.Combine(dir, name + ".zip");
            for (int n = 2; File.Exists(zip); n++) zip = Path.Combine(dir, name + "-" + n + ".zip");
            // The details of the run from its log (a freeze seen while the game ran has only its kind).
            var d = Classify(Path.Combine(p.Logs, "play.err"), v.Code, v.Seconds);
            if (v.Kind != RunEnd.Fine && d.Kind != v.Kind) { d.Kind = v.Kind; }
            string tmp = Path.Combine(Path.GetTempPath(), "GT5-report-" + Guid.NewGuid().ToString("N") + ".zip");
            try
            {
                // A few MB at most: smaller parts of the logs until it fits.
                long[][] budgets = { new long[] { 3000000, 1500, 5000 }, new long[] { 1000000, 800, 2500 }, new long[] { 300000, 300, 1000 } };
                foreach (var b in budgets)
                {
                    MiniZip.Write(tmp, Entries(p, d, sc, b[0], (int)b[1], (int)b[2]));
                    if (new FileInfo(tmp).Length <= (6L << 20)) break;
                }
                File.Copy(tmp, zip, false);
            }
            finally { try { File.Delete(tmp); } catch { } }
            return zip;
        }

        static List<KeyValuePair<string, byte[]>> Entries(Paths p, RunVerdict v, Scrubber sc, long full, int head, int tail)
        {
            var e = new List<KeyValuePair<string, byte[]>>();
            Action<string, string> add = delegate (string n, string text)
            {
                e.Add(new KeyValuePair<string, byte[]>(n, Encoding.UTF8.GetBytes(sc.Apply(text))));
            };
            add("README.txt", Readme);
            add("summary.txt", Summary(p, v));
            foreach (var n in new[] { "play.err", "play.out", "play.prev.err" })
            {
                string f = Path.Combine(p.Logs, n);
                if (!File.Exists(f)) continue;
                bool main = n == "play.err";
                add(n + ".txt", Excerpt(ReadLines(f), main ? full : full / 3, main ? head : head / 2, main ? tail : tail / 3));
            }
            if (File.Exists(p.Settings)) add("settings.txt", Secret.Replace(ReadText(p.Settings), "$1$2=<removed>"));
            if (File.Exists(p.VersionFile)) add("version.txt", ReadText(p.VersionFile));
            foreach (var n in new[] { "launcher.log", "update.log", "prepare.log" })
            {
                string f = Path.Combine(p.Logs, n);
                if (File.Exists(f)) add(n + ".txt", Tail(f, 1500));
            }
            string check = Path.Combine(p.Root ?? p.Gt5, ".gt5", "pc-check.json");
            if (File.Exists(check)) add("pc-check.txt", ReadText(check));
            return e;
        }

        const string Readme =
            "GT5 PC problem report\r\n" +
            "Made by the GT5 launcher from this PC's game logs. Before anything was written, the Windows user name was\r\n" +
            "replaced by <user>, the PC's name by <pc>, the user's folder by %USER%, the game's folder by %INSTALL% and\r\n" +
            "every other folder by <folder>. No save data, game files or shader cache are in it. Open the files to\r\n" +
            "check them before you share it. Nothing was sent anywhere: you attach this file to a report yourself.\r\n" +
            "\r\n" +
            "GT5 PC 问题报告\r\n" +
            "由 GT5 启动器根据这台电脑的游戏日志生成。写入前，Windows 用户名已替换为 <user>，电脑名替换为 <pc>，用户文件夹\r\n" +
            "替换为 %USER%，游戏文件夹替换为 %INSTALL%，其他文件夹替换为 <folder>。不含存档、游戏文件或着色器缓存。分享前\r\n" +
            "可以打开文件检查。没有发送任何内容：请你自己把这个文件附加到报告中。\r\n";

        static string Summary(Paths p, RunVerdict v)
        {
            var sb = new StringBuilder();
            sb.Append("GT5 PC problem report, made ").Append(DateTime.Now.ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture)).Append("\r\n\r\n");
            sb.Append("What happened: ").Append(v.Kind == RunEnd.Crash ? "the game stopped unexpectedly (crash)" : v.Kind == RunEnd.Freeze ? "the game stopped responding (freeze)" :
                      v.Kind == RunEnd.NoStart ? "the game didn't start properly (no picture)" : "saved by hand (the last run's log shows no crash or freeze)").Append("\r\n");
            if (v.Code.HasValue) sb.Append("Exit code: ").Append(CodeText(v.Code.Value)).Append("\r\n");
            if (v.Seconds >= 0) sb.Append(string.Format(CultureInfo.InvariantCulture, "Run time: {0:0} s\r\n", v.Seconds));
            sb.Append("Pictures shown: ").Append(v.Picture ? "yes" : "no").Append("\r\n");
            if (v.Windows != "") sb.Append("Windows says: ").Append(v.Windows).Append("\r\n");
            sb.Append("\r\n");
            string ver = p.InstalledVersion();
            sb.Append("Port version: ").Append(ver == "" ? "?" : ver).Append("\r\n");
            if (v.Disc != "") sb.Append("Game: ").Append(v.Disc).Append("\r\n");
            sb.Append("Windows: ").Append(WindowsName(true)).Append("\r\n");
            sb.Append("Processor: ").Append(CpuName()).Append(", ").Append(Environment.ProcessorCount).Append(" logical processors\r\n");
            sb.Append("Memory: ").Append(MemoryText()).Append("\r\n");
            foreach (var g in Graphics()) sb.Append("Graphics: ").Append(g).Append("\r\n");
            if (v.Lines.Count > 0)
            {
                sb.Append("\r\nFrom the log:\r\n");
                foreach (var l in v.Lines) sb.Append("  ").Append(l).Append("\r\n");
            }
            return sb.ToString();
        }

        // ---- the PC's parts (nothing that names the PC or its user) ----
        public static string WindowsName(bool build)
        {
            try
            {
                using (var k = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion"))
                {
                    string name = Convert.ToString(k.GetValue("ProductName")), ver = Convert.ToString(k.GetValue("DisplayVersion")), cb = Convert.ToString(k.GetValue("CurrentBuild"));
                    object ubr = k.GetValue("UBR");
                    int b;
                    if (int.TryParse(cb, out b) && b >= 22000) name = name.Replace("Windows 10", "Windows 11");   // Windows 11 still says 10 there
                    string s = (name + " " + ver).Trim();
                    if (build) s += " (build " + cb + (ubr != null ? "." + Convert.ToString(ubr, CultureInfo.InvariantCulture) : "") + ")";
                    return s;
                }
            }
            catch { return Environment.OSVersion.VersionString; }
        }

        public static string CpuName()
        {
            try
            {
                using (var k = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"HARDWARE\DESCRIPTION\System\CentralProcessor\0"))
                    if (k != null) return Regex.Replace(Convert.ToString(k.GetValue("ProcessorNameString")), @"\s+", " ").Trim();
            }
            catch { }
            return "?";
        }

        [StructLayout(LayoutKind.Sequential)]
        class MemStatus { public uint Length = 64, Load; public ulong Total, Avail, TotalPage, AvailPage, TotalVirtual, AvailVirtual, AvailExtended; }
        [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] static extern bool GlobalMemoryStatusEx([In, Out] MemStatus m);
        static string MemoryText()
        {
            try
            {
                var m = new MemStatus();
                if (GlobalMemoryStatusEx(m)) return string.Format(CultureInfo.InvariantCulture, "{0:0.0} GB, {1:0.0} GB free now", m.Total / 1073741824.0, m.Avail / 1073741824.0);
            }
            catch { }
            return "?";
        }

        // Each graphics card with its driver version (Windows' list of display drivers), else the names DXGI gives.
        static List<string> Graphics()
        {
            var r = new List<string>();
            try
            {
                using (var cls = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"))
                    if (cls != null)
                        foreach (var sub in cls.GetSubKeyNames())
                        {
                            if (!Regex.IsMatch(sub, @"^\d{4}$")) continue;
                            try
                            {
                                using (var k = cls.OpenSubKey(sub))
                                {
                                    string desc = k == null ? "" : Convert.ToString(k.GetValue("DriverDesc")), ver = k == null ? "" : Convert.ToString(k.GetValue("DriverVersion"));
                                    if (desc == "") continue;
                                    string s = desc + (ver != "" ? " (driver " + ver + ")" : "");
                                    if (!r.Contains(s)) r.Add(s);
                                }
                            }
                            catch { }
                        }
            }
            catch { }
            if (r.Count == 0) try { r.AddRange(GpuInfo.Read().Names); } catch { }
            return r;
        }

        static string Clip(string s, int n) { s = (s ?? "").Trim(); return s.Length > n ? s.Substring(0, n) : s; }

        // GitHub's new-issue form for a problem, with a title and the version and the PC's parts filled in. Nothing else
        // goes in the link: no log text, no path, no name.
        public static string IssueUrl(Paths p, RunVerdict v, GpuInfo g)
        {
            string title = v.Kind == RunEnd.Freeze ? "The game froze" : v.Kind == RunEnd.NoStart ? "The game didn't start" : "The game stopped unexpectedly";
            string bare = Repo + "/issues/new?template=2-problem.yml&title=" + Uri.EscapeDataString("[Problem] " + title);
            string gpu = "";
            if (g != null) foreach (var n in g.Names) if (n.IndexOf("Microsoft Basic", StringComparison.OrdinalIgnoreCase) < 0) { gpu = n; break; }
            string ver = Clip(p.InstalledVersion(), 60), hw = Clip(CpuName(), 80) + ", " + Clip(gpu, 80) + ", " + Clip(WindowsName(false), 60);
            // Anything the scrubber would change (a name of this PC in a version line, say): the title alone.
            string filled = ver + "\n" + hw;
            if (Scrubber.ThisPc(InstallDir(p)).Apply(filled) != filled) return bare;
            return bare + "&version=" + Uri.EscapeDataString(ver) + "&hardware=" + Uri.EscapeDataString(hw) +
                   "&what=" + Uri.EscapeDataString(title + ".\n\nWhat I was doing:\n");
        }

        // ---- the seen list and the launcher's own log ----
        static string SeenFile(Paths p) { return Path.Combine(p.Root ?? p.Gt5, ".gt5", "report-seen.txt"); }
        public static bool Seen(Paths p, string key)
        {
            if (string.IsNullOrEmpty(key)) return false;
            try { foreach (var l in File.ReadAllLines(SeenFile(p))) if (l.Trim() == key) return true; } catch { }
            return false;
        }
        public static void MarkSeen(Paths p, string key)
        {
            if (string.IsNullOrEmpty(key) || Seen(p, key)) return;
            try
            {
                var all = new List<string>();
                try { all.AddRange(File.ReadAllLines(SeenFile(p))); } catch { }
                all.Add(key);
                if (all.Count > 30) all.RemoveRange(0, all.Count - 30);
                Directory.CreateDirectory(Path.GetDirectoryName(SeenFile(p)));
                File.WriteAllLines(SeenFile(p), all.ToArray());
            }
            catch { }
        }

        // GT5\logs\launcher.log: what the launcher saw of each run (start, end, exit code), a few lines a run; it goes
        // into the report.
        public static void Log(Paths p, string line)
        {
            try
            {
                Directory.CreateDirectory(p.Logs);
                string f = Path.Combine(p.Logs, "launcher.log");
                if (File.Exists(f) && new FileInfo(f).Length > 256 * 1024)
                {
                    var all = File.ReadAllLines(f);
                    var keep = new string[all.Length / 2];
                    Array.Copy(all, all.Length - keep.Length, keep, 0, keep.Length);
                    File.WriteAllLines(f, keep);
                }
                File.AppendAllText(f, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture) + "  " + line + "\r\n");
            }
            catch { }
        }
    }

    // What a report may hold: the game's folder becomes %INSTALL%, the Windows user's folder %USER% (also any other
    // Users\<name> folder, as in a log from another PC), Windows' and the program folders their usual names, any other
    // folder <folder> (the file name stays: a crash names its DLL), and the Windows user name and this PC's name
    // anywhere else <user> and <pc>; e-mail addresses go, and a person's name in a device name ("Anna's AirPods").
    class Scrubber
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern int GetShortPathName(string l, StringBuilder s, int n);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern int GetLongPathName(string s, StringBuilder l, int n);
        const string Seg = @"[^\\/:*?""<>|\r\n\t]+";                // a folder's name (spaces allowed)
        // What follows the last separator, to a quote, a comma, the end of the line or the next path: a file name (and
        // the log's words after it), or a folder's last name, which goes whole, spaces and all.
        const string Last = @"((?:(?![A-Za-z]:[\\/])[^\\/:*?""'<>|\r\n\t,;)])*)";
        const string End = @"(?=[\\/""'<>|:;,)\]\s]|$)";              // where a folder's name ends
        const RegexOptions Opt = RegexOptions.IgnoreCase | RegexOptions.CultureInvariant;
        // User names that are also words of the logs: replaced in paths only.
        static readonly string[] Common = { "user", "users", "admin", "administrator", "owner", "pc", "game", "player", "guest", "default", "public", "test", "home", "windows", "gt5" };
        static readonly Regex FileName = new Regex(@"^[^.]*[^.\s]\.[A-Za-z0-9]{1,8}(?![A-Za-z0-9])");
        static readonly Regex Possessive = new Regex(@"(?<![A-Za-z0-9])\p{Lu}[\p{L}\p{N}_.-]*['’]s(?![A-Za-z0-9])");
        readonly List<KeyValuePair<Regex, MatchEvaluator>> rules = new List<KeyValuePair<Regex, MatchEvaluator>>();

        public static Scrubber ThisPc(string install)
        {
            var pcs = new List<string>();
            string user = "", profile = "";
            try { pcs.Add(Environment.MachineName); } catch { }
            try { pcs.Add(Dns.GetHostName()); } catch { }
            try
            {
                string d = Environment.UserDomainName;
                if (!string.IsNullOrEmpty(d) && !d.Equals("WORKGROUP", StringComparison.OrdinalIgnoreCase) && !d.Equals("AzureAD", StringComparison.OrdinalIgnoreCase)) pcs.Add(d);
            }
            catch { }
            try { user = Environment.UserName; } catch { }
            try { profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile); } catch { }
            return new Scrubber(install, profile, user, pcs.ToArray());
        }

        public Scrubber(string install, string profile, string user, string[] pcs)
        {
            foreach (var f in Forms(install)) Folder(f, "%INSTALL%");
            foreach (var f in Forms(profile)) Folder(f, "%USER%");
            Add(@"(?:(?<![A-Za-z0-9])[A-Za-z]:)?[\\/]+Users[\\/]+" + Seg, "%USER%");   // also /c/Users/<name> (Git's shell)
            Add(@"%USER%[\\/]+AppData[\\/]+Local[\\/]+Temp" + End, "%TEMP%");
            Add(@"%USER%[\\/]+AppData[\\/]+Local" + End, "%LOCALAPPDATA%");
            Add(@"%USER%[\\/]+AppData[\\/]+Roaming" + End, "%APPDATA%");
            // Folders inside the user's own: <folder> (Desktop, Downloads and the like by name), the file name kept.
            Add(@"(%USER%|%TEMP%|%LOCALAPPDATA%|%APPDATA%)((?:[\\/]+" + Seg + @")*)[\\/]+" + Last, InUserFolder);
            Add(@"(?<![A-Za-z0-9])[A-Za-z]:[\\/]+Windows" + End, "%WINDIR%");
            Add(@"(?<![A-Za-z0-9])[A-Za-z]:[\\/]+Program Files \(x86\)" + End, "%ProgramFiles(x86)%");
            Add(@"(?<![A-Za-z0-9])[A-Za-z]:[\\/]+Program Files" + End, "%ProgramFiles%");
            Add(@"(?<![A-Za-z0-9])[A-Za-z]:[\\/]+ProgramData" + End, "%ProgramData%");
            // Any other folder, on a drive or a network share.
            Add(@"(?<![A-Za-z0-9])[A-Za-z]:[\\/]+(?:" + Seg + @"[\\/]+)*" + Last, KeepFile);
            Add(@"(?<![\\/A-Za-z0-9%:])[\\/]{2}(?![?.][\\/])[^\\/:*?""<>|\r\n\t ]+[\\/]+(?:" + Seg + @"[\\/]+)*" + Last, KeepFile);
            var leaf = Path.GetFileName((profile ?? "").TrimEnd('\\', '/'));
            Name(user, "<user>");
            if (!string.Equals(leaf, user, StringComparison.OrdinalIgnoreCase)) Name(leaf, "<user>");
            foreach (var pc in pcs) Name(pc, "<pc>");
            // (Only from the start of a word: tried at every letter of a long run it took seconds on some logs.)
            Add(@"(?<![A-Za-z0-9._%+-])[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}", "<email>");
            // A sound device or a wheel named after its owner.
            Add(@"(sound device|audio|\[wheel\]|bluetooth)[^\r\n]*", delegate (Match m) { return Possessive.Replace(m.Value, "<name>'s"); });
        }

        public string Apply(string text)
        {
            foreach (var r in rules) text = r.Key.Replace(text, r.Value);
            return text;
        }

        void Add(string pattern, string to) { Add(pattern, delegate (Match m) { return to; }); }
        void Add(string pattern, MatchEvaluator ev) { rules.Add(new KeyValuePair<Regex, MatchEvaluator>(new Regex(pattern, Opt), ev)); }

        // A folder's path as a pattern: either slash, doubled ones too (as in JSON), any case.
        void Folder(string path, string to)
        {
            var parts = path.Split(new[] { '\\', '/' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length < 2) return;   // a drive alone is no folder of ours
            var sb = new StringBuilder(@"(?<![A-Za-z0-9])");
            for (int i = 0; i < parts.Length; i++) { if (i > 0) sb.Append(@"[\\/]+"); sb.Append(Regex.Escape(parts[i])); }
            Add(sb.Append(End).ToString(), to);
        }

        // The path as given, its short (8.3) and long forms: longest first.
        static List<string> Forms(string path)
        {
            var r = new List<string>();
            if (string.IsNullOrEmpty(path)) return r;
            string p = path.TrimEnd('\\', '/');
            if (p.Length < 3) return r;
            r.Add(p);
            try { var sb = new StringBuilder(1024); if (GetShortPathName(p, sb, sb.Capacity) > 0) { string s = sb.ToString().TrimEnd('\\'); if (!r.Contains(s)) r.Add(s); } } catch { }
            try { var sb = new StringBuilder(1024); if (GetLongPathName(p, sb, sb.Capacity) > 0) { string s = sb.ToString().TrimEnd('\\'); if (!r.Contains(s)) r.Add(s); } } catch { }
            r.Sort(delegate (string a, string b) { return b.Length - a.Length; });
            return r;
        }

        // A name anywhere, as a whole word (a name in another script: wherever it is).
        void Name(string n, string to)
        {
            n = (n ?? "").Trim();
            if (n.Length < 2 || Array.IndexOf(Common, n.ToLowerInvariant()) >= 0) return;
            bool ascii = true;
            foreach (char c in n) if (c >= 0x80) ascii = false;
            string pat = Regex.Escape(n);
            Add(ascii ? @"(?<![A-Za-z0-9])" + pat + @"(?![A-Za-z0-9])" : pat, to);
        }

        // A path in the user's own folders: the folder token, a well-known folder by name, <folder> for the rest, and
        // the file name.
        static readonly string[] UserFolders = { "desktop", "documents", "downloads", "onedrive", "pictures", "videos", "music", "saved games" };
        static string InUserFolder(Match m)
        {
            string last = m.Groups[3].Value;
            var dirs = m.Groups[2].Value.Split(new[] { '\\', '/' }, StringSplitOptions.RemoveEmptyEntries);
            var sb = new StringBuilder(m.Groups[1].Value);
            int i = 0;
            if (dirs.Length > 0 && Array.IndexOf(UserFolders, dirs[0].Trim().ToLowerInvariant()) >= 0) { sb.Append('\\').Append(dirs[0].Trim()); i = 1; }
            bool file = FileName.IsMatch(last);
            if (i < dirs.Length || (!file && last != "")) sb.Append("\\<folder>");
            if (file) sb.Append('\\').Append(last);
            return sb.ToString();
        }

        // <folder>, with the file name when the path ends in one.
        static string KeepFile(Match m)
        {
            string last = m.Groups[1].Value;
            if (FileName.IsMatch(last)) return "<folder>\\" + last;
            return last.Length == 0 ? "<folder>\\" : "<folder>";
        }
    }

    // A ZIP file (deflate) made with what .NET Framework's System.dll has: the launcher is built with no other library.
    static class MiniZip
    {
        static uint[] table;
        static uint Crc(byte[] d)
        {
            if (table == null)
            {
                var t = new uint[256];
                for (uint n = 0; n < 256; n++) { uint c = n; for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[n] = c; }
                table = t;
            }
            uint crc = 0xFFFFFFFFu;
            foreach (byte b in d) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
            return crc ^ 0xFFFFFFFFu;
        }

        public static void Write(string path, List<KeyValuePair<string, byte[]>> files)
        {
            DateTime now = DateTime.Now;
            ushort time = (ushort)((now.Hour << 11) | (now.Minute << 5) | (now.Second / 2));
            ushort date = (ushort)(((now.Year - 1980) << 9) | (now.Month << 5) | now.Day);
            using (var fs = new FileStream(path, FileMode.Create, FileAccess.Write))
            using (var w = new BinaryWriter(fs))
            using (var cd = new MemoryStream())
            using (var c = new BinaryWriter(cd))
            {
                foreach (var f in files)
                {
                    byte[] name = Encoding.UTF8.GetBytes(f.Key), data = f.Value, packed;
                    using (var ms = new MemoryStream())
                    {
                        using (var ds = new System.IO.Compression.DeflateStream(ms, System.IO.Compression.CompressionMode.Compress, true)) ds.Write(data, 0, data.Length);
                        packed = ms.ToArray();
                    }
                    ushort method = 8;
                    if (packed.Length >= data.Length) { packed = data; method = 0; }
                    uint crc = Crc(data), at = (uint)fs.Position;
                    // local header, flag 0x800: the name is UTF-8
                    w.Write(0x04034b50u); w.Write((ushort)20); w.Write((ushort)0x800); w.Write(method); w.Write(time); w.Write(date);
                    w.Write(crc); w.Write((uint)packed.Length); w.Write((uint)data.Length); w.Write((ushort)name.Length); w.Write((ushort)0);
                    w.Write(name); w.Write(packed);
                    w.Flush();
                    c.Write(0x02014b50u); c.Write((ushort)20); c.Write((ushort)20); c.Write((ushort)0x800); c.Write(method); c.Write(time); c.Write(date);
                    c.Write(crc); c.Write((uint)packed.Length); c.Write((uint)data.Length); c.Write((ushort)name.Length); c.Write((ushort)0); c.Write((ushort)0);
                    c.Write((ushort)0); c.Write((ushort)0); c.Write(0u); c.Write(at); c.Write(name);
                }
                c.Flush();
                uint start = (uint)fs.Position;
                byte[] dir = cd.ToArray();
                w.Write(dir);
                w.Write(0x06054b50u); w.Write((ushort)0); w.Write((ushort)0); w.Write((ushort)files.Count); w.Write((ushort)files.Count);
                w.Write((uint)dir.Length); w.Write(start); w.Write((ushort)0);
            }
        }
    }
}
