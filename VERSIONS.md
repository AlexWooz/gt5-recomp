# 🗂️ Version history

Every release of the port, newest first. The details of each are on the
[Releases page](https://github.com/MormonFTP-hub/gt5-recomp/releases) and in [CHANGELOG.md](CHANGELOG.md);
the launcher's Updates page shows the newest one's notes.

| Version | Date | What changed |
|---|---|---|
| [v0.1.77](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.77) | 16 Nov 2026 | Vulkan: exposure read from the right surface (no more washed-out races), async readbacks and shader prebuild on by default (#38, #20) |
| [v0.1.76](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.76) | 16 Nov 2026 | Linux: your own keyboard keys (PS3_KEYS) and the small-CPU setting up to 12 threads, as on Windows |
| [v0.1.75](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.75) | 16 Nov 2026 | Cockpit view (#36): a wider field of view and the seat moved forward, back, up or down, in the launcher's Display page; off by default |
| [v0.1.74](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.74) | 16 Nov 2026 | Choose your own keyboard keys: every pad button can be rebound on the launcher's Controls page or with PS3_KEYS, labelled with GT5's own actions |
| [v0.1.73](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.73) | 16 Nov 2026 | Vulkan: frame generation and even frame pacing on a presenter thread; command thread reuses images, keeps converted geometry and copies constants once (#38) |
| [v0.1.72](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.72) | 16 Nov 2026 | Steer with the mouse (#37): in races the mouse turns the emulated G27, left button throttle, right brake; off by default |
| [v0.1.71](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.71) | 15 Nov 2026 | Vulkan draws at 1080p in the 1080p mode; its title shows FPS and the resolution (the DirectX 12 title's resolution fixed too) |
| [v0.1.70](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.70) | 15 Nov 2026 | Update now opens an update window (progress over your Photo Mode pictures, details one click away); crash reports name the DLL a crash happened in |
| [v0.1.69](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.69) | 15 Nov 2026 | Wheels: Thrustmaster pedal layout (normal and combined mode), a button as a pedal, reverse on a button (MT) |
| [v0.1.68](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.68) | 15 Nov 2026 | Sound on busy and small CPUs: no more 85 ms holes, and the sound no longer runs late (the 2.11 audio lag) |
| [v0.1.67](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.67) | 15 Nov 2026 | Launcher: lines you add to settings.bat by hand are kept (they were overwritten on the next Play) |
| [v0.1.66](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.66) | 15 Nov 2026 | The check that an SPU job's code is unchanged costs half as much: a little less CPU for the sound on small CPUs |
| [v0.1.65](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.65) | 15 Nov 2026 | 6-core/12-thread CPUs: the game's waiting threads sleep instead of spinning, 5-14% less CPU at the same frame rate |
| [v0.1.64](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.64) | 15 Nov 2026 | Linux/Vulkan: races start at the right brightness, car reflections and shadows drawn in full (#20) |
| [v0.1.63](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.63) | 15 Nov 2026 | Less CPU for every frame on small CPUs and the Steam Deck (community pull request #33), the sound filters a little cheaper; one re-lift |
| [v0.1.62](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.62) | 15 Nov 2026 | Cockpit view: dust and smoke from the cars ahead no longer cover the dashboard |
| [v0.1.61](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.61) | 15 Nov 2026 | Updates recompile only what changed: unchanged runtime files keep their timestamps |
| [v0.1.60](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.60) | 15 Nov 2026 | Adding Vulkan builds with Visual Studio 18; setup and the installer skip an incomplete Visual Studio |
| [v0.1.59](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.59) | 15 Nov 2026 | Steering wheels: steering and pedals no longer get stuck in a race when a busy PC falls behind |
| [v0.1.58](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.58) | 15 Nov 2026 | Vulkan window: opens at the window size, resizes and maximises, and survives being minimised |
| [v0.1.57](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.57) | 15 Nov 2026 | Sound on small CPUs: the audio chain a little cheaper, its EQ module a third cheaper |
| [v0.1.56](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.56) | 15 Nov 2026 | Renderer choice in the launcher: DirectX 12 (default) or Vulkan (experimental, added with a one-time build) |
| [v0.1.55](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.55) | 15 Nov 2026 | Esc asks before it quits the game |
| [v0.1.54](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.54) | 15 Nov 2026 | Frame generation (beta): 30 fps and watch mode look like 60, made on the graphics card |
| [v0.1.53](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.53) | 15 Nov 2026 | Saves over 2 MB load again instead of the game closing at every start; wheels without a d-pad move through the menus |
| [v0.1.52](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.52) | 15 Nov 2026 | Vulkan renderer: new shaders are handed over faster on scene changes |
| [v0.1.51](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.51) | 14 Nov 2026 | Photo Mode: Auto Focus no longer ghosts the viewfinder; exposure after a photo stays right at 1080p |
| [v0.1.50](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.50) | 14 Nov 2026 | Linux: the game window opens on KDE Plasma under Wayland (#32) |
| [v0.1.49](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.49) | 14 Nov 2026 | Linux: no more freezes when something new comes on screen, and road markings sit on the road (#20) |
| [v0.1.48](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.48) | 14 Nov 2026 | Photo Mode pictures have their real colours again: no more yellow-green tint or coloured ghost copies |
| [v0.1.47](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.47) | 14 Nov 2026 | The game starts with Windows' Control Flow Guard forced on (#31), and Options > Display no longer crashes it |
| [v0.1.46](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.46) | 14 Nov 2026 | Races at 1080p get the right brightness: auto-exposure read a leftover image (Speed Test blown out to white, some tracks far too dark) |
| [v0.1.45](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.45) | 14 Nov 2026 | Install game updates and DLC from their .pkg files (launcher, Help), through RPCS3 |
| [v0.1.44](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.44) | 14 Nov 2026 | The launcher says why the game stopped when Windows recorded it (a crash the game couldn't report, or Microsoft Defender) |
| [v0.1.43](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.43) | 14 Nov 2026 | Leaner SPU code: the sound mixer 22% faster, all SPU jobs 14% less CPU (one re-lift) |
| [v0.1.42](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.42) | 13 Nov 2026 | Linux and the Steam Deck much quicker, and the PS3's vector multiply-add four lanes at once (community pull request #30); a steering wheel that stops answering no longer keeps driving the car |
| [v0.1.41](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.41) | 13 Nov 2026 | Small CPUs and laptops: the graphics threads sleep instead of spinning while they wait (20% less CPU on 4 cores, +25% frames when the CPU is shared) |
| [v0.1.40](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.40) | 13 Nov 2026 | Car lamps show their lights again: red tail lights, headlights with their reflectors |
| [v0.1.39](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.39) | 13 Nov 2026 | Cockpit view with a game update (2.xx): the windows are clear again |
| [v0.1.38](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.38) | 13 Nov 2026 | Steering wheels: a Genius Speed Wheel 3 is found by itself, and pedals on one axis split into throttle and brake |
| [v0.1.37](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.37) | 13 Nov 2026 | Clear the shader cache from the launcher (Help) or ClearShaders.bat |
| [v0.1.36](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.36) | 13 Nov 2026 | The GT5 Master Mod runs (2.11, US): setup names its executable; README and STATUS list it |
| [v0.1.35](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.35) | 13 Nov 2026 | An installed game update gets its executable by itself: setup and Update unlock its EBOOT.BIN with RPCS3 |
| [v0.1.34](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.34) | 13 Nov 2026 | Setup catches a game update installed without its executable (black screen before the main menu) |
| [v0.1.33](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.33) | 13 Nov 2026 | Linux: races full screen, right exposure, sound effects on time (#20) |
| [v0.1.32](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.32) | 13 Nov 2026 | First-visit stutter about 3x shorter: new shaders compile in parallel (community pull request #28 by AlexWooz) |
| [v0.1.31](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.31) | 13 Nov 2026 | DLC works offline with your own licences (community pull request #25 by AlexWooz); README: Linux in Requirements (#20) |
| [v0.1.30](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.30) | 13 Nov 2026 | Game update 2.17 runs on the US and European releases (community pull request #23 by AlexWooz); README: saves are in GT5\gamedata |
| [v0.1.29](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.29) | 13 Nov 2026 | SPU code runs leaner: SPURS jobs take 20% less CPU, about 7% more frames on 4 cores (one re-lift) |
| [v0.1.28](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.28) | 13 Nov 2026 | About 10% faster on small CPUs (waits that nap instead of spinning, four upstream ps3recomp speed-ups); build without Visual Studio (get-toolchain.ps1, #22) |
| [v0.1.27](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.27) | 12 Nov 2026 | B-Spec mode for laptops: watch mode can draw 1 frame in 3 or 4; Steam Deck route in the README and setup.sh |
| [v0.1.26](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.26) | 12 Nov 2026 | Sound mixer costs about 12% less CPU: faster SPU shufb, job-image lookup cache 8 -> 64 |
| [v0.1.25](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.25) | 12 Nov 2026 | Launcher fits 4K and other high-scaling screens (#9) |
| [v0.1.24](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.24) | 12 Nov 2026 | Linux: a native build (setup.sh, GT5/play.sh), new and early |
| [v0.1.23](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.23) | 12 Nov 2026 | Speed: 30 fps no longer picked by itself, launcher bug that kept it on fixed, title shows game frames; 1 ms timer kept when hidden; installer: Asian release and 2.01; Discord server |
| [v0.1.22](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.22) | 12 Nov 2026 | Game updates (2.xx) run: SPURS workload flag, 2.01's SPU jobs; Hong Kong/Asian release BCAS20108 tested (01.00 and 2.01) (#18) |
| [v0.1.21](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.21) | 12 Nov 2026 | Skip the opening movie at startup (PS3_SKIP_INTRO, on by default): under heavy CPU load it could freeze the first start (#10) |
| [v0.1.20](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.20) | 12 Nov 2026 | Laptops draw on their dedicated GPU ([gpu] line in play.err, PS3_GPU); launcher Speed setting (30 fps) picked for your PC |
| [v0.1.19](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.19) | 12 Nov 2026 | Fix the black screen from a GT.VOL split into pieces by a FAT32 dump, or missing (#12): joined automatically, and the launcher warns |
| [v0.1.18](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.18) | 12 Nov 2026 | Installer: recognise GT5 by its Chinese, Japanese and Korean titles (#18); say when a copy has a 2.xx update built in |
| [v0.1.17](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.17) | 12 Nov 2026 | Alt+Enter no longer freezes the picture (#10) |
| [v0.1.16](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.16) | 12 Nov 2026 | Fix wrong track textures (#14): GT5's graphics-memory copies (NV0039) now happen |
| [v0.1.15](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.15) | 12 Nov 2026 | The launcher: settings, borderless full screen, Play, Updates and Help in one window (English and Chinese); double-click `Update.bat`; the installer lets other GT5 releases through |
| [v0.1.14](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.14) | 12 Nov 2026 | Fix updates stopping with "Access is denied" on `GT5\spu`; the version in the window title |
| [v0.1.13](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.13) | 11 Nov 2026 | Hotfix: installs and updates work again (v0.1.12's patch didn't apply) |
| [v0.1.12](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.12) | 11 Nov 2026 | 1080p fixed and the default again: the window shows the whole picture |
| [v0.1.11](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.11) | 11 Nov 2026 | Hotfix: back to 720p while v0.1.10's 1080p window was cropped |
| [v0.1.10](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.10) | 11 Nov 2026 | 1080p: the game's own 1080p mode; `play.bat` keeps the previous log |
| [v0.1.9](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.9) | 11 Nov 2026 | Installer: fits high-DPI screens, unpacks RPCS3 on Windows 10, keeps a log; US release confirmed |
| [v0.1.8](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.8) | 10 Nov 2026 | Builds find and redo damaged compiles |
| [v0.1.7](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.7) | 10 Nov 2026 | Builds retry with fewer jobs instead of stopping; Install section for the one-click installer |
| [v0.1.6](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.6) | 10 Nov 2026 | Ultrawide: the game's half that v0.1.5 left out |
| [v0.1.5](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.5) | 10 Nov 2026 | Ultrawide monitors: a wider view, HUD kept 16:9 in the middle |
| [v0.1.4](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.4) | 10 Nov 2026 | Black screen on start fixed (player builds compile the SPU code) |
| [v0.1.3](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.3) | 10 Nov 2026 | First try at the black-screen fix (didn't build; v0.1.4 did) |
| [v0.1.2](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.2) | 10 Nov 2026 | Steering wheels and force feedback (untested on real wheels) |
| [v0.1.1](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.1) | 10 Nov 2026 | One-command setup that checks your files and builds the game |
| [v0.1.0](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.0) | 10 Nov 2026 | Numbered versions and one-command updates (`update.ps1`) |
| (before) | 1-9 Nov 2026 | From first boot to playable: see [CHANGELOG.md](CHANGELOG.md) |

To update: the launcher's **Updates** page, or double-click `Update.bat` in your install folder (see [Updating](README.md#updating)). To go back to one of these: `update.ps1 -Version <version>`.
