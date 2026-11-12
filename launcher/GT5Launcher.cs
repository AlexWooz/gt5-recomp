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
        [DllImport("user32.dll")] static extern bool SetProcessDPIAware();

        [STAThread]
        static void Main(string[] args)
        {
            try { SetProcessDPIAware(); } catch { }
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
            // --play: press Play straight away (a "play directly" shortcut).
            var form = new LauncherForm(false);
            if (Array.IndexOf(args, "--play") >= 0) form.Shown += delegate { form.PlayNow(); };
            Application.Run(form);
        }
    }

    // ---- Where things are ------------------------------------------------------------------------
    class Paths
    {
        public string Root, Gt5, PlayBat, Settings, Exe, Elf, Logs, Mods, VersionFile, Icon0, Update, Readme, Vol, FixFiles;
        public Paths()
        {
            string here = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
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
            { "ready", new[] { "Ready. Settings are saved when you press Play.", "就绪。按“开始游戏”时会保存设置。" } },
            { "notbuilt", new[] { "The game isn't built yet (GT5\\build\\gt5.exe is missing). Run GT5-Installer.bat or setup.ps1 first.", "游戏尚未构建（缺少 GT5\\build\\gt5.exe）。请先运行 GT5-Installer.bat 或 setup.ps1。" } },
            { "noelf", new[] { "GT5\\input\\EBOOT.elf is missing: setup isn't finished (see README, Install).", "缺少 GT5\\input\\EBOOT.elf：安装尚未完成（见 README 的安装部分）。" } },
            { "novol", new[] { "GT5\\vfs\\PS3_GAME\\USRDIR\\GT.VOL is missing. It's the game's data (about 12 GB): without it the game starts to a black screen. Copy it from PS3_GAME\\USRDIR in your disc dump into that folder.", "缺少 GT5\\vfs\\PS3_GAME\\USRDIR\\GT.VOL。这是游戏数据（约 12 GB），没有它游戏会黑屏。请从光盘 dump 的 PS3_GAME\\USRDIR 中把它复制到该文件夹。" } },
            { "volparts", new[] { "GT.VOL is in pieces (GT.VOL.66600, .66601, ...), as dumps made to a FAT32 drive are. The game needs it in one file. Join the pieces now? It takes a few minutes and about 12 GB of free space.", "GT.VOL 被分成了多个部分（GT.VOL.66600、.66601 ……），这是保存到 FAT32 硬盘的 dump 的特点。游戏需要完整的一个文件。现在合并吗？需要几分钟和约 12 GB 可用空间。" } },
            { "volparts.page", new[] { "GT.VOL is in pieces (GT.VOL.66600, .66601, ...), as dumps made to a FAT32 drive are. Press Play to join them into one file (a few minutes, about 12 GB of free space).", "GT.VOL 被分成了多个部分（GT.VOL.66600、.66601 ……），这是保存到 FAT32 硬盘的 dump 的特点。按“开始游戏”即可合并为一个文件（需要几分钟和约 12 GB 可用空间）。" } },
            { "already", new[] { "The game is already running.", "游戏已经在运行。" } },
            { "started", new[] { "Starting the game...", "正在启动游戏..." } },
            { "stopped", new[] { "The game has closed.", "游戏已关闭。" } },
            { "quickexit", new[] { "The game closed right after starting. Its log is in GT5\\logs\\play.err (Help: Save logs for a report).", "游戏启动后立即关闭。日志在 GT5\\logs\\play.err（帮助：保存日志用于报告）。" } },
            { "version", new[] { "Version", "版本" } },
            { "devbuild", new[] { "development build", "开发版本" } },
            // Display
            { "res", new[] { "Resolution", "分辨率" } },
            { "res.help", new[] { "The TV mode the game is told it's on, like on a PS3. 1080p: races drawn at 1728x972 with a sharp 1920x1080 HUD. 720p: races at 1152x648, for slow graphics chips.", "游戏所认为的电视模式（同 PS3）。1080p：比赛以 1728x972 绘制，界面为 1920x1080。720p：比赛以 1152x648 绘制，适合较慢的显卡。" } },
            { "res.1080", new[] { "1080p (sharper)", "1080p（更清晰）" } },
            { "res.720", new[] { "720p (faster on slow graphics)", "720p（较慢显卡更流畅）" } },
            { "shape", new[] { "Screen shape", "画面比例" } },
            { "shape.help", new[] { "Auto uses your monitor's shape: on an ultrawide the game draws a wider view and keeps the HUD 16:9 in the middle.", "自动：使用显示器的比例。超宽屏上游戏会显示更宽的视野，界面保持 16:9 居中。" } },
            { "shape.auto", new[] { "Auto (your monitor)", "自动（跟随显示器）" } },
            { "window", new[] { "Window", "窗口" } },
            { "window.help", new[] { "Borderless full screen covers the whole monitor (Alt+Enter is switched off in the game).", "无边框全屏会铺满整个显示器（游戏中已禁用 Alt+Enter）。" } },
            { "window.windowed", new[] { "Window", "窗口模式" } },
            { "window.max", new[] { "Maximized window", "最大化窗口" } },
            { "window.borderless", new[] { "Borderless full screen", "无边框全屏" } },
            { "size", new[] { "Window size", "窗口大小" } },
            { "size.help", new[] { "The size the window opens at (window mode only).", "窗口打开时的大小（仅窗口模式）。" } },
            { "size.default", new[] { "Default", "默认" } },
            { "speed", new[] { "Speed", "速度" } },
            { "speed.help", new[] { "For slower PCs. GT5 counts time in frames, so when the PC can't keep up the whole game slows down. 30 fps draws every other frame and leaves that CPU time to the game: about 10% faster on a 6-core test, more where drawing is what holds it back. Picked for your PC the first time the launcher starts.", "适合较慢的电脑。GT5 以帧计时，电脑跟不上时整个游戏都会变慢。30 fps 隔帧绘制，把省下的 CPU 时间留给游戏：在 6 核测试中快约 10%，在绘制是瓶颈的电脑上更多。启动器第一次运行时会按你的电脑自动选择。" } },
            { "speed.full", new[] { "Draw every frame (fast PCs)", "每帧都绘制（高性能电脑）" } },
            { "speed.30", new[] { "30 fps: draw every other frame (slower PCs, laptops)", "30 fps：隔帧绘制（较慢的电脑、笔记本）" } },
            { "watch", new[] { "Watch mode by itself", "自动观看模式" } },
            { "watch.help", new[] { "Comes on after 10 s without input (B-Spec, replays): draws every other frame and lowers the game's priority. F9 toggles it any time.", "10 秒无操作后自动开启（B-Spec、回放）：隔帧绘制并降低游戏优先级。随时可按 F9 切换。" } },
            { "spus", new[] { "Extra SPU threads", "额外 SPU 线程" } },
            { "spus.help", new[] { "PC threads for the PS3's co-processors. 2 suits most CPUs; 0-1 on 4-core CPUs.", "用于 PS3 协处理器的 PC 线程。多数 CPU 用 2；4 核 CPU 用 0-1。" } },
            // Controls
            { "padbg", new[] { "Controller works when the game isn't in front", "游戏不在前台时手柄仍可用" } },
            { "clip", new[] { "Keep the mouse inside the window", "鼠标限制在窗口内" } },
            { "wheel", new[] { "Steering wheel", "方向盘" } },
            { "wheel.help", new[] { "Plug it in before starting. The game sees it as a Logitech G27 with force feedback (new, not yet tried on a real wheel: please report how it goes).", "请在启动前连接。游戏会将其识别为带力反馈的 Logitech G27（新功能，尚未在真实方向盘上测试，欢迎反馈）。" } },
            { "wheel.on", new[] { "Use one if connected", "已连接时使用" } },
            { "wheel.off", new[] { "Don't look for a wheel", "不使用方向盘" } },
            { "range", new[] { "Wheel rotation", "方向盘转角" } },
            { "range.help", new[] { "Set it to what your wheel is set to.", "设置为方向盘自身的转角设定。" } },
            { "ffb", new[] { "Force feedback strength", "力反馈强度" } },
            { "ffbinv", new[] { "Forces push the wrong way (invert)", "力反馈方向相反（反转）" } },
            { "keys", new[] { "Keyboard: Z = Cross (OK), X = Circle, A = Square, S = Triangle, arrows = D-pad, Enter = Start, Tab = Select. DualSense, DualShock 4 and Xbox pads work too.", "键盘：Z = ✕（确定），X = ○，A = □，S = △，方向键 = 十字键，Enter = Start，Tab = Select。也支持 DualSense、DualShock 4 和 Xbox 手柄。" } },
            // Game
            { "lang", new[] { "Game language", "游戏语言" } },
            { "lang.help", new[] { "The PS3 system language the game is told. It only changes if your disc has that language (Europe: many; North America: English, French, Spanish, Portuguese, Japanese).", "告诉游戏的 PS3 系统语言。只有光盘包含该语言时才会改变（欧版语言较多；美版：英、法、西、葡、日）。" } },
            { "lang.system", new[] { "Same as Windows", "与 Windows 相同" } },
            { "mods", new[] { "Mods", "模组" } },
            { "mods.none", new[] { "No mods installed. Mods go in GT5\\mods (see MODDING.md); after adding one, run update.ps1 -Force so it's built in.", "未安装模组。模组放在 GT5\\mods（见 MODDING.md）；添加后运行 update.ps1 -Force 以编译进游戏。" } },
            { "ui", new[] { "Launcher language", "启动器语言" } },
            { "hide", new[] { "Minimize the launcher while playing", "游戏时最小化启动器" } },
            { "autocheck", new[] { "Check for updates when the launcher opens", "打开启动器时检查更新" } },
            // Updates
            { "upd.installed", new[] { "Installed", "已安装" } },
            { "upd.latest", new[] { "Newest release", "最新版本" } },
            { "upd.check", new[] { "Check now", "立即检查" } },
            { "upd.now", new[] { "Update now", "立即更新" } },
            { "upd.checking", new[] { "Checking GitHub...", "正在检查 GitHub..." } },
            { "upd.uptodate", new[] { "You're up to date.", "已是最新版本。" } },
            { "upd.available", new[] { "Update available: {0}", "有可用更新：{0}" } },
            { "upd.failed", new[] { "Couldn't reach GitHub: {0}", "无法连接 GitHub：{0}" } },
            { "upd.howto", new[] { "Update now opens a window that downloads the new version, rebuilds what changed (a few minutes; sometimes about 10 minutes more) and opens the launcher again. Your saves, settings and game files are kept.", "“立即更新”会打开一个窗口，下载新版本并重新构建改动部分（几分钟，有时多约 10 分钟），然后重新打开启动器。存档、设置和游戏文件都会保留。" } },
            { "upd.notes", new[] { "What's new", "更新内容" } },
            { "upd.running", new[] { "The game is running: close it before updating.", "游戏正在运行：请先关闭游戏再更新。" } },
            { "upd.nogit", new[] { "Updating needs the install folder made by the installer (a git clone with update.ps1).", "更新需要由安装程序创建的安装文件夹（含 update.ps1 的 git 克隆）。" } },
            // Help
            { "help.logs", new[] { "Open the logs folder", "打开日志文件夹" } },
            { "help.save", new[] { "Save logs for a report", "保存日志用于报告" } },
            { "help.saved", new[] { "Saved to your desktop as {0}. Attach it to your report (GitHub accepts .txt).", "已保存到桌面：{0}。请附加到报告中（GitHub 接受 .txt）。" } },
            { "help.report", new[] { "Report a problem", "报告问题" } },
            { "help.readme", new[] { "Read the FAQ", "常见问题" } },
            { "help.changes", new[] { "Changelog", "更新日志" } },
            { "help.versions", new[] { "Version history", "版本历史" } },
            { "help.discord", new[] { "Discord: mormonftp", "Discord：mormonftp" } },
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
        public Choice(string v, string l) { Value = v; Label = l; }
        public override string ToString() { return Label; }
    }

    class Settings
    {
        // Every key the launcher manages, with play.bat's default. "" = not set.
        public static readonly string[][] Defaults = new[]
        {
            new[] { "PS3_VIDEO_MODE", "1080" }, new[] { "PS3_ASPECT", "auto" }, new[] { "PS3_WINDOW_SIZE", "" },
            new[] { "LD_WATCH", "" }, new[] { "SPURS_EXTRA_SPUS", "2" }, new[] { "PAD_BACKGROUND", "1" },
            new[] { "PAD_CURSOR_CLIP", "" }, new[] { "PS3_WHEEL", "" }, new[] { "PS3_WHEEL_RANGE", "" },
            new[] { "PS3_WHEEL_FFB", "" }, new[] { "PS3_WHEEL_FFB_INVERT", "" }, new[] { "PS3_LANG", "" },
            new[] { "GT5_LAUNCH_WINDOW", "windowed" }, new[] { "GT5_LAUNCHER_LANG", "" },
            new[] { "GT5_LAUNCHER_HIDE", "1" }, new[] { "GT5_LAUNCHER_AUTOCHECK", "1" },
            new[] { "GT5_SPEED", "" },
        };
        // True when there was no settings.bat yet: the launcher's first start on this install.
        public bool Fresh;
        public Dictionary<string, string> V = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        readonly string path;
        static readonly Regex SetLine = new Regex("^\\s*set\\s+\"([A-Za-z0-9_]+)=(.*)\"\\s*$", RegexOptions.IgnoreCase);

        public Settings(string path)
        {
            this.path = path;
            Fresh = !File.Exists(path);
            foreach (var d in Defaults) V[d[0]] = d[1];
            try
            {
                foreach (var line in File.ReadAllLines(path))
                {
                    var m = SetLine.Match(line);
                    if (m.Success) V[m.Groups[1].Value] = m.Groups[2].Value;
                }
            }
            catch { }
        }
        public string Get(string k) { string v; return V.TryGetValue(k, out v) ? v : ""; }
        public void Set(string k, string v) { V[k] = v ?? ""; }
        public void Save()
        {
            var sb = new StringBuilder();
            sb.AppendLine("@echo off");
            sb.AppendLine("REM Written by the GT5 Launcher; play.bat runs it after its own defaults. Change these in the");
            sb.AppendLine("REM launcher, or edit them here: one set \"NAME=value\" per line, an empty value means not set.");
            var keys = new List<string>(V.Keys);
            keys.Sort(StringComparer.OrdinalIgnoreCase);
            foreach (var k in keys)
                sb.AppendLine(string.Format("set \"{0}={1}\"", k, V[k].Replace("\"", "")));
            // Speed (GT5_SPEED) turns into the renderer's own switches, after the keys so it wins.
            // GT5 counts time in frames: these draw fewer frames instead of letting the game slow down.
            string speed = Get("GT5_SPEED");
            if (speed == "30") { sb.AppendLine("set \"LD_WATCH=2\""); sb.AppendLine("set \"LD_WATCH_PRIORITY=0\""); }
            File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
        }
    }

    class ModInfo
    {
        public string Name, Description, Switch;
        public bool Default;
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
        public static Font F(float size, FontStyle st) { return new Font("Segoe UI", size, st, GraphicsUnit.Point); }
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

        // First start on this install: a first guess at Speed and resolution from the CPU, which sets the
        // pace in this port (the graphics work runs on one thread). Everything stays changeable.
        void AutoPick()
        {
            int n = Environment.ProcessorCount;
            var bs = SystemInformation.PowerStatus.BatteryChargeStatus;
            bool laptop = (bs & BatteryChargeStatus.NoSystemBattery) == 0 && bs != BatteryChargeStatus.Unknown;
            if (n <= 8 || (laptop && n <= 12)) { S.Set("GT5_SPEED", "30"); S.Set("PS3_VIDEO_MODE", "720"); }
        }

        public LauncherForm(bool screenshots)
        {
            shots = screenshots;
            S = new Settings(P.Settings);
            if (S.Fresh) AutoPick();
            T.Zh = S.Get("GT5_LAUNCHER_LANG") == "zh";
            Text = "Gran Turismo 5 — PC";
            BackColor = Look.Bg; ForeColor = Look.Text;
            FormBorderStyle = FormBorderStyle.FixedSingle; MaximizeBox = false;
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(980, 620);
            StartPosition = FormStartPosition.CenterScreen;
            Font = Look.F(9.5f, FontStyle.Regular);
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }
            try { if (File.Exists(P.Icon0)) logo = Image.FromFile(P.Icon0); } catch { }
            DoubleBuffered = true;
            Build();
            ShowPage("nav.play");
            // After the window exists: the check answers on the UI thread through BeginInvoke.
            Shown += delegate { if (S.Get("GT5_LAUNCHER_AUTOCHECK") != "0" && !shots) CheckForUpdates(false); };
        }

        // -- layout --
        void Build()
        {
            Controls.Clear(); pages.Clear(); navButtons.Clear();
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

            content.Dock = DockStyle.Fill; content.BackColor = Look.Bg; content.Padding = new Padding(28, 18, 28, 10);
            Controls.Add(content); Controls.Add(nav); Controls.Add(bottom); Controls.Add(header);

            pages["nav.play"] = BuildPlayPage();
            pages["nav.display"] = BuildDisplayPage();
            pages["nav.controls"] = BuildControlsPage();
            pages["nav.game"] = BuildGamePage();
            pages["nav.updates"] = BuildUpdatesPage();
            pages["nav.help"] = BuildHelpPage();
            foreach (var p in pages.Values) { p.Dock = DockStyle.Fill; p.Visible = false; p.AutoScroll = true; DarkScroll(p); content.Controls.Add(p); }
            DarkScroll(notes);
            RefreshVersion();
            PlaceHeaderRight(header);
            bottom.Width = ClientSize.Width;
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
            var l = new Label { Text = text, ForeColor = Look.Dim, Font = Look.F(8.75f, FontStyle.Regular), Location = new Point(x, y), MaximumSize = new Size(700 - x, 0), AutoSize = true };
            p.Controls.Add(l);
            return y + l.PreferredHeight + 12;
        }
        int AddCombo(Panel p, string labelKey, string helpKey, string key, Choice[] choices, int y)
        {
            p.Controls.Add(new Label { Text = T.Get(labelKey), AutoSize = true, Location = new Point(0, y + 4), ForeColor = Look.Text });
            var cb = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, FlatStyle = FlatStyle.Flat, BackColor = Look.Panel, ForeColor = Look.Text, Location = new Point(250, y), Width = 300,
                                    DrawMode = DrawMode.OwnerDrawFixed, ItemHeight = 22 };
            cb.DrawItem += DrawDarkItem;
            foreach (var c in choices) cb.Items.Add(c);
            string cur = S.Get(key);
            int sel = 0;
            for (int i = 0; i < choices.Length; i++) if (string.Equals(choices[i].Value, cur, StringComparison.OrdinalIgnoreCase)) sel = i;
            cb.SelectedIndex = sel;
            cb.SelectedIndexChanged += delegate
            {
                S.Set(key, ((Choice)cb.SelectedItem).Value); SaveQuiet();
                if (key == "GT5_LAUNCHER_LANG")   // the launcher's own language: redraw everything in it
                    BeginInvoke((MethodInvoker)delegate { T.Zh = S.Get(key) == "zh"; string pg = page; Build(); ShowPage(pg); if (latestTag != "") CheckForUpdates(false); });
            };
            p.Controls.Add(cb);
            y += 34;
            return helpKey != null ? AddHelp(p, T.Get(helpKey), y - 2, 250) : y + 6;
        }
        int AddCheck(Panel p, string labelKey, string helpKey, string key, string onValue, int y)
        {
            var c = new CheckBox { Text = T.Get(labelKey), AutoSize = true, Location = new Point(0, y), ForeColor = Look.Text, Checked = S.Get(key) == onValue };
            c.CheckedChanged += delegate { S.Set(key, c.Checked ? onValue : ""); SaveQuiet(); };
            p.Controls.Add(c);
            y += 30;
            return helpKey != null ? AddHelp(p, T.Get(helpKey), y - 4, 20) : y + 4;
        }
        void SaveQuiet() { try { S.Save(); } catch { } }

        // Dark dropdowns: Windows draws DropDownList boxes in light colours unless they're drawn by hand.
        static void DrawDarkItem(object sender, DrawItemEventArgs e)
        {
            var cb = (ComboBox)sender;
            bool hot = (e.State & DrawItemState.Selected) != 0 && (e.State & DrawItemState.ComboBoxEdit) == 0;
            using (var b = new SolidBrush(hot ? Look.Accent : Look.Panel)) e.Graphics.FillRectangle(b, e.Bounds);
            if (e.Index >= 0)
                TextRenderer.DrawText(e.Graphics, cb.Items[e.Index].ToString(), cb.Font, new Rectangle(e.Bounds.X + 6, e.Bounds.Y, e.Bounds.Width - 6, e.Bounds.Height),
                                      Look.Text, TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
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
            // Below the settings: what would stop the game (not built, GT.VOL missing or in pieces).
            var warn = new Label { ForeColor = Color.FromArgb(240, 180, 70), AutoSize = true, Location = new Point(0, y + 134), MaximumSize = new Size(700, 0), Font = Look.F(10f, FontStyle.Bold) };
            p.Controls.Add(warn);
            Action refresh = delegate { summary.Text = Summary(); warn.Text = Warning(); };
            p.VisibleChanged += delegate { if (p.Visible) refresh(); };
            refresh();
            y += 134 + 64;
            y = AddHelp(p, T.Get("keys"), y, 0);
            y = AddHelp(p, T.Get("help.about"), y + 6, 0);
            return p;
        }

        string Summary()
        {
            string res = S.Get("PS3_VIDEO_MODE") == "720" ? "720p" : "1080p";
            string shape = S.Get("PS3_ASPECT"); if (shape == "" || shape == "auto") shape = T.Get("shape.auto");
            string win = S.Get("GT5_LAUNCH_WINDOW") == "borderless" ? T.Get("window.borderless") : S.Get("GT5_LAUNCH_WINDOW") == "max" ? T.Get("window.max") : T.Get("window.windowed");
            string wheel = S.Get("PS3_WHEEL") == "off" ? T.Get("off") : T.Get("wheel.on");
            var sb = new StringBuilder();
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("res"), res));
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("shape"), shape));
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("window"), win));
            string sp = S.Get("GT5_SPEED");
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("speed"), T.Get(sp == "30" ? "speed.30" : "speed.full")));
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("wheel"), wheel));
            sb.AppendLine(string.Format("{0}:  {1}", T.Get("lang"), S.Get("PS3_LANG") == "" ? T.Get("lang.system") : S.Get("PS3_LANG")));
            return sb.ToString();
        }

        string Warning()
        {
            if (!File.Exists(P.Exe)) return T.Get("notbuilt");
            if (!File.Exists(P.Vol)) return T.Get(P.VolInPieces() ? "volparts.page" : "novol");
            return "";
        }

        Panel BuildDisplayPage()
        {
            var p = NewPage(); int y = 0;
            y = AddHeading(p, T.Get("nav.display"), y);
            y = AddCombo(p, "res", "res.help", "PS3_VIDEO_MODE", new[] { new Choice("1080", T.Get("res.1080")), new Choice("720", T.Get("res.720")) }, y);
            y = AddCombo(p, "speed", "speed.help", "GT5_SPEED", new[] { new Choice("", T.Get("speed.full")), new Choice("30", T.Get("speed.30")) }, y);
            y = AddCombo(p, "shape", "shape.help", "PS3_ASPECT", new[] { new Choice("auto", T.Get("shape.auto")), new Choice("16:9", "16:9"), new Choice("21:9", "21:9"), new Choice("32:9", "32:9") }, y);
            y = AddCombo(p, "window", "window.help", "GT5_LAUNCH_WINDOW", new[] { new Choice("windowed", T.Get("window.windowed")), new Choice("max", T.Get("window.max")), new Choice("borderless", T.Get("window.borderless")) }, y);
            y = AddCombo(p, "size", "size.help", "PS3_WINDOW_SIZE", new[] { new Choice("", T.Get("size.default")), new Choice("1280x720", "1280 x 720"), new Choice("1600x900", "1600 x 900"), new Choice("1920x1080", "1920 x 1080"), new Choice("2560x1440", "2560 x 1440"), new Choice("2560x1080", "2560 x 1080 (21:9)"), new Choice("3440x1440", "3440 x 1440 (21:9)"), new Choice("3840x2160", "3840 x 2160") }, y);
            y = AddCheck(p, "watch", "watch.help", "LD_WATCH", "1", y);
            y = AddCombo(p, "spus", "spus.help", "SPURS_EXTRA_SPUS", new[] { new Choice("0", "0"), new Choice("1", "1"), new Choice("2", "2"), new Choice("3", "3"), new Choice("4", "4") }, y);
            return p;
        }

        Panel BuildControlsPage()
        {
            var p = NewPage(); int y = 0;
            y = AddHeading(p, T.Get("nav.controls"), y);
            y = AddCheck(p, "padbg", null, "PAD_BACKGROUND", "1", y);
            y = AddCheck(p, "clip", null, "PAD_CURSOR_CLIP", "1", y);
            y = AddCombo(p, "wheel", "wheel.help", "PS3_WHEEL", new[] { new Choice("", T.Get("wheel.on")), new Choice("off", T.Get("wheel.off")) }, y + 8);
            y = AddCombo(p, "range", "range.help", "PS3_WHEEL_RANGE", new[] { new Choice("", T.Get("size.default")), new Choice("270", "270°"), new Choice("540", "540°"), new Choice("720", "720°"), new Choice("900", "900°"), new Choice("1080", "1080°") }, y);
            var ffb = new List<Choice> { new Choice("", T.Get("size.default")) };
            for (int v = 100; v >= 0; v -= 10) ffb.Add(new Choice(v.ToString(), v + "%"));
            y = AddCombo(p, "ffb", null, "PS3_WHEEL_FFB", ffb.ToArray(), y);
            y = AddCheck(p, "ffbinv", null, "PS3_WHEEL_FFB_INVERT", "1", y);
            y = AddHelp(p, T.Get("keys"), y + 10, 0);
            return p;
        }

        Panel BuildGamePage()
        {
            var p = NewPage(); int y = 0;
            y = AddHeading(p, T.Get("nav.game"), y);
            y = AddCombo(p, "lang", "lang.help", "PS3_LANG", new[] {
                new Choice("", T.Get("lang.system")), new Choice("en", "English (US)"), new Choice("en-gb", "English (UK)"),
                new Choice("fr", "Français"), new Choice("es", "Español"), new Choice("de", "Deutsch"), new Choice("it", "Italiano"),
                new Choice("nl", "Nederlands"), new Choice("pt", "Português"), new Choice("pt-br", "Português (Brasil)"),
                new Choice("ru", "Русский"), new Choice("pl", "Polski"), new Choice("tr", "Türkçe"), new Choice("fi", "Suomi"),
                new Choice("sv", "Svenska"), new Choice("da", "Dansk"), new Choice("no", "Norsk"), new Choice("ja", "日本語"),
                new Choice("ko", "한국어"), new Choice("zh-t", "中文（繁體）"), new Choice("zh-s", "中文（简体）") }, y);
            y = AddHeading(p, T.Get("mods"), y + 8);
            var mods = LoadMods();
            if (mods.Count == 0) y = AddHelp(p, T.Get("mods.none"), y, 0);
            foreach (var m in mods)
            {
                string cur = S.Get(m.Switch);
                bool on = cur == "" ? m.Default : cur == "1";
                var c = new CheckBox { Text = m.Name, AutoSize = true, Location = new Point(0, y), ForeColor = Look.Text, Checked = on };
                var mod = m;
                c.CheckedChanged += delegate { S.Set(mod.Switch, c.Checked ? "1" : "0"); SaveQuiet(); };
                p.Controls.Add(c); y += 28;
                if (m.Description != "") y = AddHelp(p, m.Description, y - 4, 20);
            }
            y = AddHeading(p, T.Get("ui"), y + 10);
            y = AddCombo(p, "ui", null, "GT5_LAUNCHER_LANG", new[] { new Choice("", "English"), new Choice("zh", "中文（简体）") }, y);
            var restart = new Label { Text = "", ForeColor = Look.Dim, AutoSize = true, Location = new Point(560, y - 36) };
            p.Controls.Add(restart);
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
                        var m = new ModInfo { Name = name, Description = d.TryGetValue("description", out o) && o != null ? o.ToString() : "" };
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
            y += 50;
            y = AddHelp(p, T.Get("upd.howto"), y, 0);
            p.Controls.Add(new Label { Text = T.Get("upd.notes"), AutoSize = true, Location = new Point(0, y + 4), Font = Look.F(10.5f, FontStyle.Bold), ForeColor = Look.Text });
            notes = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, BackColor = Look.Panel, ForeColor = Look.Text, BorderStyle = BorderStyle.None, Location = new Point(0, y + 32), Size = new Size(700, 170), Font = Look.F(9f, FontStyle.Regular) };
            p.Controls.Add(notes);
            return p;
        }

        Panel BuildHelpPage()
        {
            var p = NewPage(); int y = 0;
            y = AddHeading(p, T.Get("nav.help"), y);
            string[][] links = {
                new[] { "help.save", "save" }, new[] { "help.logs", "logs" }, new[] { "help.report", "https://github.com/MormonFTP-hub/gt5-recomp/issues/new/choose" },
                new[] { "help.readme", "https://github.com/MormonFTP-hub/gt5-recomp#faq" }, new[] { "help.versions", "https://github.com/MormonFTP-hub/gt5-recomp/blob/main/VERSIONS.md" },
                new[] { "help.changes", "https://github.com/MormonFTP-hub/gt5-recomp/blob/main/CHANGELOG.md" },
            };
            foreach (var l in links)
            {
                var b = new FlatButton(T.Get(l[0]), Look.Panel, Look.NavHot) { Location = new Point(0, y), Size = new Size(300, 40), TextAlign = ContentAlignment.MiddleLeft, Padding = new Padding(10, 0, 0, 0) };
                string target = l[1];
                b.Click += delegate { HelpAction(target); };
                p.Controls.Add(b); y += 48;
            }
            y = AddHelp(p, T.Get("help.discord"), y + 6, 0);
            y = AddHelp(p, T.Get("help.about"), y, 0);
            return p;
        }

        void HelpAction(string what)
        {
            try
            {
                if (what == "logs") { Directory.CreateDirectory(P.Logs); Process.Start("explorer.exe", "\"" + P.Logs + "\""); return; }
                if (what == "save")
                {
                    string desk = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
                    var saved = new List<string>();
                    foreach (var n in new[] { "play.err", "play.prev.err", "play.out" })
                    {
                        string src = Path.Combine(P.Logs, n);
                        if (!File.Exists(src)) continue;
                        string dst = Path.Combine(desk, "gt5-" + n + ".txt");
                        File.Copy(src, dst, true); saved.Add(Path.GetFileName(dst));
                    }
                    MessageBox.Show(this, saved.Count == 0 ? "GT5\\logs is empty: start the game once first." : string.Format(T.Get("help.saved"), string.Join(", ", saved.ToArray())), "GT5");
                    return;
                }
                Process.Start(what);
            }
            catch (Exception e) { MessageBox.Show(this, e.Message, "GT5"); }
        }

        // -- version and updates --
        void RefreshVersion()
        {
            string v = P.InstalledVersion();
            if (v.StartsWith("development build")) v = T.Get("devbuild") + " " + v.Substring("development build".Length).Trim();
            versionLabel.Text = T.Get("version") + ": " + (v == "" ? "?" : v);
            if (installedLabel != null) installedLabel.Text = T.Get("upd.installed") + ":  " + (v == "" ? "?" : v);
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
                            latestLabel.Text = T.Get("upd.latest") + ":  " + string.Format(T.Get("upd.failed"), err);
                            return;
                        }
                        DateTime when;
                        string d2 = DateTime.TryParse(date, CultureInfo.InvariantCulture, DateTimeStyles.AdjustToUniversal, out when) ? when.ToString("d MMM yyyy", CultureInfo.InvariantCulture) : "";
                        bool newer = Newer(tag, inst);
                        latestLabel.Text = T.Get("upd.latest") + ":  " + tag + (d2 != "" ? "  (" + d2 + ")" : "") + "   " + (newer ? string.Format(T.Get("upd.available"), tag) : T.Get("upd.uptodate"));
                        notes.Text = PlainText(body);
                        updateBadge.Text = string.Format(T.Get("upd.available"), tag);
                        updateBadge.Visible = newer;
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

        void RunUpdate()
        {
            if (GameRunning() || running) { MessageBox.Show(this, T.Get("upd.running"), "GT5"); return; }
            if (!File.Exists(P.Update) || !Directory.Exists(Path.Combine(P.Root, ".git"))) { MessageBox.Show(this, T.Get("upd.nogit"), "GT5"); return; }
            try
            {
                // The launcher closes, so update.ps1 can rebuild it; update.ps1 opens it again at the end.
                var psi = new ProcessStartInfo("powershell.exe",
                    string.Format("-NoProfile -ExecutionPolicy Bypass -File \"{0}\" -Relaunch \"{1}\"", P.Update, Application.ExecutablePath));
                psi.WorkingDirectory = P.Root; psi.UseShellExecute = true;
                Process.Start(psi);
                Close();
            }
            catch (Exception e) { MessageBox.Show(this, e.Message, "GT5"); }
        }

        // -- play --
        public void PlayNow() { Play(); }
        void Play()
        {
            if (running || GameRunning()) { status.Text = T.Get("already"); return; }
            if (!File.Exists(P.Exe)) { MessageBox.Show(this, T.Get("notbuilt"), "GT5"); return; }
            if (!File.Exists(P.Elf)) { MessageBox.Show(this, T.Get("noelf"), "GT5"); return; }
            if (!File.Exists(P.Vol) && P.VolInPieces() && File.Exists(P.FixFiles))
            {
                if (MessageBox.Show(this, T.Get("volparts"), "GT5", MessageBoxButtons.YesNo) != DialogResult.Yes) return;
                try
                {
                    var fix = Process.Start(new ProcessStartInfo("powershell.exe",
                        "-NoProfile -ExecutionPolicy Bypass -File \"" + P.FixFiles + "\""));
                    Cursor = Cursors.WaitCursor;
                    while (!fix.WaitForExit(200)) Application.DoEvents();
                }
                catch (Exception e) { MessageBox.Show(this, e.Message, "GT5"); }
                finally { Cursor = Cursors.Default; }
            }
            if (!File.Exists(P.Vol)) { MessageBox.Show(this, T.Get("novol"), "GT5"); return; }
            try { S.Save(); } catch (Exception e) { MessageBox.Show(this, "settings.bat: " + e.Message, "GT5"); return; }
            Process cmd;
            try
            {
                var psi = new ProcessStartInfo("cmd.exe", "/c \"\"" + P.PlayBat + "\"\"");
                psi.WorkingDirectory = P.Gt5; psi.UseShellExecute = false; psi.CreateNoWindow = true;
                psi.WindowStyle = ProcessWindowStyle.Hidden;
                cmd = Process.Start(psi);
            }
            catch (Exception e) { MessageBox.Show(this, e.Message, "GT5"); return; }
            running = true;
            playButton.Text = T.Get("running"); playButton.Enabled = false;
            status.Text = T.Get("started");
            string mode = S.Get("GT5_LAUNCH_WINDOW");
            bool hide = S.Get("GT5_LAUNCHER_HIDE") != "0";
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
                        Thread.Sleep(600);
                        ApplyWindowMode(hwnd, mode);
                    }
                    try { game.WaitForExit(); } catch { }
                }
                try { cmd.WaitForExit(); } catch { }
                bool quick = (DateTime.Now - start).TotalSeconds < 25;
                try
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        running = false;
                        playButton.Text = T.Get("play"); playButton.Enabled = true;
                        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
                        Activate();
                        status.Text = quick ? T.Get("quickexit") : T.Get("stopped");
                    });
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
}
