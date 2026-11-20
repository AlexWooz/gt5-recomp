# 📋 Current state

*Last updated: 18 November 2026*

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
  race at 60 fps (found and tested by a contributor), and the European 2.17 reaches a 16-car Arcade
  race too (README, Game updates)
- DLC, new: with US 2.17, the gift cars arrive in the garage and every pack (Racing Car Pack, Car
  Packs 2 and 3, the 2012 car packs, Course Pack, Speed Test, Twin Ring Motegi) installs from
  Downloadable Content, once the DLC and its licence keys are in `GT5\hdd0` (README, DLC)
- One-click install: `GT5-Installer.bat` (README, Install)
- Ultrawide monitors (21:9, 32:9), new: the game draws a wider view itself and keeps the HUD and
  menus 16:9 in the middle (checked in 2560×1080, 3440×1440 and 3840×1080 windows, not yet on a real
  ultrawide monitor: [tell us how it looks](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=1-tried-it.yml))

## 🔧 Being worked on

- **Vulkan on Windows**: built in at install; pick it under Display > Renderer. It has the in-game menu, fills the screen and
  has the Shadows setting, but not yet Resolution, DLSS or the AMD / NVIDIA frame generators, and it is slower than DirectX 12.
- **Linux, new and early** (a native build: Vulkan, SDL2, `setup.sh`; README, Linux). Its first test
  ran on Ubuntu 24.04 with no graphics card (software Vulkan): menus, GT Mode and Arcade up to the
  pre-race screen work, the race itself stayed black there. Not yet tried on a real Linux PC with a
  graphics card, a Steam Deck, or under Proton/Wine:
  [tell us how it goes](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=1-tried-it.yml).
- Steering wheels with force feedback: new in v0.1.2. Your PC wheel appears to the game as a Logitech
  G27; without a wheel it was checked that the game accepts it, a race steers from it, and the game's
  force-feedback commands arrive. The first real wheel, a player's Genius Speed Wheel 3 Vibration (no
  force feedback, both pedals on one axis), attaches and gets the game's commands in a race
  ([#29](https://github.com/MormonFTP-hub/gt5-recomp/pull/29)); since 13 Nov it's found by itself and
  its pedal axis splits into throttle and brake: on v0.1.39 the player raced a full 16-car field with it,
  using the keyboard for the menus. Since 15 Nov a wheel with no d-pad moves through the menus itself
  (rim for left / right, throttle and brake for down / up), which works on that wheel now, and since
  v0.1.59 the steering and pedals no longer stay stuck in a race when a busy PC falls behind (checked
  with a simulated wheel). The second real wheel, a player's Thrustmaster T-GT, has force feedback that
  works and pulls the right way; since v0.1.69 Thrustmaster wheels get their own pedal layout (normal and
  combined pedal mode), a button can be a pedal, and a button can be reverse (manual gearbox only). Its
  button numbers aren't known yet. Wheel owners:
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
  time per frame on a fast core, 160 ms on 4 efficient cores), and threads that spin while they wait.
  Lifted SPU code keeps its registers in CPU registers since 13 Nov, and GT5's SPURS jobs take 20% less
  CPU. Since v0.1.41 the graphics threads sleep instead of spinning on CPUs with 8 threads or fewer:
  on 4 cores that's 20% less CPU at the same frame rate, and +25% frames when the CPU is shared with
  other programs. On 4 small cores the sound mixer, one thread GT5 can't split, ran flat out and short
  of real time; since v0.1.43 it takes 22% less time (all SPU jobs 14% less CPU). A player's
  Ryzen 5 3600 showed that drawing fewer frames doesn't help there: the launcher's 30 fps setting
  halved the frames drawn and left the game's speed unchanged, so it's no longer picked by itself.
  Laptops now draw on their dedicated graphics card (v0.1.20).
- Shorter shader-compile stutter the first time a scene is seen

## ❌ Not supported yet

- Ultrawide on releases other than the European disc 01.00: they keep the 16:9 picture for now
- Game updates other than 2.01, 2.17 (US, Europe) and 2.11 with the GT5 Master Mod (US, a player's
  report: menus, the mod's debug menu, an A-Spec race) are untried. Each can change the game's SPU programs again; setup's
  `SPU jobs found in EBOOT.elf: N of 15` says whether yours are all known

## ⚠️ Known issues

- The first visit to a scene stutters while its shaders compile; later visits load them from the cache.
  New shaders compile on up to 8 threads since 13 Nov, which made those stutters about three times
  shorter (a race start with about 340 new shaders: 3.4 s instead of 12.2 s).
- Online features don't work.
- Some graphics glitches remain. If a track looks wrong, please
  [report it](https://github.com/MormonFTP-hub/gt5-recomp/issues/new/choose) with the track, its time
  and weather, and a screenshot. (Textures showing another texture's picture, like banners across the
  road at Chamonix, are fixed since v0.1.16: [#14](https://github.com/MormonFTP-hub/gt5-recomp/issues/14).)
- The mouse change (Steam's controller mouse is ignored, the real mouse stays free) hasn't been
  tried with a physical controller yet.

## 📰 Recent changes

The last few days; the full history since 1 November is in [CHANGELOG.md](CHANGELOG.md).

- **20 Nov 2026:** shaders no longer stall the game: they are prepared before the game starts and built in the background on all cores, and with Asynchronous shader loading (on by default, DirectX 12) an object whose shader is not ready yet is drawn at once by a general stand-in (a first race on a PC that has never played: about 90 draws missing for a moment, where about 2,500 were, and 0.3 s of waiting in all); frame generation makes frames on every screen (menus, replays, pause); the controller buttons are the game's own again by default, with the triggers layout as a choice; Button symbols: Xbox buttons or your keyboard keys in the game's menus (a choice in Controls); the launcher offers Save a report after a crash or freeze (logs with your user name and folders taken out); Frame rate 60 or 30; a faster start with a big shader cache (v0.1.109).
- **18 Nov 2026:** the picture fills the screen; sharper shadows (new Shadows setting) and textures; DLSS / DLAA on every 3D screen; controller triggers are throttle and brake; Esc asks before quitting, F1 pauses the race; sound follows the Windows output device, with Volume; the window starts borderless and is sharp at any Windows scaling; Vulkan gets the in-game menu and is set up at install; a resizable launcher and an installer that checks the PC first (v0.1.108).
- **17 Nov 2026:** NVIDIA DLSS / DLAA anti-aliasing (optional, fetched from NVIDIA by the launcher; DLAA costs 1-2 fps on an RTX 4070 Ti SUPER), frame generation presented evenly and a new generator that uses the game's real motion, menu rows that grey out when they don't apply, F8 snapshot for bug reports (v0.1.101, DirectX 12, no re-lift); in-game menu: cockpit view, mouse steering and force feedback settings apply while playing (v0.1.100, no re-lift); render resolution 1080p to 5K whatever the game's mode (4K at 57-60 fps in a race on an RTX 4070 Ti SUPER) and a settings menu inside the game on F1 or Select + Start, most settings applied while playing (v0.1.99, DirectX 12, no re-lift); frame rate multiplier: 2x/3x/4x frame generation on screens faster than 60 Hz, about 118 frames a second on a 165 Hz screen with the game at 57 (v0.1.98, DirectX 12, no re-lift); fix for new cars no longer being drawn after a long session through the dealerships (DirectX 12 pipeline table full), and a picture check that every release now has to pass (v0.1.97, no re-lift); FIX for cars missing body panels since v0.1.92 (a change to the SPU translation; switched off), and the waiting threads sleep on every processor: about a fifth less CPU on big ones (v0.1.96, no re-lift); the SPU workers are woken one at a time instead of all together: 6% less CPU for the whole game, +4% frames a second where the CPU is the limit (v0.1.95, no re-lift); the native grid pass and pack pass of v0.1.93 did not run in the game (they stood aside while SPU interrupts were enabled, which is always); fixed and counted in a race, SPU workers about 3% less CPU (v0.1.94, no re-lift); native code for the busiest geometry functions (grid pass 4 times faster alone) and a cheaper SPU-to-runtime path: SPU workers 3.6% less CPU (v0.1.93, no re-lift); the SPU translator writes faster code (registers kept whole, the +0 rule off the float chains) and the reverb and the EQ's FFT are native: sound jobs 17% less time, same output (v0.1.92, one re-lift)
- **16 Nov 2026:** the first native code in place of translated PS3 code: the sound mixer's inner loops, 44% less time for the mixer job, same output bit for bit (v0.1.91, no re-lift); a real-speed 30 fps game mode (mod `half-rate`; frame generation fills it back to 60): roughly a third less CPU (v0.1.90, one re-lift); the game's vector and float maths run as whole vectors and byte shuffles, as RPCS3 does them: the main game thread does 17% less work per frame, same results (v0.1.89, one re-lift: the update rebuilds the game code); mouse steering: left click = OK, right click = back and the wheel moves up/down at the menus; keyboard: an arrow-key tap moves one place in the menus (it moved two), the keyboard works with a pad connected, and mouse steering speed goes down to 1% in steps of 0.5 (launcher number box); a `[threads]` line in `logs\play.err` every 5 s says how busy each kind of thread is (`PS3_THREAD_STATS=0` turns it off);
  DirectX 12: the drawing threads hand work over with less cache traffic (-20% per call); Vulkan: the start-up shader list keeps the most recently used shaders, and a full list no longer
  freezes the first race start for ~11 s; Vulkan: new scenes no longer stay half-drawn for a minute on slow PCs (each shader built once,
  builders above normal priority, what's on screen first) and a minimised window comes back (v0.1.80); texture dumping and HD texture packs on DirectX 12: `PS3_TEX_DUMP=1` (or the launcher's Display page)
  writes the game's textures to `GT5\textures\dump`, and files named by the same hash in `GT5\textures\replace` are used
  instead at any size, PNG or DDS ([MODDING.md](MODDING.md#2-textures-and-hd-texture-packs)) (v0.1.78); Vulkan: no more washed-out (or too dark) races, as the exposure is measured from the right surface;
  readbacks without a wait and shaders built ahead at start-up are on by default on Windows (v0.1.77); Linux: your own keyboard keys (`PS3_KEYS` in `GT5/settings.sh`) and the small-CPU setting up to
  12 threads, as on Windows (v0.1.76); a wider cockpit view and the seat moved forward, back, up or down (launcher Display > Cockpit
  view, off by default; only the view from the driver's seat changes) (v0.1.75); your own keyboard keys: every pad button can take the keys you choose, on the launcher's Controls
  page (labelled with what each button does in GT5) or with `PS3_KEYS` (v0.1.74); Vulkan: frame generation works there too (57 new pictures a second instead of 26 at Speed 30 fps),
  presents are evenly spaced on a thread of their own, and its command thread is cheaper (+17% game fps in a test
  race) (v0.1.73); mouse steering (launcher Controls, off by default): in races the mouse turns the wheel, left button
  accelerates, right brakes, with a speed setting and optional self-centring; menus and pauses leave the mouse alone (v0.1.72).
- **15 Nov 2026:** Vulkan draws at 1080p in the 1080p mode, and its window title shows the frame rate and resolution
  (v0.1.71); **Update now** opens an update window with a progress bar over your Photo Mode pictures, the full text one
  click away and saved to `GT5\logs\update.log`, and a crash report names the DLL a crash happened in (v0.1.70); the
  launcher keeps lines of your own in `settings.bat` (v0.1.67); steering wheels: Thrustmaster pedals the right way round in normal and combined mode, a button can
  be a pedal (`brake=4`), and a button can be reverse with the manual gearbox (v0.1.69); sound on busy and small CPUs: no more 85 ms holes, and the sound no longer runs up to 80 ms late
  (the 2.11 "audio lag", v0.1.68); the check that an SPU job's code is unchanged costs half as much, a little less CPU for the
  sound on small CPUs (v0.1.66); on 6-core/12-thread CPUs the game's waiting threads sleep instead of spinning, as on
  8-thread ones: 5-14% less CPU at the same frame rate (v0.1.65); Linux and the Vulkan renderer: races no longer start far too dark, and car reflections and
  shadows are drawn in full (v0.1.64); less CPU for every frame on small CPUs and the Steam Deck, from AlexWooz's pull request #33,
  and the sound mixer's filters a little cheaper (v0.1.63, one re-lift); in the cockpit view, dust and smoke from the cars ahead stay outside instead of covering
  the dashboard (v0.1.62); a steering wheel's steering and pedals no longer get stuck in a race when the PC is too
  busy for the game to keep up (v0.1.59); the sound chain needs less CPU (its EQ module a third less), for the audio lag on small
  CPUs (v0.1.57); renderer choice in the launcher (Display > Renderer): DirectX 12, the default, or Vulkan,
  experimental and slower on Windows for now, added to `gt5.exe` with a one-time build (v0.1.56);
  Esc asks before it quits the game (v0.1.55); frame generation (beta, launcher Display): 30 fps and watch mode look like 60, frames made
  on the graphics card, presented on a thread of its own so the game keeps its speed (v0.1.54);
  saves over 2 MB (`GT5.0` + `GT5.1`, a big garage on 2.xx) load again instead of the
  game closing at every start; a steering wheel with no d-pad moves through the menus: the rim for left / right,
  throttle and brake for down / up, and spare buttons can be the d-pad (v0.1.53).
- **14 Nov 2026:** Photo Mode: Auto Focus no longer fills the viewfinder with ghost copies, and photos after the first keep the right exposure, and new cars get a proper Garage picture instead of a black square (v0.1.51); Linux: the game window opens on KDE Plasma under Wayland (v0.1.50); no more freezes when new shaders are needed (they build in the background), and
  road markings and shadows get the PS3's polygon offset (v0.1.49); Photo Mode pictures keep their colours: no more yellow-green tint or coloured ghost copies (v0.1.48);
  the game starts on PCs where Windows forces Control Flow Guard on, and Options >
  Display no longer crashes it (v0.1.47); races at 1080p get the right brightness: auto-exposure read a leftover image, so
  tracks could come out blown-out white (Speed Test) or far too dark (v0.1.46); game updates and DLC
  install from their `.pkg` files (launcher, Help), through RPCS3 (v0.1.45); the launcher says why the game stopped when Windows recorded it (v0.1.44); SPU code leaner: the sound mixer 22% faster, all SPU jobs 14% less CPU (one re-lift)
  (v0.1.43).
- **13 Nov 2026:** Linux and the Steam Deck much quicker (menus ~17 → ~25-27 fps, race frames ~180 →
  ~110 ms on a Deck) and the PS3's vector multiply-add four lanes at once on every PC, from a community
  pull request ([#30](https://github.com/MormonFTP-hub/gt5-recomp/pull/30)); a steering wheel that stops
  answering no longer keeps driving the car (v0.1.42); on CPUs with 8 threads or fewer, the graphics threads sleep instead of spinning
  while they wait: on 4 cores, 20% less CPU at the same frame rate, and +25% frames when the CPU is
  shared (v0.1.41); car tail lights and headlights show their lamps again (they were a pale mirror of the
  sky); with a game update (2.xx), the cockpit view's windows are clear again (they showed a
  blown-out, speckled copy of the screen with a second HUD); the first real steering wheel tried (a Genius Speed Wheel 3): it's now found by
  itself, and wheels with both pedals on one axis get throttle and brake (before, resting was half
  throttle); the stutter the first time you see a car or track is about three times shorter: new
  shaders compile on up to 8 threads (from a community pull request) (v0.1.32); DLC works offline with
  your own licences (v0.1.31); game update 2.17, the last one, runs on the US and European releases (from a
  community pull request; one re-lift) (v0.1.30); SPU code runs leaner: SPURS jobs take 20% less CPU, about 7% more frames on a
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
