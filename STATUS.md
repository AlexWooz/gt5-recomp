# 📋 Current state

*Last updated: 12 November 2026*

## 🟢 Playable

Gran Turismo 5 boots, plays and saves on PC. Races run at roughly 45 to 58 frames per second on a
fast desktop, and most modes of the game work. It's still a work in
progress: see the known issues below, and please [report what you find](https://github.com/MormonFTP-hub/gt5-recomp/issues/new/choose).

## 🏎️ Tested rig

| Part | |
|---|---|
| CPU | Intel Core i9-14900KF (8 performance + 16 efficient cores, 32 threads) |
| Graphics | NVIDIA GeForce RTX 4070 Ti SUPER, 16 GB (driver 616.56) |
| Memory | 32 GB DDR5-6000 |
| Storage | 1 TB NVMe SSD (the game and its shader cache live here) |
| System | Windows 11 Home (build 26200) |
| Monitor | 2560×1440 at 164 Hz; the game draws its 1080p mode (races at 1728×972), scaled to the window |
| Controller | DualSense over Bluetooth |
| Game | Europe **BCES00569**, disc executable 01.00, no update |
| Port settings | `play.bat` as shipped (live D3D12 renderer, 2 extra SPU threads, pad works in the background) |

## 📊 Results on that rig

From a 10-minute session on 9 November 2026:

| | Frames per second |
|---|---|
| Races | 45 to 58 (median 47) |

The FPS counter in the window title read up to about 120 in that session, because it was counting
every frame twice; that was fixed the same day, and the numbers above are the game's own frames.
Speed depends mostly on the CPU: the graphics work runs on one thread, so fast single-core
performance matters more than the graphics card.

## ✅ Works

- A-Spec and B-Spec races, licence tests, special events, replays
- Photo Mode (photos save as JPEG), Course Maker, GT TV (movies with sound), Music Library,
  Photo Album, Garage, Driver List, Parts Shop, Manual
- Buying cars and saving, on-screen keyboard (a Windows text box)
- DualSense, DualShock 4, Xbox/XInput pads and keyboard
- Watch mode for B-Spec and replays (F9): half the drawing work, full race speed
- The US release (BCUS98114) too: players have raced on it (an i5-1135G7 laptop with Iris Xe graphics,
  and a GTX 1650 laptop), with frame rates to match those machines. Thanks for the reports!
- The Hong Kong / Asian release (BCAS20108), new: a player ran it on v0.1.18, with the launcher and the
  game's own text in Chinese ([discussion #18](https://github.com/MormonFTP-hub/gt5-recomp/discussions/18))
- Game updates (2.xx), new: the Asian release's update 2.01 boots, loads a long-played career save and
  races in Arcade Mode. It needs the update's data as well as its executable (README, Game updates).
  Frame rates on it aren't measured yet
- Update 2.17 (the last), new: the US release's 2.17 runs menus, GT Mode and a full 16-car Arcade
  race at 60 fps (README, Game updates)
- One-click install: `GT5-Installer.bat` (README, Install)
- Ultrawide monitors (21:9, 32:9), new: the game draws a wider view itself and keeps the HUD and
  menus 16:9 in the middle (checked in 2560×1080, 3440×1440 and 3840×1080 windows, not yet on a real
  ultrawide monitor: [tell us how it looks](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=1-tried-it.yml))

## 🔧 Being worked on

- **Linux, new and early** (a native build: Vulkan, SDL2, `setup.sh`; README, Linux). Its first test
  ran on Ubuntu 24.04 with no graphics card (software Vulkan): menus, GT Mode and Arcade up to the
  pre-race screen work, the race itself stayed black there. Not yet tried on a real Linux PC with a
  graphics card, a Steam Deck, or under Proton/Wine:
  [tell us how it goes](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=1-tried-it.yml).
- Steering wheels with force feedback: new in v0.1.2 and **not yet tried on a real wheel**. Your PC
  wheel appears to the game as a Logitech G27; without a wheel it was checked that the game accepts
  it, a race steers from it, and the game's force-feedback commands arrive. Wheel owners:
  [tell us how it goes](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=1-tried-it.yml),
  with `GT5\logs\play.err` attached (settings are in the README's FAQ).
- **The picture can freeze on one screen at the first start** while the game carries on behind it
  (the menu sounds still react to the controller). Two players have seen it; restarting the game got
  past it. Being looked into ([#10](https://github.com/MormonFTP-hub/gt5-recomp/issues/10)). One
  sure cause, **Alt+Enter** (found by a player), is fixed since v0.1.17: Alt+Enter now does nothing,
  and the launcher's borderless full screen fills the screen instead. A second one, found in testing:
  under heavy CPU load the opening movie could stop on one frame for good; since v0.1.21 it's skipped
  (GT TV still has it). The last possible cause is the graphics driver resetting or refusing a frame.
  **If it still happens to you:** attach that run's log to #10: `GT5\logs\play.err`, or
  `GT5\logs\play.prev.err` once you've started the game again (since v0.1.10 each start keeps the
  previous log); a line starting with `[d3d-fail]` says what failed.
- More speed on 4-6 core CPUs and laptops. On 6 of the tested rig's efficient cores, with nothing else
  running, races hold 49-55 game frames a second (an earlier "about 22" was measured while another game
  shared the graphics card). Laptops do much worse: a tester's i5-1135G7 races at about 13, with its CPU
  at its power limit. The work now is cutting the CPU the game needs per frame (about 100 ms of CPU
  time per frame today, spread over its threads), and threads that spin while they wait; lifted SPU
  code keeps its registers in CPU registers since 13 Nov, and GT5's SPURS jobs take 20% less CPU. A player's
  Ryzen 5 3600 showed that drawing fewer frames doesn't help there: the launcher's 30 fps setting
  halved the frames drawn and left the game's speed unchanged, so it's no longer picked by itself.
  Laptops now draw on their dedicated graphics card (v0.1.20).
- Shorter shader-compile stutter the first time a scene is seen

## ❌ Not supported yet

- Ultrawide on releases other than the European disc 01.00: they keep the 16:9 picture for now
- Game updates other than 2.01 and US 2.17 are untried: 2.11 (what the GT5 Master Mod needs) and
  other regions' updates. Each can change the game's SPU programs again; setup's
  `SPU jobs found in EBOOT.elf: N of 15` says whether yours are all known

## ⚠️ Known issues

- The first visit to a scene stutters while its shaders compile; later visits load them from the cache.
- Online features don't work.
- Some graphics glitches remain. If a track looks wrong, please
  [report it](https://github.com/MormonFTP-hub/gt5-recomp/issues/new/choose) with the track, its time
  and weather, and a screenshot. (Textures showing another texture's picture, like banners across the
  road at Chamonix, are fixed since v0.1.16: [#14](https://github.com/MormonFTP-hub/gt5-recomp/issues/14).)
- The mouse change (Steam's controller mouse is ignored, the real mouse stays free) hasn't been
  tried with a physical controller yet.

## 📰 Recent changes

The last few days; the full history since 1 November is in [CHANGELOG.md](CHANGELOG.md).

- **13 Nov 2026:** SPU code runs leaner: SPURS jobs take 20% less CPU, about 7% more frames on a
  4-core CPU (one re-lift, done by `update.ps1`); about 10% faster on small CPUs and laptops before
  that: waits that no longer spin a core, plus four speed-ups from the upstream ps3recomp project
  (v0.1.28).
- **12 Nov 2026:** a native Linux build, new and early (`setup.sh`, `GT5/play.sh`); the opening movie is skipped, so it can't freeze the first start (v0.1.21);
  laptops draw on their dedicated graphics card, and the launcher has a **Speed**
  setting (30 fps, for a graphics card at its limit) (v0.1.20); a black screen from a split or missing `GT.VOL` is
  fixed or explained (v0.1.19); the Hong Kong release runs; wrong track textures fixed (banners across the road at Chamonix, scrambled Eiger):
  the game's graphics-memory copies now happen (v0.1.16); Alt+Enter no longer freezes the picture
  (v0.1.17); the launcher: every setting in one window (resolution, borderless full screen, wheel,
  language, mods), Play, one-click updates and a log saver, in English or Chinese; double-click
  `Update.bat`; updates no longer stop with "Access is denied"; the version in the window title.
- **11 Nov 2026:** 1080p: the game uses its own 1080p mode (races at 1728×972 instead of 1152×648, same
  speed on the tested rig; `PS3_VIDEO_MODE=720` for slow graphics chips); v0.1.10's window showed only
  part of that picture, fixed in v0.1.12; the previous run's log is kept as `play.prev.err`; the installer fits high-DPI (4K) screens, unpacks RPCS3 on Windows 10 too,
  and keeps a log; the US release confirmed working by players.
- **10 Nov 2026:** one-click installer (`GT5-Installer.bat`); ultrawide monitors (21:9, 32:9): a wider view, HUD kept 16:9 in the middle;
  black screen on start fixed for everyone building from GitHub (the SPU code is
  now compiled; run `update.ps1`); steering wheels and force feedback (v0.1.2, untested on real wheels: owners
  wanted); one-command setup that checks your files and builds the game (`setup.ps1`); numbered
  versions and one-command updates (`update.ps1`); FAQ at the top of the README.
- **9 Nov 2026:** noisy distant road and track textures fixed (Tsukuba); each frame shown once
  (fixes flicker); the mouse is no longer held inside the window; optional watch mode (F9); tools
  for other releases (US untested); feedback forms, modding guide and game-code mods.
- **7 Nov 2026:** the graphics thread processes the game's commands about twice as fast, and each
  draw costs about 17% less CPU time.
- **6 Nov 2026:** graphics commands recorded on a second thread; quicker hand-off between the PS3's
  main processor and its SPUs.

---

Tried it? Say how it went in the [discussion](https://github.com/MormonFTP-hub/gt5-recomp/discussions)
or in the [Workshop Discord](https://discord.gg/9cVPS7r5Ha) (#gt5-test-reports).
