# 📜 Changelog

Everything done on the port, newest first. All dates are 2026. Frame rates are from the European
release on the tested rig in [STATUS.md](STATUS.md) unless a day says otherwise.

> Frame rates before **9 Nov** were read from the window's FPS counter, which at times counted empty
> or repeated frames (fixed on 4 Nov and 9 Nov), so treat them as rough before-and-after comparisons.
> From 9 Nov on, numbers are the game's own frames.

## 11 Nov 2026

🏷️ Released as **v0.1.10**

🖥️ **1080p** (after a report from a 4K TV, [#14](https://github.com/MormonFTP-hub/gt5-recomp/issues/14))
- The port always told the game it was on a 720p TV, so everyone got GT5's 720p mode: races drawn at
  1152×648, inside the game's own black border. It now offers the 1080p mode, as a PS3 on a 1080p TV
  does, and the game switches to it by itself: races are drawn at 1728×972 (2.25 times the pixels), the
  HUD and menus at 1920×1080. `play.bat` sets `PS3_VIDEO_MODE=1080`; `720` brings back the 720p mode for a
  slow graphics chip.
- Tested on the tested rig in an Arcade race: 32 to 42 fps at 1080p against 32 to 43 fps at 720p in the
  same race, so on a desktop graphics card it costs nothing (the CPU is the limit). The intro, menus,
  car select and the race all ran at 1080p. In a 21:9 window (2560×1080) the game draws the wider view
  into 1920×1080 with the HUD a 16:9 block in the middle, and the window spreads it sideways.
- The black border around races is the game's own (its display margin, kept for TVs that cut off the
  edges), not the port's.

📝 **Logs survive a restart**
- `play.bat` now keeps the previous run's log as `GT5\logs\play.prev.err`, so the log of a frozen picture
  ([#10](https://github.com/MormonFTP-hub/gt5-recomp/issues/10)) can still be sent after restarting.

🏷️ Released as **v0.1.9**

🧰 **Installer fixes from the first reports**
- High-DPI screens: on a 4K monitor with Windows scaling the installer's window was squashed, with text
  over text and cut off ([#11](https://github.com/MormonFTP-hub/gt5-recomp/issues/11)). The layout now
  scales with the screen; checked at 100%, 150% and 200%.
- Windows 10: its built-in `tar` can't open RPCS3's `.7z` download ("LZMA codec is unsupported",
  [#8](https://github.com/MormonFTP-hub/gt5-recomp/issues/8)). The installer now falls back to 7-Zip if
  it's installed, or downloads 7-Zip's own small extractor (`7zr.exe`, from 7-zip.org). Checked with
  `tar` switched off.
- The installer writes everything it shows to `GT5-Installer.log` next to itself, and asks for that log
  when something stops it.

🏁 **US release confirmed**
- Players have raced on the US disc (BCUS98114), so it's listed as working, and setup recognises its
  executable (SHA-1 40126D2D14B0A95B9601BB11E3A6E778186067AC). Its release-specific addresses turned
  out the same as the European disc's.
- README FAQ: other disc versions (1.02) and game updates (2.xx, needed by the GT5 Master Mod).
- Known issue added: the picture can freeze on one screen at the first start while the game carries
  on ([#10](https://github.com/MormonFTP-hub/gt5-recomp/issues/10)). Being looked into.
- That freeze didn't happen in a test of a brand-new install on the tested rig (empty save, empty shader
  cache, through the first-start screens, the intro movie, the menu and driver creation; only 66
  pipelines were needed, so it isn't shader compiling). The renderer does stop for good when the
  graphics driver fails a present or a command list, logging `[d3d-fail]`, which fits both reports (an
  AMD card and a switchable-graphics laptop). README FAQ: how to send that log before restarting.

## 10 Nov 2026

🏷️ Released as **v0.1.8**

🧰 **Damaged compiles are found and redone**
- The same installer test then failed to link: two SPU job objects compiled during that crash were
  written damaged (the linker found calls to the compiler's internal `llvm.lifetime` markers; the same
  source compiles cleanly otherwise). Long, heavy parallel builds do this now and then, especially on
  some 13th/14th-gen Intel CPUs. The build's last step is now `scripts\build_run.ps1`: when the build
  stops, it deletes any object the linker reports as damaged and tries again (up to four times, halving
  the parallel jobs each time). The whole output also goes to `GT5\build\last_build.log`.
- Checked on that broken install: two passes each found and removed one damaged object, and the third
  linked `gt5.exe`.

🏷️ Released as **v0.1.7**

🧰 **A crashed compile no longer stops the build**
- In an installer test with other builds running at the same time, the compiler crashed on one of
  the biggest generated files (an SPU job), and setup stopped. `build.cmd` now tries again with half
  the parallel jobs, carrying on from where it stopped.
- README: an **Install** section at the top, for the one-click installer.

🏷️ Released as **v0.1.6**

- v0.1.5 left out the game's half of ultrawide (`GT5\src\gt5_widescreen.cpp`), so on an ultrawide
  monitor it still drew 16:9. This release adds it. Updated to v0.1.5? Run `update.ps1` again.

🏷️ Released as **v0.1.5**

🖥️ **Ultrawide monitors (21:9, 32:9)** (new)
- On a screen wider than 16:9 the game now draws a wider picture itself: race and replay cameras see
  more at the sides (nothing is stretched), the 3D scenes behind the menus widen too, and the HUD and
  menus stay a 16:9 block in the middle. It's GT5's own way of fitting a TV of another shape: it takes
  the shape of the picture from two constants it chooses by the PS3's TV setting, and the port now sets
  them to the shape of your window (`GT5\src\gt5_widescreen.cpp`, European disc 01.00).
- The game also renders every pixel of the wider picture: the port reports a wider 720p mode (about
  1700×720 for 21:9, at most 1920×720), the game sizes its frame from it, and the window shows that
  frame. Shapes wider than 8:3 (32:9) are drawn at 1920×720 and spread sideways to fill the window.
- `play.bat` sets `PS3_ASPECT=auto`, the shape of your monitor; on a 16:9 monitor nothing changes.
  `PS3_ASPECT=21:9` (or `32:9`, `3440x1440`) picks a shape, `PS3_ASPECT=16:9` keeps the PS3's
  picture, and `PS3_WINDOW_SIZE=2560x1080` opens the window at an exact size. On another release the
  picture stays 16:9 for now (`logs\play.err` says so in a `[widescreen]` line).
- Checked without an ultrawide monitor, in 2560×1080, 3440×1440 and 3840×1080 windows: an Arcade race
  with its HUD, the menus on the way there (car select, colour, options), and the attract demo.
- Runtime: the shape is decided in one place (`display_aspect.c`), cellVideoOut reports the wider
  mode, the window opens at the shape, a title's own code can change data before the game starts
  (`ps3_on_loaded`), and `PS3_POKE32=<address>=<value>` tries such a change without building anything.

🧰 **One-click installer** (after a player asked for one)
- `GT5-Installer.bat`, attached to every release (README, Install): double-click it, pick your game
  (a disc-dump folder or an `.iso`) and Sony's `PS3UPDAT.PUP`, press Install. It installs Git, Python
  and Microsoft's C++ build tools if they're missing (winget), downloads the code, downloads RPCS3 and
  uses it only to unlock the game's EBOOT.BIN and install the PS3 system software (RPCS3 holds keys
  that can't be published here; the game never runs in it), copies the game in, runs setup, and puts a
  **Gran Turismo 5 (PC)** shortcut with the game's own icon on the desktop. Running it again updates
  an install, or carries on after a stop, skipping what's done.
- Checked on the European disc image: the window recognises the game from the `.iso`, the system
  software installs (the same 1,075 files as a hand install), and the unlocked EBOOT.elf is identical
  to the hand-made one.
- `update.ps1` now only waits for this install's own game to close, not for any `gt5.exe` running.

🏷️ Released as **v0.1.4**

- v0.1.3 didn't build: it picked up one file of the unfinished ultrawide work without the rest of it.
  v0.1.4 is v0.1.3 without that file. Updated to v0.1.3? Run `update.ps1` again.

🏷️ Released as **v0.1.3**

🛠️ **Black screen on start fixed** (every build made from GitHub until now)
- The game stayed black with `draws: 0` in the window title. Setup never compiled the game's SPU code
  (the PS3's co-processors, which do the physics, sound and graphics set-up), and the port can't run a
  SPU job that wasn't compiled: it drops it, and the game waits for it forever. Only the developer's
  own build had that code, so the problem never showed there.
- `relift.ps1` now builds it too: the SPU programs and all 43 SPU jobs, found in your own EBOOT.elf
  (`scripts\spu_jobs_from_eboot.py` looks for each job by its hashes; no game data is in the repo). Its
  output says `SPU jobs found in EBOOT.elf: 15 of 15` for the European release.
- Checked with a fresh install straight from GitHub on the European release: it now boots and draws,
  and no SPU job is dropped. The US release gets every job whose code is the same as in the European
  one; the count above says how many.
- **Already installed?** Run `update.ps1` (or double-click `setup.bat`): it re-generates the code and
  rebuilds.
- `setup.bat`: double-click to run setup without typing commands; the window stays open at the end.

🏷️ Released as **v0.1.2**

🎮 **Steering wheels and force feedback** (new, not yet tried on a real wheel: owners wanted)
- Your PC wheel now appears to the game as a Logitech G27, the wheel GT5 drives itself over USB on a
  PS3: 900°, three pedals, paddles and an H-shifter. The port plays the USB side (the game's own wheel
  driver attaches to it, reads its reports and sends it force-feedback commands), and turns those
  commands into effects on your wheel through DirectInput: the centring spring in the menus, the
  steering forces in a race. Any DirectInput wheel should work; buttons and pedals are preset for
  Logitech G29 / G923 / G920, and `PS3_WHEEL_*` settings in `play.bat` remap them (README FAQ).
- Checked without a wheel: the game accepts the G27, a race steers from it (a simulated wheel held to
  one side turned the car), and the game's force-feedback commands are decoded. Not checked: the feel
  on a real wheel, including which way the forces push (`PS3_WHEEL_FFB_INVERT=1` flips them).
- Nothing changes without a wheel: the port looks for one every few seconds on a thread of its own
  and stays out of the way (`PS3_WHEEL=off` stops even that). `logs\play.err` lists the controllers
  it saw, the wheel's axes, and what the game asked of the wheel.
- Runtime: the USB library (`cellUsbd`) is now real for this case, custom controllers (`cellPadLdd*`)
  work, and `sys_mempool_*` exists; the game's wheel driver keeps its state in those pools and failed
  without them.

🏷️ Released as **v0.1.1**

🧰 **Setup made easier** (after a player got stuck)
- `setup.ps1` now does the whole job in one command, run as often as you like. It finds Python (the
  `py` launcher first, avoiding the Microsoft Store stub), makes the environment, fetches and patches
  ps3recomp, downloads FFmpeg, and checks Git and Visual Studio. It checks your game files and says
  exactly what's missing, catching the common mistakes: a still-encrypted EBOOT, `PS3_GAME` or
  `dev_flash` one folder too deep. Once everything is there it generates the code and builds the game.
- `relift.ps1` checks for the Python environment, ps3recomp and EBOOT.elf before starting, and says to
  run setup instead of printing a cascade of errors.
- `build.cmd` finds any Visual Studio edition with the Clang tools (not only 2022 Community), and
  picks the number of parallel compiles from the PC's memory, so 16 GB PCs don't run out.
- Setup works from a ZIP download again (updating still needs a git clone).
- README: Setup rewritten around `setup.ps1`. The FAQ moved to the top as questions and answers, with
  new entries: widescreen vs ultrawide, ready-made builds (there can't be any), and setup errors.

🏷️ Released as **v0.1.0**

🧰 **Tools and repo**
- Versions and updates: every change now comes out as a numbered version on the Releases page.
  `update.ps1` updates an install to the newest one or switches to any other (`-List`,
  `-Version v0.1.0`, `-Dev`). It swaps the ps3recomp patch, re-lifts only when the lift tools change,
  rebuilds only what changed, and keeps saves, game files, the shader cache and edits to files like
  `play.bat`. It also works on installs made before it existed. See "Updating" in the README.
- `setup.ps1` uses the same patch step as `update.ps1`, and the patch's ps3recomp base commit is now
  part of each version (`GT5/patches/ps3recomp-base.txt`).
- README FAQ: steering wheels and force feedback, ultrawide, resolution, US copies. STATUS.md lists
  what isn't supported yet. Wheels with force feedback and ultrawide are on the list after player
  requests.

## 9 Nov 2026

🎨 **Graphics**
- Distant track surfaces no longer turn into blocky noise or another texture. At Tsukuba the road went
  black-and-white or grass green a few metres ahead, and banners and buildings were speckled. Many track
  textures arrive with only their top two or three detail levels filled in; the rest held whatever an
  earlier scene had left in that memory. Each level is now checked against the one above it, and missing
  ones are rebuilt from the last good level (`LD_MIP_REBUILD=0` turns this off). Far-away track also
  looks smoother on other circuits, where those levels used to be cut off.
- Every frame is shown once. Each frame was being presented twice, the first time half drawn, which
  flickered and made textures look wrong. It also made the window's FPS counter read double.

🎮 **Controls**
- The mouse is no longer held inside the window. While the controller is in use with the game in
  front, only the mouse movement and clicks that Steam makes from the controller are ignored; the
  real mouse always works. `PAD_CURSOR_CLIP=1` brings back the old behaviour.

⚡ **Speed**
- Optional watch mode for B-Spec races, replays and anything you only watch: **F9** in the game, or
  `LD_WATCH=1` in `play.bat` to have it come on by itself after 10 seconds without input. It draws every
  other frame while the race keeps full speed, draws nothing while minimized, and lowers the game's
  priority so other programs come first.
- Measured on the tested rig: races at 45 to 58 fps (median 47) over a 10-minute session.

🧰 **Tools and repo**
- Other releases: the lift now finds the few addresses that differ between releases in each person's
  own executable (`version_addrs.py`). The US release (BCUS98114) should work but is untested.
- Game-code mods: switchable instruction patches in `GT5/mods`, checked against the executable
  ([MODDING.md](MODDING.md)), with an experimental frame-rate unlock as the example.
- The repository went public: feedback forms, Discussions, a current-state board, modding and
  contributing guides, the GT-style banner, and a Discord contact.

## 7 Nov 2026

⚡ **Speed** (all on the graphics thread, which sets the frame rate in busy races)
- Processing the game's graphics commands takes about half the time it did: whole runs of commands are
  handed over at once, and a second copy of the graphics state that nothing read is no longer built.
- Each draw costs about 17% less CPU time: texture units are read once instead of three times, facts
  about each shader are remembered, the pipeline lookup key is one fast hash, the 8 KB block of vertex
  constants is reused when it hasn't changed (about a third of draws), sampler tables are found through
  an index, and plain textures skip the search for render targets.

## 6 Nov 2026

⚡ **Speed**
- A vertex cache: converted vertices and triangle lists are kept and reused while the game hasn't
  rewritten their memory, so most race draws skip conversion. Races went from 41-49 to 48-60 fps.
- Texture change checks skip about 90% of texture hashing by watching which memory pages were written.
- Graphics commands are recorded on a second thread.
- The game's main processor spends far less time on SPU bookkeeping (86% to 62% busy).
- The SPU job scheduler wakes as soon as work arrives instead of on its next 1 ms poll.

🎮 **Controls**
- The controller keeps working when another window takes focus (`PAD_BACKGROUND=1`).

🏁 **Gameplay**
- Vertical-blank and flip notifications reach the game as often as on the PS3 (they were merged).

## 5 Nov 2026

🎮 **Controls**
- L2 and R2 are analog again (they were all-or-nothing, so throttle and brake were full or off).
- A Bluetooth DualSense works with Steam closed, and losing focus no longer leaves triggers held.
- Steam's desktop controller layout no longer types into the game through the keyboard.

⚡ **Speed and smoothness**
- The race-start freeze went from about 4 seconds to under half a second: compiled shaders load from
  one pack file instead of thousands of small files.
- GPU waits per race frame went from 7-8 ms to about 1 ms: reading results back from the GPU no longer
  stops the frame, and sampler tables persist.
- Vertex conversion uses SIMD, index de-duplication is about 3x faster, and the recompiled game code is
  built with AVX2/BMI/MOVBE. Heavy 12-car races reached 42-44 fps, lighter scenes 47-51.
- Repeated graphics state (pipelines, render targets, viewports) is no longer re-sent every draw.

## 4 Nov 2026

🎨 **Graphics**
- The white glow around cars' edges is gone (the reflection map was read in the wrong format).
- Half-float textures stay half-float: coloured rings in the dusk sky and multicoloured rain are fixed.

🏁 **Gameplay**
- The in-game clock shows real time (it showed time since boot, dated 1970).
- Photo Mode works end to end: preview, save, and real JPEG files.
- The on-screen keyboard works (a Windows text box), so naming things no longer hangs.
- Tested working: B-Spec races and rewards, Car Delivery, Special Events, Course Maker (themes,
  layouts, test drive, save), GT TV with sound, Music Library, Photo Album, Garage, Driver List,
  Parts Shop, Manual.

🔊 **Sound**
- Race sound no longer cuts out: SPU memory transfers stopped fighting over one lock, and the sound job
  was made faster. Silence in races went from 62% of the time to under 0.3%.

⚡ **Speed**
- The FPS counter was fixed to count only frames with something drawn; real race speed went from about
  18-20 to 25-26 fps that day after a render-target copy bug was fixed.
- Frames are presented without waiting for the GPU to finish the one before.
- Closing the window exits the game properly.

## 3 Nov 2026

🏁 **Gameplay**
- Movies play, with picture and sound: the opening movie, its skip, and GT TV.
- Three causes of freezes fixed: lost wake-ups, SPU jobs squeezed onto one core, and the game giving up
  on the graphics chip during slow scene loads.
- A track load that hung with a full progress bar is fixed (more game threads allowed).
- Tested working: buying a car and keeping it after a restart, licence tests, A-Spec races, pause,
  results, replays, Photo Mode, driver creation, B-Spec. Five long test runs, no freeze.

🎨 **Graphics**
- The rainbow car paint is fixed (bordered textures read with the wrong layout).
- Compressed 3D textures decode correctly.
- Photo Mode and auto-exposure read real pixels back from the GPU.

⚡ **Speed**
- Saving shaders happens in the background; unused texture slots no longer cause extra shader
  compiles. SPU work spreads over 8 threads.

## 2 Nov 2026

🏁 **Gameplay**
- A race runs: Arcade, High Speed Ring, Fiat 500, with HUD, timer, track map and opponents, and the car
  drives.
- Menu backgrounds, buttons and the cursor draw.

🎨 **Graphics**
- The race looks like GT5: sky, signage and car paint. 3D textures, shadow comparison, texture gamma,
  signed textures, sRGB output and render-target cube maps were added to the renderer.
- Track lighting: textures stopped going black in the distance.

🔊 **Sound**
- Music and effects play reliably (the sound graph was being torn down by a job-chain bug), and the
  mixer runs on the clock without bursts.
- In-game music decoding added.

⚡ **Speed**
- The race went from about 3 frames a second to around 30, mostly by compiling 28 SPU jobs instead of
  interpreting them, and half the frames were found to be dropped by the flip queue.

🎮 **Controls**
- DualSense and DualShock 4 read directly over USB and Bluetooth (input only).

## 1 Nov 2026

🏁 **Gameplay**
- Project started: static recompilation with ps3recomp, from the European disc.
- First picture: GT5's own "Accessing game data" dialog.
- The game's heap was placed where GT5 expects it, which got it through Select Language, screen
  setup, creating system files and the welcome text to the real top menu, with the 3D scene behind it.
- Render targets that share memory now alias, as on the PS3.
