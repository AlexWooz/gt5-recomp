# 📋 Current state

*Last updated: 11 November 2026*

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
| Monitor | 2560×1440 at 164 Hz; the game renders its 1080p mode (2048×1080) |
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
- One-click install: `GT5-Installer.bat` (README, Install)
- Ultrawide monitors (21:9, 32:9), new: the game draws a wider view itself and keeps the HUD and
  menus 16:9 in the middle (checked in 2560×1080, 3440×1440 and 3840×1080 windows, not yet on a real
  ultrawide monitor: [tell us how it looks](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=1-tried-it.yml))

## 🔧 Being worked on

- Steering wheels with force feedback: new in v0.1.2 and **not yet tried on a real wheel**. Your PC
  wheel appears to the game as a Logitech G27; without a wheel it was checked that the game accepts
  it, a race steers from it, and the game's force-feedback commands arrive. Wheel owners:
  [tell us how it goes](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=1-tried-it.yml),
  with `GT5\logs\play.err` attached (settings are in the README's FAQ).
- **The picture can freeze on one screen at the first start** while the game carries on behind it
  (the menu sounds still react to the controller). Two players have seen it; restarting the game got
  past it. Being looked into ([#10](https://github.com/MormonFTP-hub/gt5-recomp/issues/10)). The
  likely cause is the graphics driver resetting or refusing a frame, after which the port stops
  drawing. **If it happens to you:** close the game and attach `GT5\logs\play.err` to #10 *before* you
  start it again (each start overwrites that file); a line starting with `[d3d-fail]` says what failed.
- More speed in busy race scenes (the graphics thread is the limit), and on laptops and integrated
  graphics
- Shorter shader-compile stutter the first time a scene is seen

## ❌ Not supported yet

- Ultrawide on releases other than the European disc 01.00: they keep the 16:9 picture for now
- Executables from game updates (2.xx), so mods that need one, like the GT5 Master Mod (2.11), don't
  work yet

## ⚠️ Known issues

- The first visit to a scene stutters while its shaders compile; later visits load them from the cache.
- Online features don't work.
- Some graphics glitches remain.
- The mouse change (Steam's controller mouse is ignored, the real mouse stays free) hasn't been
  tried with a physical controller yet.

## 📰 Recent changes

The last few days; the full history since 1 November is in [CHANGELOG.md](CHANGELOG.md).

- **11 Nov 2026:** the installer fits high-DPI (4K) screens, unpacks RPCS3 on Windows 10 too, and
  keeps a log; the US release confirmed working by players.
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
or on Discord (**mormonftp**).
