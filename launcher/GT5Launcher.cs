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
            // --update-window <install folder> <launcher> [update.ps1 arguments]: the update window (#39), run
            // by "Update now" from a copy of this exe, so the build can replace GT5Launcher.exe meanwhile.
            if (args.Length >= 3 && args[0] == "--update-window")
            {
                var rest = new string[args.Length - 3];
                Array.Copy(args, 3, rest, 0, rest.Length);
                Application.Run(new UpdateForm(args[1], args[2], rest));
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
            { "crashed", new[] { "The game stopped. Windows says: {0}. This is in GT5\\logs\\play.err too (Help: Save logs for a report).", "游戏已停止。Windows 报告：{0}。这也写在 GT5\\logs\\play.err 里（帮助：保存日志用于报告）。" } },
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
            { "intro", new[] { "Skip the opening movie", "跳过开场动画" } },
            { "intro.help", new[] { "A quicker start. On a busy or slower PC the opening movie could stop on one frame at the first start. GT TV still has it.", "启动更快。在繁忙或较慢的电脑上，开场动画在首次启动时可能会卡在一帧。GT TV 中仍可观看。" } },
            { "speed.help", new[] { "30 fps draws every other frame. It only makes the game faster when the graphics card is what holds it back (GPU busy near 100%). On most PCs the CPU is the limit: then the picture gets less smooth for little or no gain (about 10% on a 6-core test, nothing on a player's Ryzen 5 3600). While it's on, the window title shows both numbers, e.g. FPS: 15 (game 30); the game number is the one that counts.", "30 fps 隔帧绘制。只有在显卡是瓶颈（GPU 占用接近 100%）时才会让游戏变快。大多数电脑的瓶颈是 CPU：这时画面变得不流畅，速度几乎没有提升（6 核测试中约 10%，一位玩家的 Ryzen 5 3600 上没有提升）。开启时窗口标题会同时显示两个数字，例如 FPS: 15 (game 30)；以 game 后面的数字为准。" } },
            { "speed.full", new[] { "Draw every frame (recommended)", "每帧都绘制（推荐）" } },
            { "speed.30", new[] { "30 fps: draw every other frame (only if the graphics card is the limit)", "30 fps：隔帧绘制（仅当显卡是瓶颈时）" } },
            { "watch", new[] { "Watch mode by itself", "自动观看模式" } },
            { "watch.help", new[] { "Comes on after 10 s without input (B-Spec, replays): draws fewer frames (see the next setting) and lowers the game's priority. F9 toggles it any time.", "10 秒无操作后自动开启（B-Spec、回放）：减少绘制的帧数（见下一项）并降低游戏优先级。随时可按 F9 切换。" } },
            { "framegen", new[] { "Frame generation (beta)", "帧生成（测试版）" } },
            { "framegen.help", new[] { "Fills the frames that 30 fps and watch mode leave out with frames made on the graphics card, so the picture stays at 60. The game and its CPU work are unchanged; the newest frame reaches the screen one refresh later. Off: only drawn frames are shown.", "用显卡生成的帧补上 30 fps 和观看模式省略的帧，让画面保持 60 帧。游戏本身和 CPU 负载不变；最新的一帧会晚一次刷新显示。关闭时只显示实际绘制的帧。" } },
            { "renderer", new[] { "Renderer", "渲染器" } },
            { "renderer.help", new[] { "DirectX 12 is the port's main renderer and the fastest. Vulkan (the Linux and Android renderer) is here to try: on Windows it's slower for now, some effects may look different, and frame generation and the F9 watch-mode key are DirectX 12 only. Picking Vulkan the first time adds it to your gt5.exe (a one-time build).", "DirectX 12 是本移植的主要渲染器，也是最快的。Vulkan（Linux 和 Android 版使用的渲染器）可供试用：目前在 Windows 上较慢，部分效果可能不同，帧生成和 F9 观看模式快捷键只在 DirectX 12 下可用。第一次选择 Vulkan 时会把它编译进 gt5.exe（只需一次）。" } },
            { "renderer.dx12", new[] { "DirectX 12 (recommended)", "DirectX 12（推荐）" } },
            { "renderer.vk", new[] { "Vulkan (experimental)", "Vulkan（实验性）" } },
            { "vk.add", new[] { "Vulkan isn't in your gt5.exe yet. Add it now?\n\nThis downloads the source code of SDL2 and glslang (about 60 MB, from GitHub) and rebuilds the game's runtime with them: about 10 to 20 minutes, in a window that shows the progress. DirectX 12 keeps working either way.", "你的 gt5.exe 里还没有 Vulkan。现在添加吗？\n\n这会从 GitHub 下载 SDL2 和 glslang 的源代码（约 60 MB），并用它们重新编译游戏的运行库：约 10 到 20 分钟，进度会显示在一个窗口中。无论如何 DirectX 12 都能继续使用。" } },
            { "vk.adding", new[] { "Adding Vulkan to gt5.exe (see the other window)...", "正在把 Vulkan 加入 gt5.exe（见另一个窗口）……" } },
            { "vk.added", new[] { "Vulkan is ready: the next Play uses it.", "Vulkan 已就绪：下次开始游戏时使用。" } },
            { "vk.failed", new[] { "Vulkan couldn't be added (the other window said why), so the renderer is DirectX 12 again.", "未能添加 Vulkan（原因见另一个窗口），渲染器已改回 DirectX 12。" } },
            { "wdraw", new[] { "Watch mode draws", "观看模式绘制" } },
            { "wdraw.help", new[] { "B-Spec mode for laptops: while you only watch, fewer frames are drawn and the race keeps full speed, so more of the CPU is left for the game. The window title shows FPS: x (game y); y is how fast the game runs.", "笔记本的 B-Spec 模式：只观看时绘制更少的帧，比赛保持全速，把更多 CPU 留给游戏。窗口标题显示 FPS: x (game y)；y 是游戏实际运行的速度。" } },
            { "wdraw.2", new[] { "1 frame in 2", "每 2 帧绘制 1 帧" } },
            { "wdraw.3", new[] { "1 frame in 3 (laptops)", "每 3 帧绘制 1 帧（笔记本）" } },
            { "wdraw.4", new[] { "1 frame in 4 (weakest PCs)", "每 4 帧绘制 1 帧（最弱的电脑）" } },
            { "spus", new[] { "Extra SPU threads", "额外 SPU 线程" } },
            { "spus.help", new[] { "PC threads for the PS3's co-processors. 2 suits most CPUs; 0-1 on 4-core CPUs.", "用于 PS3 协处理器的 PC 线程。多数 CPU 用 2；4 核 CPU 用 0-1。" } },
            { "cockpit", new[] { "Cockpit view", "车内视角" } },
            { "cockpit.game", new[] { "As in the game", "游戏原样" } },
            { "cfov", new[] { "Field of view", "视野" } },
            { "cfov.help", new[] { "Only the view from the driver's seat (52.4° from the top of the screen to the bottom in the game; the sides widen with it). The chase, bumper and roof views stay as they are.", "只影响驾驶座视角（游戏中从屏幕上缘到下缘为 52.4°，左右随之变宽）。追尾、保险杠和车顶视角保持不变。" } },
            { "cfov.wider", new[] { "{0}° wider", "加宽 {0}°" } },
            { "cfov.narrower", new[] { "{0}° narrower", "缩小 {0}°" } },
            { "cseat", new[] { "Seat forward / back", "座椅前后" } },
            { "cseat.help", new[] { "Moves your eye in the cockpit view toward the windscreen, or back, from where the game puts the driver.", "在车内视角中把视点移向挡风玻璃，或从游戏设定的驾驶员位置向后移。" } },
            { "cseat.fwd", new[] { "{0} cm forward", "向前 {0} 厘米" } },
            { "cseat.back", new[] { "{0} cm back", "向后 {0} 厘米" } },
            { "cheight", new[] { "Seat height", "座椅高度" } },
            { "cheight.up", new[] { "{0} cm higher", "升高 {0} 厘米" } },
            { "cheight.down", new[] { "{0} cm lower", "降低 {0} 厘米" } },
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
            { "mouse", new[] { "Steer with the mouse", "用鼠标转向" } },
            { "mouse.help", new[] { "In races, move the mouse left and right to steer. Left button = throttle, right button = brake, middle button = straighten the wheel; the keyboard and pads keep working. At the menus the mouse does nothing, and Alt+Tab frees it at any time. The game sees a steering wheel, so a real wheel isn't used while this is on.", "比赛中左右移动鼠标即可转向。左键 = 油门，右键 = 刹车，中键 = 方向盘回正；键盘和手柄仍然可用。在菜单中鼠标不起作用，随时按 Alt+Tab 即可释放鼠标。游戏会把它当作方向盘，所以开启时不会使用真实方向盘。" } },
            { "msens", new[] { "Mouse steering speed", "鼠标转向灵敏度" } },
            { "msens.help", new[] { "Normal: mouse movement about the width of a 1080p screen turns the wheel from straight ahead to full lock (at Windows' default pointer speed).", "正常：鼠标移动约一个 1080p 屏幕的宽度，方向盘从正前方转到打满（按 Windows 默认指针速度）。" } },
            { "msens.normal", new[] { "Normal", "正常" } },
            { "mcenter", new[] { "Wheel straightens by itself", "方向盘自动回正" } },
            { "mcenter.help", new[] { "When you stop moving the mouse. Off: the wheel stays where the mouse left it.", "停止移动鼠标时回正。关：方向盘停留在鼠标留下的位置。" } },
            { "mcenter.slow", new[] { "Slowly", "慢" } },
            { "mcenter.mid", new[] { "Medium", "中" } },
            { "mcenter.fast", new[] { "Quickly", "快" } },
            { "mouse.short", new[] { "Mouse steering", "鼠标转向" } },
            { "keys", new[] { "Keyboard: Z = Cross (OK), X = Circle, A = Square, S = Triangle, arrows = D-pad, Enter = Start, Tab = Select (change them on the Controls page). DualSense, DualShock 4 and Xbox pads work too.", "键盘：Z = ✕（确定），X = ○，A = □，S = △，方向键 = 十字键，Enter = Start，Tab = Select（可在“操控”页面更改）。也支持 DualSense、DualShock 4 和 Xbox 手柄。" } },
            // Keyboard keys (PS3_KEYS)
            { "kb.title", new[] { "Keyboard keys", "键盘按键" } },
            { "kb.help", new[] { "Click a row, then press the key you want (Esc cancels); right-click a row to add a second key, × clears it. A key that's already used moves to the new row. The keyboard is used when no pad is connected. The actions are GT5's own pad layout (Options > Hardware > Change Button Assignments in the game); change them there and these rows follow the button, not the action. With mouse steering on, the keys work the same.", "点击一行，然后按下想要的键（Esc 取消）；右键点击一行可添加第二个键，× 清除。已被使用的键会移到新的一行。未连接手柄时使用键盘。动作为 GT5 自带的手柄布局（游戏中：选项 > 硬件 > 更改按键分配）；在那里更改后，这里的行对应的是按钮而不是动作。开启鼠标转向时按键同样有效。" } },
            { "kb.views", new[] { "Left View, Right View and Flash Lights have no pad button in GT5's default layout: give them one in the game (for example the right stick's left and right), then bind a key to that button here.", "在 GT5 默认布局中，“左视角”“右视角”和“闪灯”没有手柄按钮：先在游戏中为它们分配一个按钮（例如右摇杆的左和右），再在这里为该按钮绑定按键。" } },
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
            { "kb.a.rstick_left", new[] { "(nothing by default: Left View?)", "（默认无：左视角？）" } },
            { "kb.a.rstick_right", new[] { "(nothing by default: Right View?)", "（默认无：右视角？）" } },
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
            // The update window
            { "uw.title", new[] { "Updating Gran Turismo 5", "正在更新 Gran Turismo 5" } },
            { "uw.start", new[] { "Starting the update", "正在开始更新" } },
            { "uw.fetch", new[] { "Getting the new version", "正在获取新版本" } },
            { "uw.prep", new[] { "Getting the build ready", "正在准备构建" } },
            { "uw.relift", new[] { "Translating the game's code again (about 10 minutes)", "正在重新转换游戏代码（约 10 分钟）" } },
            { "uw.build", new[] { "Building the game", "正在构建游戏" } },
            { "uw.files", new[] { "{0:N0} of {1:N0} steps", "第 {0:N0} / {1:N0} 步" } },
            { "uw.retry", new[] { "The build stopped; trying again with fewer jobs", "构建中断，正在以更少的并行任务重试" } },
            { "uw.finish", new[] { "Finishing", "正在完成" } },
            { "uw.done", new[] { "Updated to {0}", "已更新到 {0}" } },
            { "uw.donegen", new[] { "Update finished", "更新完成" } },
            { "uw.uptodate", new[] { "Already up to date", "已是最新版本" } },
            { "uw.opening", new[] { "Opening the launcher...", "正在打开启动器..." } },
            { "uw.failed", new[] { "The update stopped", "更新已中止" } },
            { "uw.log", new[] { "The full text is in GT5\\logs\\update.log: attach it when you report this.", "完整内容在 GT5\\logs\\update.log：报告问题时请附上。" } },
            { "uw.details", new[] { "Details", "详细信息" } },
            { "uw.hide", new[] { "Hide details", "隐藏详细信息" } },
            { "uw.open", new[] { "Open the launcher", "打开启动器" } },
            { "uw.close", new[] { "Close", "关闭" } },
            { "uw.stop", new[] { "The update is still running. Stop it?\n\nThe game may not start until an update finishes: run Update now again later.", "更新仍在进行。要停止吗？\n\n在更新完成之前游戏可能无法启动：之后请再次点击“立即更新”。" } },
            // Help
            { "help.logs", new[] { "Open the logs folder", "打开日志文件夹" } },
            { "help.save", new[] { "Save logs for a report", "保存日志用于报告" } },
            { "help.saved", new[] { "Saved to your desktop as {0}. Attach it to your report (GitHub accepts .txt).", "已保存到桌面：{0}。请附加到报告中（GitHub 接受 .txt）。" } },
            { "help.report", new[] { "Report a problem", "报告问题" } },
            { "help.shaders", new[] { "Clear the shader cache", "清除着色器缓存" } },
            { "help.pkg", new[] { "Install a game update or DLC (.pkg)", "安装游戏更新或 DLC（.pkg）" } },
            { "pkg.running", new[] { "Close the game first: installing a package changes its files.", "请先关闭游戏：安装包会更改游戏文件。" } },
            { "pkg.missing", new[] { "GT5\\scripts\\install_pkg.ps1 is missing: update first (Updates page).", "缺少 GT5\\scripts\\install_pkg.ps1：请先更新（更新页面）。" } },
            { "shaders.ask", new[] { "Delete the shader cache ({0} files, {1:0} MB)?\n\nThe game compiles its shaders again as it needs them, so the first visit to each menu, car and track stutters again for a while, as after a fresh install. Saves, settings and game files are not touched.", "删除着色器缓存（{0} 个文件，{1:0} MB）？\n\n游戏会在需要时重新编译着色器，因此每个菜单、车辆和赛道第一次出现时会再次短暂卡顿，就像刚安装时一样。存档、设置和游戏文件不受影响。" } },
            { "shaders.empty", new[] { "The shader cache is already empty.", "着色器缓存已经是空的。" } },
            { "shaders.done", new[] { "Shader cache cleared. The game compiles its shaders again as you play.", "已清除着色器缓存。游戏会在你游玩时重新编译着色器。" } },
            { "shaders.partly", new[] { "Some files couldn't be deleted: close the game and try again.", "部分文件无法删除：请关闭游戏后重试。" } },
            { "shaders.running", new[] { "The game is running: close it first.", "游戏正在运行：请先关闭游戏。" } },
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
            new[] { "GT5_SPEED", "" }, new[] { "PS3_SKIP_INTRO", "1" }, new[] { "LD_WATCH_DRAW", "" },
            new[] { "LD_FRAMEGEN", "" }, new[] { "GT5_RENDERER", "" },
            new[] { "PS3_MOUSE_STEER", "" }, new[] { "PS3_MOUSE_STEER_SENS", "" }, new[] { "PS3_MOUSE_STEER_CENTER", "" },
            new[] { "PS3_KEYS", "" },
            new[] { "PS3_COCKPIT_FOV", "" }, new[] { "PS3_COCKPIT_SEAT", "" }, new[] { "PS3_COCKPIT_HEIGHT", "" },
        };
        // True when there was no settings.bat yet: the launcher's first start on this install.
        public bool Fresh;
        public Dictionary<string, string> V = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        readonly string path;
        const string SpeedMark = "REM Speed";
        static readonly Regex SetLine = new Regex("^\\s*set\\s+\"([A-Za-z0-9_]+)=(.*)\"\\s*$", RegexOptions.IgnoreCase);

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
        }

        // The set "NAME=value" lines of a settings.bat. The Speed lines Save() adds at the end are not
        // settings of their own. v0.1.20-v0.1.22 wrote them with no marker, so they were read back as
        // LD_WATCH=2 and 30 fps stayed on after Speed was set back: the first value of a key counts, and
        // LD_WATCH_PRIORITY that came with such a repeated LD_WATCH is dropped.
        static Dictionary<string, string> Read(string path)
        {
            var r = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            bool speedLines = false;
            foreach (var line in File.ReadAllLines(path))
            {
                if (line.TrimStart().StartsWith(SpeedMark, StringComparison.OrdinalIgnoreCase)) break;
                var m = SetLine.Match(line);
                if (!m.Success) continue;
                string k = m.Groups[1].Value;
                if (r.ContainsKey(k)) { if (k.Equals("LD_WATCH", StringComparison.OrdinalIgnoreCase)) speedLines = true; continue; }
                if (speedLines && k.Equals("LD_WATCH_PRIORITY", StringComparison.OrdinalIgnoreCase)) continue;
                r[k] = m.Groups[2].Value;
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
            var sb = new StringBuilder();
            sb.AppendLine("@echo off");
            sb.AppendLine("REM Written by the GT5 Launcher; play.bat runs it after its own defaults. Change these in the");
            sb.AppendLine("REM launcher, or edit them here: one set \"NAME=value\" per line, an empty value means not set.");
            sb.AppendLine("REM Lines of your own in that form (like PS3_WHEEL_PEDALS) are kept; other lines are not.");
            var keys = new List<string>(V.Keys);
            keys.Sort(StringComparer.OrdinalIgnoreCase);
            foreach (var k in keys)
                sb.AppendLine(string.Format("set \"{0}={1}\"", k, V[k].Replace("\"", "")));
            // Speed (GT5_SPEED) turns into the renderer's own switches, after the keys so it wins.
            // GT5 counts time in frames: these draw fewer frames instead of letting the game slow down.
            string speed = Get("GT5_SPEED");
            if (speed == "30")
            {
                sb.AppendLine(SpeedMark + " (GT5_SPEED=30): the renderer's own switches. The launcher writes these from Speed; reading stops here.");
                sb.AppendLine("set \"LD_WATCH=2\""); sb.AppendLine("set \"LD_WATCH_PRIORITY=0\""); sb.AppendLine("set \"LD_WATCH_DRAW=2\"");
            }
            File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
            Base = new Dictionary<string, string>(V, StringComparer.OrdinalIgnoreCase);
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
            lo = Math.Max(l, shown); hi = Math.Max(h, lo); step = text; sub = ""; secs = expect;
            within = 0; counted = false; stepT0 = DateTime.Now;
        }

        void Line(string l)
        {
            if (log != null) { try { log.WriteLine(l); } catch { } }
            tail.Add(l); if (tail.Count > 1500) tail.RemoveRange(0, 300); tailChanged = true;
            string t = l.Trim();
            if (t.Length > 0) raw = t;
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
            m = SwitchLine.Match(t);
            if (m.Success) { label = m.Groups[2].Value.Trim(); Step(0.02, 0.08, T.Get("uw.fetch"), 15); return; }
            if (t.StartsWith("Reinstalling ")) { label = t.Substring(13).Trim(); Step(0.02, 0.08, T.Get("uw.fetch"), 15); return; }
            if (t.StartsWith("Already on ")) { upToDate = true; return; }
            if (t.StartsWith("ps3recomp:")) { Step(0.08, 0.12, T.Get("uw.prep"), 40); return; }
            if (t.StartsWith("Re-lifting")) { relifted = true; Step(0.12, 0.55, T.Get("uw.relift"), 600); return; }
            if (t.StartsWith("Building (only what changed)")) { Step(relifted ? 0.55 : 0.12, 0.97, T.Get("uw.build"), 300); return; }
            if (t.StartsWith("The build stopped. Trying again")) { step = T.Get("uw.retry"); counted = false; return; }
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
            if (code != 0) { Fail(failMsg != "" ? failMsg : raw); return; }
            within = 1; shown = Math.Max(shown, 0.999);
            step = upToDate ? T.Get("uw.uptodate") : label != "" ? string.Format(T.Get("uw.done"), label) : T.Get("uw.donegen");
            sub = T.Get("uw.opening");
        }

        // Segoe UI has no Chinese: Windows then mixes fonts character by character.
        static Font UF(float size, FontStyle st)
        {
            return T.Zh ? new Font("Microsoft YaHei UI", size, st, GraphicsUnit.Point) : Look.F(size, st);
        }

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
                if (MessageBox.Show(this, T.Get("uw.stop"), T.Get("uw.title"), MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
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

        // First start on this install: laptops and small CPUs start at 720p, easier on their graphics chip.
        // Speed stays at every frame: drawing half the frames only helps when the graphics card is the
        // limit, and on a player's 6-core desktop it gained nothing (discussion #1). Everything stays changeable.
        void AutoPick()
        {
            int n = Environment.ProcessorCount;
            var bs = SystemInformation.PowerStatus.BatteryChargeStatus;
            bool laptop = (bs & BatteryChargeStatus.NoSystemBattery) == 0 && bs != BatteryChargeStatus.Unknown;
            if (n <= 8 || (laptop && n <= 12)) S.Set("PS3_VIDEO_MODE", "720");
        }

        public LauncherForm(bool screenshots)
        {
            shots = screenshots;
            S = new Settings(P.Settings);
            if (S.Fresh) AutoPick();
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
                // Vulkan picked but not built into gt5.exe yet: offer the one-time build, or go back to DirectX 12.
                if (key == "GT5_RENDERER" && ((Choice)cb.SelectedItem).Value == "vulkan" && !VulkanBuilt() && !AddVulkan(cb))
                { cb.SelectedIndex = 0; return; }
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
            string wheel = S.Get("PS3_MOUSE_STEER") == "1" ? T.Get("mouse.short") : S.Get("PS3_WHEEL") == "off" ? T.Get("off") : T.Get("wheel.on");
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
            y = AddCombo(p, "renderer", "renderer.help", "GT5_RENDERER", new[] { new Choice("", T.Get("renderer.dx12")), new Choice("vulkan", T.Get("renderer.vk")) }, y);
            y = AddCombo(p, "res", "res.help", "PS3_VIDEO_MODE", new[] { new Choice("1080", T.Get("res.1080")), new Choice("720", T.Get("res.720")) }, y);
            y = AddCombo(p, "speed", "speed.help", "GT5_SPEED", new[] { new Choice("", T.Get("speed.full")), new Choice("30", T.Get("speed.30")) }, y);
            y = AddCombo(p, "shape", "shape.help", "PS3_ASPECT", new[] { new Choice("auto", T.Get("shape.auto")), new Choice("16:9", "16:9"), new Choice("21:9", "21:9"), new Choice("32:9", "32:9") }, y);
            y = AddCombo(p, "window", "window.help", "GT5_LAUNCH_WINDOW", new[] { new Choice("windowed", T.Get("window.windowed")), new Choice("max", T.Get("window.max")), new Choice("borderless", T.Get("window.borderless")) }, y);
            y = AddCombo(p, "size", "size.help", "PS3_WINDOW_SIZE", new[] { new Choice("", T.Get("size.default")), new Choice("1280x720", "1280 x 720"), new Choice("1600x900", "1600 x 900"), new Choice("1920x1080", "1920 x 1080"), new Choice("2560x1440", "2560 x 1440"), new Choice("2560x1080", "2560 x 1080 (21:9)"), new Choice("3440x1440", "3440 x 1440 (21:9)"), new Choice("3840x2160", "3840 x 2160") }, y);
            y = AddCheck(p, "watch", "watch.help", "LD_WATCH", "1", y);
            y = AddCombo(p, "wdraw", "wdraw.help", "LD_WATCH_DRAW", new[] { new Choice("", T.Get("wdraw.2")), new Choice("3", T.Get("wdraw.3")), new Choice("4", T.Get("wdraw.4")) }, y);
            y = AddCheck(p, "framegen", "framegen.help", "LD_FRAMEGEN", "1", y);
            y = AddCombo(p, "spus", "spus.help", "SPURS_EXTRA_SPUS", new[] { new Choice("0", "0"), new Choice("1", "1"), new Choice("2", "2"), new Choice("3", "3"), new Choice("4", "4") }, y);
            // Cockpit view (GT5\src\gt5_cockpit.cpp): "" = the game's own camera.
            p.Controls.Add(new Label { Text = T.Get("cockpit"), Font = Look.F(11f, FontStyle.Bold), ForeColor = Look.Text, AutoSize = true, Location = new Point(0, y + 8) });
            y += 40;
            y = AddCombo(p, "cfov", "cfov.help", "PS3_COCKPIT_FOV", CockpitChoices(new[] { -10, -5, 5, 10, 15, 20, 25, 30, 40 }, "cfov.wider", "cfov.narrower"), y);
            y = AddCombo(p, "cseat", "cseat.help", "PS3_COCKPIT_SEAT", CockpitChoices(new[] { -30, -20, -10, -5, 5, 10, 20, 30 }, "cseat.fwd", "cseat.back"), y);
            y = AddCombo(p, "cheight", null, "PS3_COCKPIT_HEIGHT", CockpitChoices(new[] { -10, -5, 5, 10 }, "cheight.up", "cheight.down"), y);
            return p;
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
            y = AddCheck(p, "padbg", null, "PAD_BACKGROUND", "1", y);
            y = AddCheck(p, "clip", null, "PAD_CURSOR_CLIP", "1", y);
            y = AddCombo(p, "wheel", "wheel.help", "PS3_WHEEL", new[] { new Choice("", T.Get("wheel.on")), new Choice("off", T.Get("wheel.off")) }, y + 8);
            y = AddCombo(p, "range", "range.help", "PS3_WHEEL_RANGE", new[] { new Choice("", T.Get("size.default")), new Choice("270", "270°"), new Choice("540", "540°"), new Choice("720", "720°"), new Choice("900", "900°"), new Choice("1080", "1080°") }, y);
            var ffb = new List<Choice> { new Choice("", T.Get("size.default")) };
            for (int v = 100; v >= 0; v -= 10) ffb.Add(new Choice(v.ToString(), v + "%"));
            y = AddCombo(p, "ffb", null, "PS3_WHEEL_FFB", ffb.ToArray(), y);
            y = AddCheck(p, "ffbinv", null, "PS3_WHEEL_FFB_INVERT", "1", y);
            y = AddCheck(p, "mouse", "mouse.help", "PS3_MOUSE_STEER", "1", y + 10);
            y = AddCombo(p, "msens", "msens.help", "PS3_MOUSE_STEER_SENS", new[] { new Choice("50", "50%"), new Choice("75", "75%"), new Choice("", T.Get("msens.normal") + " (100%)"), new Choice("150", "150%"), new Choice("200", "200%"), new Choice("300", "300%") }, y);
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
                kv.Value.Normal = wait ? Look.Accent : Look.Panel;
                kv.Value.BackColor = kv.Value.Normal;
                kv.Value.ForeColor = names.Count == 0 && !wait ? Look.Dim : Look.Text;
            }
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
            y = AddCheck(p, "intro", "intro.help", "PS3_SKIP_INTRO", "1", y + 4);
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
                new[] { "help.shaders", "shaders" }, new[] { "help.pkg", "pkg" },
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
                if (what == "shaders") { ClearShaderCache(); return; }
                if (what == "pkg")
                {
                    // install_pkg.ps1 does the work through RPCS3, in a console window of its own.
                    if (GameRunning() || running) { MessageBox.Show(this, T.Get("pkg.running"), "GT5"); return; }
                    string script = Path.Combine(Path.Combine(P.Gt5, "scripts"), "install_pkg.ps1");
                    if (!File.Exists(script)) { MessageBox.Show(this, T.Get("pkg.missing"), "GT5"); return; }
                    var dlg = new OpenFileDialog();
                    dlg.Filter = "PS3 packages (*.pkg)|*.pkg"; dlg.Multiselect = true; dlg.Title = T.Get("help.pkg");
                    if (dlg.ShowDialog(this) != DialogResult.OK || dlg.FileNames.Length == 0) return;
                    var a = new StringBuilder("-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"");
                    foreach (var f in dlg.FileNames) a.Append(" \"" + f + "\"");
                    var psi = new ProcessStartInfo("powershell.exe", a.ToString());
                    psi.UseShellExecute = true; psi.WorkingDirectory = P.Gt5;
                    Process.Start(psi);
                    return;
                }
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

        // build\SDL2.dll next to gt5.exe: the exe has the Vulkan renderer (GT5's CMakeLists copies it in when it
        // does and removes it when it doesn't).
        bool VulkanBuilt() { return File.Exists(Path.Combine(Path.GetDirectoryName(P.Exe), "SDL2.dll")); }

        // Runs scripts\add_vulkan.ps1 in its own window. True when it started; the renderer combo goes back to
        // DirectX 12 by itself if the build doesn't end with Vulkan in.
        bool AddVulkan(ComboBox cb)
        {
            string script = Path.Combine(P.Gt5, "scripts", "add_vulkan.ps1");
            if (!File.Exists(script) || !File.Exists(P.Exe)) { MessageBox.Show(this, T.Get("notbuilt"), "GT5"); return false; }
            if (GameRunning() || running) { MessageBox.Show(this, T.Get("upd.running"), "GT5"); return false; }
            if (MessageBox.Show(this, T.Get("vk.add"), "GT5", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return false;
            Process pr;
            try
            {
                var psi = new ProcessStartInfo("powershell.exe", "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"");
                psi.WorkingDirectory = P.Gt5; psi.UseShellExecute = true;
                pr = Process.Start(psi);
            }
            catch (Exception e) { MessageBox.Show(this, e.Message, "GT5"); return false; }
            running = true;   // no Play while gt5.exe is rebuilt
            playButton.Enabled = false; cb.Enabled = false;
            status.Text = T.Get("vk.adding");
            var th = new Thread(delegate ()
            {
                try { pr.WaitForExit(); } catch { }
                try
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        running = false; playButton.Enabled = true; cb.Enabled = true;
                        if (VulkanBuilt()) status.Text = T.Get("vk.added");
                        else { status.Text = T.Get("vk.failed"); cb.SelectedIndex = 0; }
                    });
                }
                catch { }
            });
            th.IsBackground = true; th.Start();
            return true;
        }

        void RunUpdate()
        {
            if (GameRunning() || running) { MessageBox.Show(this, T.Get("upd.running"), "GT5"); return; }
            if (!File.Exists(P.Update) || !Directory.Exists(Path.Combine(P.Root, ".git"))) { MessageBox.Show(this, T.Get("upd.nogit"), "GT5"); return; }
            // The update window: a copy of this exe, since the launcher closes so the build can replace it.
            string copy = Path.Combine(Path.GetTempPath(), "GT5Updater.exe");
            try
            {
                File.Copy(Application.ExecutablePath, copy, true);
                var w = new ProcessStartInfo(copy, string.Format("--update-window \"{0}\" \"{1}\"", P.Root, Application.ExecutablePath));
                w.WorkingDirectory = P.Root; w.UseShellExecute = false;
                Process.Start(w);
                Close();
                return;
            }
            catch { }   // a copy still open from an earlier update, say: the console as before
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
                // A game that ends without writing why (a crash its own handler couldn't report, or a
                // virus scanner stopping it) is usually in Windows' own records: put that where a report
                // will carry it.
                string why = WindowsSaysWhy(start);
                if (why != null)
                    try { File.AppendAllText(Path.Combine(P.Logs, "play.err"), "\r\n[launcher] Windows says: " + why + "\r\n"); } catch { }
                try
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        running = false;
                        playButton.Text = T.Get("play"); playButton.Enabled = true;
                        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
                        Activate();
                        status.Text = why != null ? string.Format(T.Get("crashed"), why) : quick ? T.Get("quickexit") : T.Get("stopped");
                    });
                }
                catch { }
            });
            th.IsBackground = true; th.Start();
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
