# 📋 Current state

*Last updated: 9 November 2026*

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

## 🔧 Being worked on

- More speed in busy race scenes (the graphics thread is the limit)
- The US release (BCUS98114) is supported by the tools but nobody has tried it yet:
  [tell us if you do](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=1-tried-it.yml)

## ⚠️ Known issues

- The first visit to a scene stutters while its shaders compile; later visits load them from the cache.
- Online features don't work.
- Some graphics glitches remain.
- The mouse change (Steam's controller mouse is ignored, the real mouse stays free) hasn't been
  tried with a physical controller yet.

## 📰 Recent changes

The last few days; the full history since 1 November is in [CHANGELOG.md](CHANGELOG.md).

- **9 Nov 2026:** each frame shown once (fixes flicker); the mouse is no longer held inside the
  window; optional watch mode (F9); tools for other releases (US untested); feedback forms,
  modding guide and game-code mods.
- **7 Nov 2026:** the graphics thread processes the game's commands about twice as fast, and each
  draw costs about 17% less CPU time.
- **6 Nov 2026:** graphics commands recorded on a second thread; quicker hand-off between the PS3's
  main processor and its SPUs.

---

Tried it? Say how it went in the [discussion](https://github.com/MormonFTP-hub/gt5-recomp/discussions)
or on Discord (**mormonftp**).
