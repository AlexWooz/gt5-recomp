# 📜 Changelog

Everything done on the port, newest first. All dates are 2026. Frame rates are from the European
release on the tested rig in [STATUS.md](STATUS.md) unless a day says otherwise.

> Frame rates before **9 Nov** were read from the window's FPS counter, which at times counted empty
> or repeated frames (fixed on 4 Nov and 9 Nov), so treat them as rough before-and-after comparisons.
> From 9 Nov on, numbers are the game's own frames.

## 21 Nov 2026

⚡ **Steam Deck and Linux performance: async readbacks, an SPU lifter fix, and draw-engine work** (by AlexWooz, community pull
request; one re-lift of the SPU jobs, done by `update.ps1` / `setup.sh`)
- Eight changes, each with its own entry below and a switch to turn it off. Measured on a Steam Deck OLED
  (SteamOS, US 2.17 with DLC, Vulkan), with the race paused the moment control is given (steady to about
  1 ms). Two of them change Windows too: the two SPU lifter changes.
- On v0.1.109, paused grid at a fixed 1100 MHz GPU clock: the readbacks and the draw engine +4.7%
  (alternated in one run), the two SPU lifter changes +1.4 to +2.3% (two builds compared). On the Deck
  the GPU is also power-starved: with the 15 W chip at its limit, the CPU takes the budget and the GPU
  sits at 200 MHz while 100% busy. Steam's Quick Access > Performance > Manual GPU Clock (1100 MHz ran
  a paused grid as fast as 1600 or slightly faster) is a settings change, not in this code, but it is
  what most Deck players will want with it.

🧩 **SPU interrupt windows no longer go through the indirect-branch resolver** (AlexWooz; re-lift)
- Sony's WWS jobs open their DMA critical sections with `brsl $rX,.+4; ai $rX,$rX,K; bid $rX`: a branch
  to the constant link+K with interrupts disabled. The lifter sent every one through
  `spu_indirect_branch`. In GT5's scene traversal job (`w0159C180`) that was ~1.2 million a second at a
  single site, and 11 of its 15 hottest indirect targets (`SPU_IB_HIST=1`).
- The function finder's Duff's-device rule also seeded the 64 bytes after each such branch as function
  starts, so they were lifted as one-instruction functions: 40% of that job's functions.
- `spu_lifter.py` now branches directly (the D/E bit is still applied; `--no-pcget-direct` restores the
  old lift), and `find_spu_functions.py` seeds the 64-byte span only when another instruction writes the
  register (a real Duff's device). 1,599 sites in 38 Wws jobs; `w0159C180` goes from 3,509 functions to
  2,274.
- Images with hand-written native functions (`src/native/natives.txt`) are lifted as before, so their
  lifted files still match their `lift-sha1` and the natives stay on (checked on 2.17's reverb and
  01.00's two geometry jobs).
- Steam Deck: captured jobs replay bit-exact. On v0.1.104 the traversal job was 17% faster and
  `w01571080` 14%, paused grid 58.8 to 55.7 ms (+5.6%). On v0.1.109, whose direct branches already made
  these sites cheaper, this and the channel change below give +1.4 to +2.3% together.

📮 **SPU channel writes that only set a DMA parameter no longer store the registers first** (AlexWooz; re-lift)
- With `SPU_CH_INLINE` the lifted jobs make a write to the MFC parameter channels (LSA, EAH, EAL, Size,
  TagID, WrTagMask) themselves, as a store into the SPU context, but the lifter still wrote all of the
  function's changed registers back to the context before each one, as it does before a real call.
- Those six channels never read or change a register, so `spu_lifter.py` now leaves that out for them;
  every other channel op still stores them first. `--no-inline-wrch` at lift time restores it.

🖼️ **Vulkan on Linux: small readbacks don't wait any more** (AlexWooz)
- `VK_RB_ASYNC` (async readbacks of GT5's exposure sample and brightness chain) was on by default on
  Windows only. On Linux each of those readbacks first waited for the GPU to finish everything queued:
  ~24 ms of every race frame on the Deck (`VK_SYNC_STATS=1`).
- On v0.1.104, paused grid at the Deck's automatic GPU clock: 15.7 to 17.8 fps (+14%, alternated in one
  run with `PS3_AB_WHAT=vkrb`); the exposure looks the same in play. `VK_RB_ASYNC=0` turns it off.

🎨 **Vulkan on Linux: the vertex cache and the image pool** (AlexWooz)
- `VK_VCACHE` keeps converted geometry while the guest's vertex pages are unwritten, which needs
  Windows' `GetWriteWatch`. Linux now has it through userfaultfd write-protection
  (`UFFD_FEATURE_WP_ASYNC`, kernel 6.7+) and the `PAGEMAP_SCAN` ioctl, which lists and re-protects written
  pages in one call (what Wine builds `GetWriteWatch` on). A kernel without them keeps the cache off.
  SteamOS 3.7 has both. `VK_VCACHE=0` turns it off.
- `VK_IMAGE_POOL` is on by default on Linux too (it was Windows only). `VK_IMAGE_POOL=0` turns it off.

✏️ **Draw engine: three per-draw savings on the RSX thread** (AlexWooz)
- The vertex cache probe reads a compact array of tags instead of the 816-byte entries (one memory miss
  per probe on a 16,384-entry table); texture change checks use the page write tracking above instead of
  hashing the texture every frame, where it is available; 16-bit index runs are gathered and
  byte-swapped with SSSE3/AVX2.
- Steam Deck on v0.1.104: paused grid +2.8%, car select +2.3% (`PS3_AB_WHAT=drawfast`).

🛠️ **Linux: a profile-guided build** (AlexWooz)
- `bash GT5/scripts/pgo_linux.sh gen`, then `record` (play menus and a race, quit from the game),
  `use` and `install`. The profile is made by running the game, so it lives in `GT5/analysis/pgo` and is
  never committed; after a re-lift record again. Steam Deck, paused grid: 107.4 to 92.5 ms a frame.

🔍 **Small things** (AlexWooz)
- `SPU_IB_HIST=1`: every 5 s, which indirect-branch targets reach the full SPU resolver most often.
- `ppu_lifter.py` rewrites an existing output file that isn't valid text instead of stopping.

## 20 Nov 2026

🏷️ Released as **v0.1.109**

- **Installer:** when Git can't use its own certificate file ("error adding trust anchors", a damaged Git for Windows, #46), downloading the code is tried again through Windows' certificate store, and the install keeps that setting so updates work too. Checked with a broken certificate file in Git's global settings (the retry gets through) and on a normal PC (nothing changes).
- **Nothing left out while shaders build** (DirectX 12, part of Asynchronous shader loading): an object whose own shader is still being built is drawn at once with a general stand-in and switched over when the real one is ready. The stand-in's own shaders are built once in the "Preparing shaders" step (about 20 seconds). On a PC that has not played yet, about one part in fifty still goes missing for a moment (55 draws in a first race, against about 2,500 before); with a new car or track on a PC that has played, about one in twenty. The stand-in also draws most of the passes the game used to wait for (shadow maps, passes that read a rendered picture), so a first session waits about 0.3 seconds in all where it waited over a second.

## 19 Nov 2026

- **Button symbols** (Controls, a choice; the game's own by default): the game's menus can show Xbox buttons or your keyboard keys in place of the PlayStation symbols, on the help bars and on the Options controller picture. Checked on Europe 1.00; a release with other pictures keeps its own.
- **After a crash or a freeze** the launcher offers **Save a report**: one ZIP of the logs with your user name and folders taken out, to attach on the report page. Nothing is sent by itself.
- The log's frame generation line no longer repeats many times a minute.
- The two extra job threads follow the priorities of the SPU they stand in for.
- **Frame generation** makes frames on every screen: replays, the pre-race screen, menus and pause, at 30 drawn as well as 60.
- **Shaders are prepared before the game starts** (a short step in the launcher, after updates and when graphics settings change).
- **Shorter freezes** when new shaders are needed: they build on all the PC's cores.
- **Asynchronous shader loading** (Display, on by default): no pause when a car or track first appears; a part can show up a moment late instead.
- **Faster start with a big shader cache**: the cache is usable at once; a long pause at the first menu is gone.
- If the game ever stops drawing for 8 seconds, the log now says where it is stuck, with the state of the game's job system.
- Long sessions: the runtime's lock table no longer fills up (a fix first made in Project Tourline 5, MIT, adapted).
- Groundwork for building shaders before you reach a new car or track: the game can look through its own model files in the background (Europe and Asia 1.00; it stays idle until a list of known shaders ships).
- **Pixel shaders build about a third faster.** Every PC builds them again once after this update (before the game starts where it can).

## 18 Nov 2026

- **Lighter on the CPU**: less work around every SPU job, the first native code for the biggest one, and SPU code that branches straight to where it is going.
- **Frame rate**: 60 or 30 (in the game's menu and the launcher); Frame generation doubles either.
- **Controller**: the buttons are the game's own again by default; throttle and brake on the triggers is a choice (Controls, Triggers).
- **Vulkan**: the Resolution setting (up to 5K) and NVIDIA DLSS / DLAA work.
- **DirectX 12**: frames no longer wait on the graphics card's whole queue; a little less work for the render threads.

🏷️ Released as **v0.1.108**

- **Fills the screen**: no black border.
- **Shadows**: sharper, with a new Shadows setting (Standard / High / Ultra), and several fixes.
- **Sharper textures** on the road and on cars.
- **DLSS / DLAA and Resolution** work on every 3D screen, not only in races.
- **Frame generation**: one switch. Uses NVIDIA DLSS, AMD FSR 3 or the built-in generator, and shows which.
- **In-game menu (F1)**: readable at any window size, fewer options, one Resolution setting.
- **F11 / Alt+Enter**: full screen.
- **Less stutter** when a car or a track loads.
- **Controller**: the triggers are throttle and brake (analog), the bumpers shift.
- **Quitting**: Esc asks in the game, with a controller too, and saves are flushed.
- **Sound**: follows the Windows output device; new Volume and Mute in the background.
- **Window**: starts borderless, sharp at any Windows scaling, keeps its shape at any size.
- **Vulkan**: the in-game menu (F1), F11, fills the screen, starts borderless, sharper shadows and textures, vertical sync Off; set up at install.
- **In-game menu (F1)** pauses the race and resumes it when closed.
- **Launcher**: resizable, no console windows, settings match the game's.
- **Installer**: checks the PC first; Vulkan and FSR 3 are set up during the install.

## 17 Nov 2026

🏷️ Released as **v0.1.107**

🧪 **A test channel**
- `update.ps1 -Test` switches an install to the test build: changes that are still being tried out, kept on
  a branch of their own (`test`) that releases, new installs and the launcher's Update never point at.
  A plain `update.ps1` (or Update.bat, or the launcher's Update) goes back to the newest release. When
  there is no test build it says so.

🏷️ Released as **v0.1.106**

🎨 **Installer: a calmer look and plainer words**
- The header is dark slate with a blue progress bar and spinner (it was a large red band, which looked
  alarming); red is kept for an actual failure. The window is titled **GT5 Recomp**.
- The steps and status lines say what is happening in a few plain words ("Build tools", "Converting the
  game", "Done") without tool names or file names. The specifics are still in Details and in the log.
- Notes that don't need the player (a fallback that worked, a compiler retry, processors left out by the
  check) no longer appear in the main window; they are in Details and the log.

🏷️ Released as **v0.1.105**

🪟 **The installer is one window now, installs nothing into Windows, and no longer needs RPCS3**
- **One window from start to finish.** After Install the same window shows the eight steps (done,
  running, waiting), one line saying what is happening and roughly how long it takes, a progress bar
  with real figures (download bytes, files copied, functions generated, the compiler's file count), and a
  Details button with the full text. The command prompt that used to scroll for an hour is gone (the
  .bat's own console closes after a fraction of a second).
- **Questions are part of the window.** Microsoft's licence for the compiler libraries is a page with
  the link and a tick box that only you can tick (it used to be "type yes" in the console). Nothing else
  asks anything.
- **When something stops**, the window says what in a sentence, with Try again (it resumes: finished
  steps are skipped), Open the log, Copy details and Report a problem. Cancel, or closing the window,
  asks once, stops everything it started and says it is safe to run again.
- **Finish page** with Play and a tick box for the desktop shortcut.
- **No RPCS3 for the unlock steps.** The game's `EBOOT.BIN` and Sony's firmware file are now unlocked by
  the port's own small tool, `GT5\scripts\ps3_unlock.py` (Python standard library only; about 3 s for
  the firmware, under a second for the executable). It was checked byte for byte against RPCS3's output:
  the executable of the Europe 1.00 disc (and, by the tool's own tests, the Asian 1.00 disc and the 2.01
  and 2.17 updates) and all 1,075 files of firmware 4.92. Sony's keys are still not in this repository:
  the tool fetches the key tables (about 50 KB) from RPCS3's public source at a pinned commit, checked by
  SHA-256. If that fetch or the tool fails, the installer falls back to RPCS3 as before, now started
  hidden; RPCS3 is downloaded only then. Installing game-update `.pkg` files still uses RPCS3.
- **Nothing is installed into Windows any more.** Git was the last thing (winget, with a permission
  prompt): when the PC has no Git, the port fetches a portable one (Git for Windows' MinGit 2.56.1,
  38 MB, SHA-256 and signature checked) into `tools\git`, and the installer, `setup.ps1`, `update.ps1`
  (so Update.bat and the launcher's Update button), `add_vulkan.ps1` and `add_dlss.ps1` use it.
- **A PC whose processors calculate wrongly no longer breaks the install.** A fresh install on an
  i7-14700KF kept stopping in code generation with Python errors that the tools' own lines cannot
  raise. A deterministic job run on each of its 28 logical processors showed why: two of them failed it
  most of the time, the other 26 never. Setup (and the installer, before it unlocks anything) now makes
  that check, a minute or two, with the new `GT5\scripts\pc_check.py`; processors that fail are left
  out of the whole install, the player is told which, and the result is kept for 30 days. If code
  generation still stops, the check is made again with one of the lift's own jobs before the second
  try. `pc-check.ps1` runs it by hand; README, "A processor that calculates wrongly".
- Also fixed: running the installer on an existing install updates it first (the tools used to run from
  the old scripts); a download or copy that was cut short resumes (it used to count as done, or block
  the folder); an .iso is dismounted properly; output of every program reaches the log.
- Tested on this PC up to the build (new install with no Git, existing install, the licence page,
  a forced failure, cancel, both unlock routes). **Not tested: a whole install through the hour-long
  build with the new window**; that run is being made after this release.
- No re-lift: nothing the code generation is made from changed.

🏷️ Released as **v0.1.104**

🐍 **The port brings its own Python: none has to be installed, and the PC's own is no longer used**
- Why: the tools that turn the game's code into C++ are Python programs, and until now they ran on
  whatever Python the PC had (or the installer put Python 3.11 on the PC with winget). A fresh install
  on a player's PC stopped in "spu_jobs" with `TypeError: 'int' object does not support the context
  manager protocol` on a line with no `with` in it: an error from inside that PC's Python, which the
  tool's own line cannot raise.
- Now `setup.ps1` (and the installer, right after it downloads the code) runs the new `get-python.ps1`:
  python.org's own build of **Python 3.11.9**, the version every release is generated with, from the
  package the Python Software Foundation publishes on nuget.org. 17 MB, 43 MB unpacked, into
  `tools\python` of the install folder. The download is checked against a SHA-256 and `python.exe` must
  carry the Python Software Foundation's signature. Nothing is installed in Windows, no administrator
  rights, PATH untouched; delete the folder to remove it.
- The tools' environment (`venv`) is made from that copy. An install that already works keeps its
  environment as it is. Running `GT5-Installer.bat` again switches an existing install to the port's
  own Python (that is the fix for the install above: download the new installer and run it again).
- `GT5-Installer.bat` no longer installs Python; of the things it used to put on the PC only Git is
  left. `get-toolchain.ps1` uses the port's Python too.
- If generating the game code still stops once, `setup.ps1` and `update.ps1` run it a second time from
  the start before giving up, and the message names the Python version.
- The lift scripts themselves are unchanged, so nobody has to regenerate the game code for this update.
- Checked on made-up install folders: fetch (and a second run finding it there), a new environment made
  from it with the tools' packages installed, an existing environment kept, and one made again when the
  installer asks. The player's failure itself could not be reproduced here.

🧰 **The installer no longer installs or changes Visual Studio**
- `GT5-Installer.bat` used to install Microsoft's C++ Build Tools (about 7 GB) when no Visual Studio with
  the Clang tools was found, and on a PC that had a Visual Studio without them it opened the Visual
  Studio Installer and added several GB of workloads to it (seen on a PC with Visual Studio 2026: it
  started modifying that). Neither happens any more.
- Now: a Visual Studio that already has the Clang tools and a Windows SDK is used as it is. Otherwise the
  installer runs `get-toolchain.ps1` right after downloading the code: LLVM 19.1.5, CMake, Ninja and
  Microsoft's C/C++ libraries and Windows SDK, about 1.5 GB, all inside the install folder
  (`tools\toolchain`). No administrator rights, nothing installed in Windows, the same compiler version
  the releases are tested with; delete the folder to remove it. Microsoft's part is under Microsoft's
  licence: the installer shows the link and goes on only when you type `yes`.
- Git and Python are still installed with winget when they are missing.
- Checked on made-up install folders (a usable Visual Studio; tools already fetched; licence declined:
  nothing downloaded and a clear message; an old checkout). A whole install through this route was not
  run here, because Microsoft's licence is the player's to accept; the tools and `build.cmd`'s use of
  them are unchanged since v0.1.28.

🏷️ Released as **v0.1.103**

Superseded by v0.1.104 straight away (the same change); install v0.1.104 or later.

🏷️ Released as **v0.1.102**

🧰 **The installer no longer installs or changes Visual Studio**
- `GT5-Installer.bat` used to install Microsoft's C++ Build Tools (about 7 GB) when no Visual Studio with
  the Clang tools was found, and on a PC that had a Visual Studio without them it opened the Visual
  Studio Installer and added several GB of workloads to it (seen on a PC with Visual Studio 2026: it
  started modifying that). Neither happens any more.
- Now: a Visual Studio that already has the Clang tools and a Windows SDK is used as it is. Otherwise the
  installer runs `get-toolchain.ps1` right after downloading the code: LLVM 19.1.5, CMake, Ninja and
  Microsoft's C/C++ libraries and Windows SDK, about 1.5 GB, all inside the install folder
  (`tools\toolchain`). No administrator rights, nothing installed in Windows, the same compiler version
  the releases are tested with; delete the folder to remove it. Microsoft's part is under Microsoft's
  licence: the installer shows the link and goes on only when you type `yes`.
- Git and Python are still installed with winget when they are missing.
- Checked on made-up install folders (a usable Visual Studio; tools already fetched; licence declined:
  nothing downloaded and a clear message; an old checkout). A whole install through this route was not
  run here, because Microsoft's licence is the player's to accept; the tools and `build.cmd`'s use of
  them are unchanged since v0.1.28.

🏷️ Released as **v0.1.101**

✨ **NVIDIA DLSS and DLAA, a frame generator that uses the game's real motion, and menu fixes (DirectX 12)**
- **Anti-aliasing with NVIDIA DLSS** (RTX graphics cards). In the launcher (Display, **Anti-aliasing**) or
  in the game's menu (F1, Display): **DLAA** draws the 3D picture at full size and cleans its edges;
  **DLSS Quality / Balanced / Performance / Ultra Performance** draw it smaller and rebuild it, for slower
  cards; **custom scale** takes any share from 33% to 100% (the "DLSS custom scale" row). A **DLSS
  model** row picks NVIDIA's model (default, K, M, J, L). All of it can be changed while racing.
- DLSS is NVIDIA's software and no part of it is in this project. The first time you pick a DLSS mode
  the launcher offers to add it: it fetches NVIDIA's DLSS SDK (about 150 MB) from NVIDIA's own GitHub
  page, shows NVIDIA's licence for you to accept, checks that `nvngx_dlss.dll` is signed by NVIDIA, and
  builds a small plug-in (`gt5_upscale_dlss.dll`, this project's own source in `GT5/src/upscale`) next
  to `gt5.exe`. `GT5\scripts\add_dlss.ps1` is that step; `-Remove` takes it out again. Without it the
  game is the same as before and the DLSS choices are greyed out with the reason.
- How it fits GT5: the renderer finds the moment the 3D scene is finished and the HUD starts, gives
  DLSS the scene, its depth, a sub-pixel camera shift each frame and real motion vectors for every
  object (cars, the cockpit interior and the mirror each have their own), and hands the result to the
  game's own exposure and bloom. It works on races and replays' 3D picture; menus and the car-select
  screen are left as they are. The output is the size of the picture on screen, so it pairs with
  **Render resolution** (1440p + DLAA is a good start on a 1440p screen).
- Measured in races on an RTX 4070 Ti SUPER: DLAA in the 1080p mode cost 1 to 2 frames a second (58.5
  against 59.8, switched on and off every 10 s in one race); DLAA at a 1440p render resolution 59 fps;
  DLSS Quality there (2048x864 drawn, 3072x1296 out) 59 fps. Edge roughness on a dumped frame fell from
  20-26% of edge pixels to 12%.
- **Frame generation, presentation fixed.** Made frames were spaced as if the game always ran at exactly
  60 and, with vertical sync off, were still held to a refresh by Windows, which gave an uneven
  6 / 12 ms pattern on a 165 Hz screen. They are now spaced from the measured frame time, anchored to
  the last frame shown, and with vertical sync off presented with the tearing-capable flag (even on a
  variable-refresh screen). In a race with 2x: the game stayed at 59.5-59.9 fps, about 125 shown a
  second. A stall no longer switches it off for the rest of the run. `LD_FG_PRESENT=0` restores the old way.
- **Frame generation from the game's own motion** (menu: **Frame generation: how**; `LD_FRAMEGEN=2`).
  The old generator guesses motion from two finished pictures, HUD included, which warps the HUD and
  wobbles on fences. The new one uses the same motion vectors and depth DLSS gets and never touches
  the HUD. It makes frames in races only (menus are shown as drawn). New and still being judged; the
  old one stays the default.
- **Menu fixes.** Frame generation "Off" did not turn it off when only the multiplier was set (the row
  showed Off while it stayed on): fixed. Rows that depend on another setting are greyed out with the
  reason until they apply (DLSS model and custom scale, "how" for frame generation, window size outside
  window mode, mouse steering's numbers, the wheel's rows). The anti-aliasing and frame generation rows
  say what they are doing right now. "Resolution" is now called **PS3 video mode**, apart from Render
  resolution.
- **F8: a snapshot for bug reports.** If you see a picture fault, press F8 with the game in front: every
  picture and depth buffer the renderer holds is written to `GT5\logs\snap_<date>_<time>\` (about a
  second's pause, some hundred MB). Attach or mention it in a report. `LD_SNAP_KEY=0` turns the key off.
- Vulkan: none of this yet. No re-lift: the update rebuilds the runtime, the game's own sources and
  the launcher.

🏷️ Released as **v0.1.100**

🎛️ **In-game menu: cockpit view, mouse steering and force feedback apply while playing**
- Changed in the menu (F1), these now count at once instead of from the next start: the cockpit view's
  field of view, seat position and seat height; mouse steering's speed and self-centring; force feedback
  strength and direction. Tried in a race from the driver's seat: field of view +30 set in the menu, the
  view widened from 52.4 to 82.4 degrees there and then.
- The cockpit view's wrapper is still only put in the camera's way once one of its three settings is
  set, so a game with none of them set is untouched, as before.
- A wheel that was opened without force feedback (strength 0) is opened again with it when the strength
  comes up.
- While the menu is open the steering wheel reads centred with the pedals released, as the controller
  reads at rest.
- The menu's wheel map ("press a button on the wheel") now sees the PC wheel's own buttons and axes.
- Still from the next start, and marked so: the game's 720p / 1080p mode and screen shape, the
  renderer, the game's language, which wheel is used and its rotation, and whether the mouse steers
  (the game looks for its wheel once, at boot).
- No re-lift.

🏷️ Released as **v0.1.99**

🖥️ **Render resolution up to 5K, and a settings menu inside the game (F1) (DirectX 12)**
- **Render resolution** (launcher, Display; or the new menu): the picture is drawn at 1080p, 1440p, 4K or
  5K, whatever the game's own 720p / 1080p mode is. The game still thinks it draws its PS3 picture; every
  screen-sized target it draws into (the race, the HUD, the menus, the passes behind them) is made that
  much larger on the graphics card. Sharper, and far fewer jagged edges; on a screen smaller than the
  picture it is filtered down to the window, which smooths edges further. `LD_RES_TARGET=<lines>`
  (or `LD_RES_SCALE=<factor>`, 0.25 to 8; below 1 draws fewer pixels for a slow graphics chip).
- Measured in a race in the 1080p mode on an RTX 4070 Ti SUPER, switching to 4K while driving: 57 to 60
  frames a second before and after. The cost is the graphics card's alone; an older card will feel it.
- **The menu: F1**, or hold Select + Start on the controller. Display, Performance, Controls, Game and
  About, over the running game, with the keyboard, the mouse or the controller. It reads and writes the
  same `settings.bat` as the launcher, so the two always agree.
- Each row says when it counts. **Applies now:** render resolution, frame generation and its multiplier,
  vertical sync, window mode and window size, watch mode and how much it draws, the 30 fps Speed
  setting, how waiting threads wait, the keyboard keys (with a "press a key" map), controller in the
  background, keeping the mouse in the window. **From the next start:** the game's 720p / 1080p mode
  and screen shape (the game sizes itself from them once, at boot), the renderer, the game's language,
  the steering wheel and mouse steering, the cockpit view. Those rows say so.
- The Performance page shows the game's and the drawn frame rate, cores busy, the frame's size, the
  screen's refresh rate and the draw count; the same as a small corner readout if you switch it on.
- The game is not paused by the menu: in a race, pause first. While it is open nothing you press reaches
  the game.
- The anti-aliasing / upscaling row is there and greyed out: those modes are the next step. Until then
  a render resolution above your screen's is the anti-aliasing (4K on a 1440p screen is 2.25 samples a
  pixel).
- The menu is drawn with Dear ImGui 1.93.0 (MIT licence, unmodified, in `GT5/third_party/imgui`).
- Vulkan: neither yet. No re-lift: the update rebuilds the runtime, the game's own sources and the
  launcher.

🏷️ Released as **v0.1.98**

🎞️ **Frame rate multiplier: 2x, 3x or 4x on screens faster than 60 Hz (DirectX 12)**
- Frame generation could only fill frames the game left out (watch mode, 30 fps). It can now multiply:
  for every frame the game draws, one, two or three more are made on the graphics card and shown between
  the drawn ones, evenly spaced. Launcher, Display: **Frame rate multiplier** (`LD_FRAMEGEN_MULT=2..4`;
  it turns frame generation on by itself).
- It never makes more than the screen can show: 2x needs 120 Hz, 3x needs 180 Hz, 4x needs 240 Hz. A 144
  or 165 Hz screen gets 2x. The refresh rate is read from the screen the window is on.
- Measured in a race on a 165 Hz screen: 287 drawn and 301 made frames in 5 seconds (about 118 a second
  on screen) with the game itself at 57 frames a second, the same as without it; the thread that draws
  did not wait for the presenter at all.
- Two things had to change to get there. The first version spaced each group of frames over the whole
  frame time and waited for the screen's refresh at every present: the presenter was inside the driver
  most of the time, the drawing thread queued behind it and the game fell to 17-22 frames a second. Now
  a group's frames are timed by the presenter's own clock and presented without waiting for the
  refresh, and it stops spacing a group the moment a newer frame is waiting.
- The game still runs at 60; the newest drawn frame reaches the screen about half a frame later. Made
  frames have the usual limits: fast detail close to the camera can smear. Still marked beta.
- No re-lift: the update rebuilds the runtime and the launcher.

🏷️ Released as **v0.1.97**

🧹 **Fix: after many cars, new ones were no longer drawn (DirectX 12). And a picture check before every release**
- **The bug.** The DirectX 12 renderer kept every pipeline it ever built (one for each combination of a
  car's shaders and render states) in a table of 8192 that only grew. A long session through the
  dealerships fills it, and from then on every new pipeline's draws were dropped: new cars and scenes
  came up with parts missing until the game was restarted. A player's log showed it: 8192 pipelines,
  461,803 draws dropped ("shaders stopped compiling after visiting a few dealerships"). It is older than
  the body-panel fault of v0.1.92-v0.1.95 and has nothing to do with it.
- **The fix.** When the table fills, the pipelines no draw has asked for lately are released (idle for 30
  seconds, less if that frees too little; never one used in the last two frames, never one still being
  built), and one that is needed again is built again from the shader caches. Tested with the table made
  small (`LD_PSO_CAP`): 600 entries, one fill in a race, 257 released, no draw dropped; 150 entries, less
  than a race needs at once, 128 fills without a crash. Vulkan has the same limit in its own table; that
  one is still to do.
- **A picture check before every release.** v0.1.92-v0.1.95 went out with cars missing their body while
  every test that compares code with code passed; the test runs' own frames showed the broken car and
  were not looked at. From now on the frames of a driven run of the build (car select with the car at an
  angle, the race) are compared by a program with a known-good run, a side-by-side sheet is looked at, and
  the release script refuses to publish a change to the game's code without a passing record for exactly
  those sources. Run on tonight's builds it fails every one from v0.1.92 to v0.1.95 and passes v0.1.91 and
  v0.1.96. It looks at one car and one track, so it catches a fault like that one, not every fault.
- No re-lift: the update rebuilds the runtime.

🏷️ Released as **v0.1.96**

🚨 **Fix: cars missing body panels since v0.1.92. And the waiting threads sleep: a fifth less CPU on big processors**
- **The bug.** From v0.1.92 to v0.1.95, premium cars lost parts of their body: in car select while the car
  turns, and in races (bonnet, doors, panels gone, the inside showing). Reported by two players on the
  Discord, on DirectX 12 and Vulkan; the same here once looked for. The cause was one of the two changes
  to the SPU translation in v0.1.92: float chains ran on values that had not had the SPU's "a zero is +0"
  rule applied yet ("twins"), and somewhere an operation got a twin that no longer belonged to its
  value. It passed 4.27 million differential runs on six job images and every compare race, and it was
  wrong in the game. It is switched off (the helpers compute from the values again; no re-lift), and it
  stays off until the fault is found and the car-select picture is part of the check. My test runs had
  the broken picture in their frames all along and I had not looked at them: releases now get the car
  looked at, at an angle and in a race, old build next to new.
- **Waiting threads sleep instead of spin, on every processor.** The thread that walks the game's graphics
  commands and the one that records them for DirectX 12 spun while they waited, on processors with more
  than 12 threads (smaller ones already slept). Switched off and on every 10 seconds inside one race on a
  32-thread i9: cores busy 3.53 to 2.96 (-22% +-3%), frames a second +0.7% +-0.3%. On 16 slow cores alone:
  -10% CPU and -2.5% frames a second (there the CPU is the limit). `PS3_SPIN_SLEEP=0` spins as before.
- **Float operations with a lane out of range** (real game vectors often carry something that is not a
  number in their fourth lane) were redone one lane at a time in slow exact code; they are now done for
  all four lanes in double precision on the CPU's vector unit, with the same results: 2.4 billion random
  lanes per operation and all pairs of an edge set against the exact reference, no difference; 2.5 to 6
  times faster per such call. Those routines were 3.8% of the SPU workers' time.
- **Native 0x66A8 and 0x6418** (job `w0163C080`) no longer hand a call back when a lane is out of range:
  counted in a race, 0 calls handed back (it was 79% and 100%), and on 96 inputs captured from the game
  6-8 ns a call against 43-47 ns.
- Measured before the fix above was in (so with the faulty translation feature still on, which was a
  little faster): against v0.1.95 on 8 slow cores, three pairs, the whole game about 4% less CPU. The
  numbers for this exact build are still to be measured.
- No re-lift: the update rebuilds the SPU jobs and the runtime.

🏷️ Released as **v0.1.95**

💤 **The SPU workers are woken one at a time: 6% less CPU for the whole game**
- The job scheduler kept one wake-up signal for all seven SPU worker threads and the coordinator. Every
  job dispatch and every finished job woke all of them, thousands of times a second, and each idle worker
  took the scheduler's lock, saw that the slot was not its own and went back to sleep. A profile of
  v0.1.93 had a quarter of the workers' running time in that.
- Each worker now has its own signal, and a dispatch wakes only the worker it filled. The code had been
  there since 13 Nov, switched off: it was judged by frame rate on 4 slow cores then and showed nothing.
  Judged by CPU it is the biggest single saving of this round.
- Measured by switching it off and on every 10 seconds inside one race (so both halves see the same load):
  8 slow cores, cores busy -5.7% +-1.6%, frame rate +0.9% +-0.7%; all cores, -8.3% +-1.7%, frame rate the
  same; 4 slow cores, where the CPU is the limit, frame rate +3.9% +-2.5%. As whole races against v0.1.94
  on 8 slow cores, two pairs: the whole game 516 to 485 (hundredths of a core), the SPU workers 343 to 314.
- `SPURS_SLOT_WAKE=0` brings the shared signal back.
- No re-lift: the update rebuilds the runtime.

🏷️ Released as **v0.1.94**

🔧 **The native grid pass and pack pass now really run in the game (a correction to v0.1.93)**
- v0.1.93 said the grid pass (`w01606C00` 0x8038) and the pack pass (0x7DC8) run as native code. In the
  game they did not: both stood aside whenever SPU interrupts were enabled, and GT5's worker jobs keep
  them enabled the whole time (the job manager's handler). A test build that counts what each native
  function does showed it: 0 of 270,000 calls of the grid pass taken natively in a race. The test harness
  had only run them with interrupts off, and the side-by-side compare mode skipped the calls it could not
  take, so neither showed it. The fill loop, 0x66A8 and 0x6418 were not affected by this.
- Now they run whenever no enabled event is pending when they start (none ever was in the race). An event
  raised while one runs is taken at the next label of the translated code that follows, a few microseconds
  later; with none pending the translated function's own checks do nothing, so the results are the same.
- Counted in a race with this build: the grid pass natively on all 278,000 calls and all 572 million
  cells (none handed back), the pack pass fast-forwarding 283 million passes. Compared side by side with
  the translated functions on every call, real data: 150 million calls, no difference.
- Still handed back most of the time, and next: 0x66A8 (79% of its calls) and 0x6418 (all of them), because
  the fourth lane of the vectors they work on holds values outside the PC's float range in the real data.
- Measured against v0.1.93 on 8 slow cores, races run back to back: the SPU workers use about 3% less CPU
  (pairs: -1%, -5%; one more pair was unusable). So the natives of v0.1.93 and this fix together are a few
  percent of the workers' time, not the large step the functions' own numbers (4 times faster alone)
  suggested: those functions are about 8% of what the workers do.
- `cmake -DGT5_NATIVE_STATS=ON` builds the counting version (`[native-geo-stat]` every 5 seconds), for
  checking that a native function is really taken before believing a measurement.
- No re-lift: the update rebuilds two SPU jobs' native files and links.

🏷️ Released as **v0.1.93**

🧱 **Native code for the busiest geometry functions, and a cheaper path from SPU code to the runtime**
- Five functions of the two geometry jobs that run on the SPU workers every frame now run as PC code:
  the grid pass (`w01606C00` 0x8038, about 4 times faster measured alone on a 16x128 grid), its fill loop
  (0x5BA8, 2 to 7 times) and the pack pass after it (0x7DC8, 3.8 times); in `w0163C080`, 0x66A8 (2 times)
  and 0x6418 (1.4 times). Each was run 3 million times against the translated function with every register
  and the whole local store compared, and with one bit flipped on purpose to see the check catch it.
- In the game, with both versions run side by side on every call: 152 million calls in a race, no
  difference. `GT5_NATIVE_GEO=0` runs the translated code, `=2` runs both and logs any difference
  (`[native-geo]`).
- The grid pass keeps the parts its fast path does not cover as the translated statements themselves. Those
  are cut out of your own copy's translation when the game is built (`src/native/gen_*.py`), so no game
  code is in the repository. `natives.txt` can now say which translation a native function was written
  for (`lift-sha1`): with any other (another release of the game), that job simply stays translated.
- Runtime, on by default: the hook every SPU write to graphics memory goes through kept one shared ring
  that all workers wrote (110-130 ns a call with 7 threads; now a ring per thread, about 8 ns); writes to
  the DMA channels and status reads take a short path (a 128-byte PUT: 60 to 26 ns). The old paths stay
  in the build and were checked against the new ones (`GCM_PUT_HOOK=old|check`, `SPU_CH_FASTPATH=0`,
  `SPU_DMA_FASTPATH=0`, `SPU_MFC_CTX_CACHE=0`).
- Measured against v0.1.92 on 8 slow cores, three pairs of races run back to back (before the pack pass
  was added): the SPU workers use 3.6% less CPU (2.8%, 4.9%, 3.1%), the whole game 1.5%. Both builds hold
  57 frames a second there. Smaller than the functions' own numbers: a fresh profile shows the workers
  now spend a quarter of their time going to sleep and being woken, and that is the next thing.
- In the build but off until raced: `SPURS_WAKE_FILTER=1`, `SPURS_NATIVE_WORK=1` and `SPU_SN_ONCE=1`
  (fewer wake-ups of the job scheduler, a native look at a job list before running its manager, one
  interrupt per stall instead of two).
- No re-lift: the update rebuilds the SPU jobs and the runtime.

🏷️ Released as **v0.1.92**

🧱 **The SPU translator writes faster code, and the reverb and the EQ's FFT are native**
- Two changes to how SPU code is translated (`tools/spu_lifter.py`, both on by default):
  - A read of a register's first word (the SPU's "preferred slot", used for every address, counter and
    branch test) is written so the compiler keeps the register in one machine register. Before, each such
    read made it keep a second copy in memory or move the value between vector and integer registers.
  - The SPU rule "a zero result is +0" was applied after every float operation, three operations in
    series on every chain of float maths. It now runs where a value leaves the chain (a store, a compare,
    a shuffle); inside the chain the unfixed value is used, which gives the same results (the sign of a
    zero operand cannot change a non-zero result, and a zero result is fixed where it leaves).
- Same results: 4.27 million runs of the old translation against the new one on six job images (every
  register and the whole local store compared), no difference. Measured alone, the busiest geometry
  function is 1.3 times faster and the reverb's loops 1.4 to 3.6 times; the generated code is 3 to 8%
  smaller.
- Native code for the reverb (9 functions) and the EQ's FFT (6 functions), next to the mixer of v0.1.91.
  Each was run 3 million times against the translated function without a difference, and is 1.8 to 5
  times faster measured alone. `GT5_NATIVE_SND=0` runs the translated code, `=2` runs both and logs any
  difference (`[native-snd]`).
- In the game: with both versions of every native sound function run side by side (mixer, reverb, EQ),
  159 million calls in menus and a race, no difference. The sound jobs together take 17% less time than
  in v0.1.91 (two pairs of races run back to back on all cores: 2.21 to 1.84 and 2.18 to 1.84 seconds of
  job time per 10 seconds).
- What it does not show yet: the whole game's CPU use. On the test PC both builds hold 57-58 frames a
  second on the fast cores and on 16 slow cores, and the busy-cores figure differs less between the
  builds than between two races of the same build (about 5%). The gain in the geometry jobs is there in
  the isolated measurements, not yet in a number from a race.
- One re-lift: the update regenerates and rebuilds the game code once more (about 10 minutes plus the
  build). The native functions that follow build on this translation and will not need another.

## 16 Nov 2026

🏷️ Released as **v0.1.91**

🧱 **First native code in place of translated PS3 code: the sound mixer's inner loops, 44% less time for the mixer**
- Until now every SPU function ran as a step-for-step translation of the PS3's code. The sound mixer's hot
  loops (its two biquad filters, the resampler, the mix into the buses and the ADPCM block decoder) now run
  as code written for a PC: filter state in scalars, one window load where the SPU code did three loads and
  three shuffles, a whole ADPCM block decoded before its clamp is looked at.
- Same output, bit for bit: 14 million test cases against the translated functions (every register and the
  whole 256 KB local store compared, with audio-like data, edge values and unaligned or overlapping
  buffers), and a race with both versions run side by side on every call: no difference.
- Measured in a race (the game's own job timer, the same build, three races): the mixer job takes 0.88 ms
  per 5.33 ms sound block, down from 1.56 ms. Per function it is 1.5 to 4.4 times faster. On small CPUs the
  sound thread is the one that runs out first, so this is where stuttering sound should ease; that has not
  been measured on a small CPU yet.
- `GT5_NATIVE_MIXER=0` runs the translated code as before, `=2` runs both and logs any difference
  (`[native-mixer] MISMATCH`; slow, for testing).
- How it is built in: `GT5\src\native` holds the native functions and `natives.txt` lists them; the build
  compiles the mixer from a copy in which those five functions are renamed and the native ones take their
  place (`scripts\spu_native_patch.py`). No re-lift: the update rebuilds the mixer and links.
- Only where the mixer is the disc version's image (`jD58063CD`). A copy whose mixer is another image (game
  update 2.17 has its own) keeps the translated code for now. Windows only for now.

🏷️ Released as **v0.1.90**

🎞️ **A 30 frames-a-second game at real speed, filled back up to 60 by the graphics card (new mod: half-rate)**
- Found while measuring: GT5 does not go into slow motion the moment it drops under 60. As on the PS3, it
  steps its simulation twice for a frame that took two screen refreshes, so it runs at real speed from 30 to
  60 frames a second and in slow motion only below 30. (Earlier notes here said its clock counts frames;
  that is only true below 30.)
- `GT5\mods\half-rate` (off by default; turn it on in the launcher's mod list or with
  `GT5_MOD_HALF_RATE=1`): the game's own frame pacer waits two refreshes per frame. The game then draws 30
  frames a second at real speed, with about half the frames to work out.
- Frame generation (launcher, Display) now also fills the empty refresh between two such frames
  (`LD_FRAMEGEN_FILL`, default 1, 0 = as before; until now it only made the frames the Speed setting left
  undrawn). In the test race: 29 game frames and 28.6 made frames a second, about 58 on screen.
- Measured on all cores of the i9-14900KF with other programs running, the same build, one race each: mode
  on, 28.8 game frames a second, the race clock ran 49.2 s in 51 s, 4.2-5.2 cores busy late in the race;
  mode off, 46-55 frames a second (the graphics card was shared) and 5.4-7.8 cores. So roughly a third less
  CPU; one race each on a busy PC, not a precise figure.
- Mods can name their place by the code around it (`"find"`, in a list under `"*"`) instead of an address
  per release, and two mods may replace the same instruction (unlock-fps and half-rate both set the pacer's
  wait). half-rate's place is found in the European 1.00, Asian 1.00, Asian 2.01 and European 2.17
  executables; other releases get it the same way if the code is there. See MODDING.md.
- Not tested yet: long sessions, replays and menus in this mode; Vulkan (the frame fill is DirectX 12).
- One re-lift: the update regenerates and rebuilds the game code once more.

🏷️ Released as **v0.1.89**

🧮 **The game's vector and float maths, done the way RPCS3 does it: the main game thread does 17% less work (one re-lift)**
- From Whatcookie's two RPCS3 videos: do the PS3's vector instructions as whole vectors, handle the special
  cases (NaN, denormals) only when one turns up, and use the PC's byte shuffle where the PS3 shuffles bytes.
- **Vector float** in the game's own code (AltiVec): add, subtract, min, max, the compares, reciprocal,
  reciprocal square root and the int/float converts ran one lane at a time, with three NaN tests per lane.
  They now run four lanes at once, and the NaN rules run only for a vector that has a NaN in it.
  Multiply-add already did (v0.1.42); add and subtract were the next most used (3,131 places in GT5's code).
- **Byte moves:** `vperm`, `vsldoi`, `lvlx` and `lvrx` (4,404 places) were sixteen-step byte loops, and the
  sixteen one-byte stores then held up whatever read the register next. Each is now one or two byte shuffles,
  the way RPCS3 does the SPU's `shufb`.
- **Scalar float:** a result that isn't a NaN no longer has its operands checked for one: one test where
  there were three, on every add, subtract, multiply, divide and multiply-add.
- **SPU float compares** (`fceq`, `fcgt`, `fcmeq`, `fcmgt`) use the PC's own compares, and the longer exact
  form only for a vector the PC reads as holding a NaN.
- Every one gives the same bits as before: the new forms were run against the old ones on 127 million random
  and edge-case inputs with no difference (`runtime/ppu/tests/test_vmx_float4.cpp`,
  `runtime/spu/tests/test_spu_fcmp_rz.c` in ps3recomp).
- Measured: an Arcade race at High Speed Ring with the game held to 4 efficient cores of the i9-14900KF and
  other programs running, the published build and this one raced in turn, four races each. The main game
  thread used 41.7 million CPU cycles per game frame, down from 50.0 (-17%; every new race below every old
  one). The second game thread, the SPU workers and the frame rate came out within the race-to-race spread
  on that busy PC, so no frame-rate gain is claimed. Per operation the vector forms are 3 to 5 times faster.
  On all cores the new build raced at 57-58 fps with clean sound.
- This update regenerates the game code and rebuilds all of it once, so the first update to it takes about
  as long as a fresh build.
- Found on the way, not changed here: the Windows compiler (clang-cl) ignores two options the build passes
  (`-ffp-contract=off`, `-fwrapv`), which the Linux build honours.

🏷️ Released as **v0.1.88**

🧩 **RPCS3 patches that change instructions in place can become mods**
- `GT5\scripts\rpcs3_mods.py` reads RPCS3's `patch.yml` (Keduboneur78 shared one) and finds the
  patches written for exactly your copy: it works out RPCS3's ID for the game's executable from your
  `EBOOT.elf`. It lists them, says which fit, and `--add "<name>"` turns one into a mod in `GT5\mods`
  (shown in the launcher's mod list) after a relift and build. See MODDING.md.
- What fits: patches that only change instructions in place, including byte-sized edits and RPCS3's
  shared write lists. Patches that add code of their own (most long ones), change data or take a
  configurable value are left out whole: RPCS3 writes them into the game's code as it runs, and here
  that code was turned into PC code before the game starts.
- Checked on the European release: 1.00's Unlock FPS comes out identical to the port's own
  `unlock-fps` mod (and is refused as a duplicate of it), and 2.17's Unlock FPS becomes two instruction
  changes the mod builder accepts. Not yet built and played as a converted mod.

🏷️ Released as **v0.1.87**

🧰 **Installer and updates work on exFAT and FAT32 drives (#41)**
- Installed to a drive that doesn't record who owns a file (exFAT, FAT32, many USB drives), setup stopped
  with "fetching or patching ps3recomp failed": git refuses to work in a folder on such a drive ("detected
  dubious ownership"), and the first update reported it as "couldn't reach GitHub".
- The installer, `setup.ps1`, `update.ps1` and the Vulkan add-on script now tell git to trust the folders
  they work in, for their own git commands only; your own git settings are not changed. Tested on an exFAT
  drive: git refused the folder before and works in it now.
- An install that already stopped there: download **GT5-Installer.bat** again and run it. It picks up
  where it stopped.

🏷️ Released as **v0.1.86**

🚀 **Launcher: your own lines in settings.bat are kept, however and wherever you write them**
- A player's wheel mapping in `settings.bat` disappeared the next time the launcher saved (Gustingorriz).
  The launcher kept a line of the player's own only if it was written `set "NAME=value"` and sat above the
  Speed lines it adds at the end of the file when Speed is 30 fps. A line added at the very end, or one
  written `set NAME=value` without the quotes (which cmd takes just the same), was dropped.
- Now both forms are read anywhere in the file, a key set twice takes the later value as cmd does, and
  any other line of your own (a `REM` note, for example) is kept as written under "Your own lines".

🏷️ Released as **v0.1.85**

🖱️ **Mouse steering: the mouse works the menus too**
- With mouse steering on, at the menus and the pause screen the left button now presses Cross (OK), the right
  button Circle (back), and each notch of the mouse wheel moves the cursor up or down (asked for by Arpadiam).
  In races the buttons stay throttle and brake. A button already held when the menus come up (the click that
  brings the window to the front, or the throttle as a race ends) counts only after it is let go.
  `PS3_MOUSE_MENU=0` turns it off.

🏷️ Released as **v0.1.84**

⌨️ **Keyboard: one tap of an arrow key moves one place in the menus**
- In GT5's menus every arrow-key tap moved the cursor two places, on the top menu's icons, the tabs and the
  lists alike (Arpadiam, confirmed by brocolison). The arrows pressed the d-pad and pushed the left stick at the
  same time (the stick is what steers at full lock straight away in a race), and GT5's menus move one place
  for each: measured at the top menu and the track list, the d-pad alone moves one place, the stick alone one,
  both together two.
- Now Left/Right push the stick only and Up/Down press the d-pad only: one place a tap in the menus, and
  steering with the arrows in a race is as immediate and full as before. With mouse steering (or a wheel) on,
  the game itself says when it is at its menus: there all four arrows press the d-pad, in a race the d-pad and
  the stick, as before. `PS3_KEYS_ARROWS=split`, `dpad` or `both` picks one way for good.

🎮 **The keyboard works with a pad connected**
- With a pad on, the keyboard did nothing (Arpadiam). Now both work together: the buttons of both count, and
  each stick and trigger takes whichever is pushed further. Steam's desktop layout turns pad buttons into keys
  while it runs (Cross into Enter, which is Start here), so keys that go down within 60 ms of a pad button are
  taken for that and left out until they are released. `PS3_KEYS_WITH_PAD=0` turns the keyboard off while a pad
  is connected, as before.

🖱️ **Finer mouse steering speed**
- `PS3_MOUSE_STEER_SENS` takes decimals and goes down to 1 (it stopped at 10 before): 2.5 is forty times slower
  than normal (asked for by Arpadiam). The launcher's Controls page has a number box for it (1 to 1000, steps of
  0.5, the arrow keys or the mouse wheel step it) instead of the six choices. Settings you already have still work.

🏷️ Released as **v0.1.83**

🩹 **Build fix for v0.1.82**
- v0.1.82 named the new `[threads]` source file (`GT5\src\gt5_threadstats.cpp`) in the build but the release
  did not carry the file, so building v0.1.82 stopped with "Cannot find source file". This release has it.

🏷️ Released as **v0.1.82**

🧵 **A per-thread CPU line in the log**
- `logs\play.err` now gets a `[threads]` line every 5 seconds saying how busy each kind of thread was: the
  game's own threads, its SPU jobs, the drawing threads (`rsx walker`, which turns the game's drawing
  commands into DirectX 12 or Vulkan work, and `rsx record`) and sound, where 100 means one core busy the
  whole time. A single thread near 100 is the one holding the frame rate back, so a log now says which part
  of the PC is the limit. Asked for on Discord by Keduboneur78.
- `PS3_THREAD_STATS=0` turns it off; a number sets another period in seconds (1 to 60).

🏎️ **DirectX 12: the two drawing threads stop fighting over one cache line**
- The thread that walks GT5's drawing commands hands every DirectX 12 call (about nine a draw, 7,000-23,000 a
  frame in a race) to a second thread that records them. Both threads' positions in that queue, the walker's
  own counter and the sleep flag shared one 64-byte cache line, and each side read the other's position with
  a locked instruction on every call and on every spin while waiting, so each call moved that line between
  the two cores two or three times. Now the shared positions sit on lines of their own, the walker looks at
  the recording thread's position only when the queue is full, and the recording thread waits with plain
  reads. Nothing is drawn differently or in another order.
- Measured on this PC in two Arcade races, switching the change on and off every 15-20 s in the same race:
  the walker's time per handed-over call **136 -> 108 ns and 132 -> 107 ns** (-20%), the recording thread's
  CPU 45% -> 40% of a core. Race frame rate in the windows with it on: 33.7 -> 40.5 and 28.9 -> 31.7 fps
  (+13% +- 8% compared with the neighbouring windows in the second race); the busy PC's own swings are large,
  so take the frame rate as "not worse, probably a little better". On a heavy grid like AryaTwirl's (#10),
  where the walker hands over ~23,000 calls a frame, that is about 0.6 ms of the walker's ~30 ms.
- `LD_REC_FAST=0` goes back to the old way.

🏷️ Released as **v0.1.81**

🗂️ **Vulkan: the start-up shader list keeps what you play, and a full one no longer stalls the start**
- Since v0.1.77, Vulkan builds the shaders earlier sessions used while the game starts (`vk_pipelines.lst` in
  the shader cache). The list stopped at 8,192 entries, so once it was full (a player's was) nothing new was
  ever added: the start-up builder kept making what was seen first, and new cars and tracks were built from
  scratch every time. Finding an entry in a full list also re-hashed every entry: measured here with a full
  list, the first race shader froze the renderer for **11.3 s**, and looking shaders up cost **0.75 s** more
  per session.
- Now the list keeps the most recently used shaders: one used again moves to the newest end, the oldest
  unused ones drop off at 8,192, and the start-up builder makes the newest first. Repeats of the same Vulkan
  shader under different GCM settings count once. Same file format: an existing list is converted at the
  next start. Measured with the same full list: read in **31 ms** instead of 11.3 s, lookups **1-4 ms** a
  session instead of 747 ms, and **all 905** new shaders of a session added (before: 56 of 1,375); the next
  start had 537 of them ready before the game asked.
- `VK_PB_MRU=0` = the old list. `GCM_DRAIN_STAT=1` also names the images Vulkan takes from its pool most.
  Windows tested; Linux/Steam Deck (where the start-up list is opt-in, `VK_PIPELINE_PREBUILD=1`) only
  compile-checked.

🏷️ Released as **v0.1.80**

🧱 **Vulkan: new scenes no longer stay half-drawn for a minute on slow PCs** ([#38](https://github.com/MormonFTP-hub/gt5-recomp/issues/38), [#20](https://github.com/MormonFTP-hub/gt5-recomp/issues/20))
- On Vulkan, the shaders a new scene needs (a race start, another car) are built in the background, and
  anything whose shader isn't ready yet is left out of the picture until it is. On a 4-core laptop with
  Intel graphics that took one to two minutes: black sky, missing track or crowd, 25,000-57,000 objects
  left out every 5 seconds. Two builders at normal priority got a small share of a busy CPU, and Intel's
  driver needs about 25 ms of CPU or more per shader.
- Now:
  - **The same shader is built once.** GT5 asked for a third of its shaders two or more times under
    different settings that make no difference to Vulkan (blending factors with blending off, and so on):
    in one session 1,463 asked for, 967 different. The repeats now share the first one.
  - **The builders run above normal priority**, and there are half as many as the CPUs the game may use
    (an 8-thread laptop gets 4 instead of 2). They only run while something is waiting to be built.
  - **What the picture on screen needs is built first**, not leftovers from a loading screen.
  - **A shader still missing after 1.5 s is built by the renderer itself**, one a frame and at most a
    quarter of its time, so nothing stays missing however busy the PC is.
  - Shaders the start-up builder (v0.1.77) hadn't reached when the game asked for them aren't built twice.
  - The driver's compiled shaders are kept on disk too (`vk_pipeline_cache.bin` beside the shader cache),
    so the start-up rebuild of everything seen before is quick even with a driver that keeps no cache of
    its own. NVIDIA keeps one anyway; Intel and AMD untested here.
- Measured on this PC's 4 slowest cores with every shader build made to cost 100 ms (like a slow driver),
  from an empty shader cache, Arcade to a race: the longest stretch with things missing went from **61 s
  to 18 s**, and the objects left out over the whole session from **137,200 to 4,807**. Race frame rate
  afterwards unchanged (8.6-10.2 before, 8.9-10.9 after). At full speed on all cores the race starts
  behaved the same as before (2,468 and 2,500 left out over a session, gone within 1-4 s).
- 🪟 **Vulkan window back after minimising:** a minimised Vulkan window waits for the window to come back,
  and that was judged from SDL's "minimised" flag. A player's window came back black and stayed black
  with the game running on. Now the renderer asks the window's Vulkan surface whether it has a size again.
  Minimised and restored here: drawn again at once.
- `VK_SHADER_BOOST=0` goes back to the old way; `VK_SHADER_LOG=1` puts a line a second in `play.err`
  (`[vkq]`: draws, left out, queued, built, waiting); `VK_PIPELINE_DISK_CACHE=0` turns the disk cache off;
  `VK_WIN_SDL_FLAG=1` is the old window test. The Vulkan renderer is the only one on Linux, so this is for
  Linux too (except the priority, which Linux doesn't let a game raise); Linux builds untested this time.

🏷️ Released as **v0.1.79**

🧰 **Installer: a Visual Studio without the Windows SDK gets it added, instead of a failed build (#35)**
- A Visual Studio with the Clang tools but no Windows SDK stopped the build at CMake's first test with
  `could not open 'kernel32.lib'`. The installer now checks for the SDK as well and adds "Windows 11
  SDK" to that Visual Studio, with the C++ workload of its own edition (Community, Professional and
  Enterprise were asked for the Build Tools' one before). `setup.ps1` reports a missing SDK, and
  `build.cmd` says what to add, or uses get-toolchain.ps1's tools when they're there.

🏷️ Released as **v0.1.78**

🖼️ **Texture dumping and HD texture packs (DirectX 12)**
- `PS3_TEX_DUMP=1` (or the new tick box on the launcher's Display page) writes every texture the game
  shows to `GT5\textures\dump\`, once, on a background thread. DXT textures (nearly everything on the
  tracks and cars) come out as DDS holding the game's own compressed data, nothing lost; the rest
  (menus, HUD) as PNG. Each name starts with a 16-digit hash of the texture's pixels, so a texture has
  the same name on every PC and wherever GT5 keeps it in video memory (it moves textures around and
  keeps a copy per car). What the game keeps rewriting (movies, animated backgrounds), render targets,
  cube maps and 3D textures are left out.
- A file in `GT5\textures\replace\` (sub-folders are fine) whose name starts with that hash is used
  instead of the game's texture, at any size: PNG, or DDS in BC1/DXT1, BC3/DXT5, BC7 or 32-bit RGBA,
  with the file's own mipmaps or ones made when it loads. Files load in the background the first time
  their texture appears and stay for the session. `PS3_TEX_REPLACE=0` turns them off, `PS3_TEX_DIR`
  points at another folder. How-to, names and formats: [MODDING.md](MODDING.md#2-textures-and-hd-texture-packs).
  Dumped textures come off the game disc: share only your own work, never the dumps, and never on this
  project's GitHub.
- Tested in Arcade races. One run from the menus into a High Speed Ring race wrote 588 textures
  (1,392 files over the test runs, 160 MB, every one opens in an image viewer). The track's billboard sheet (1024x1024 DXT5) swapped for a recoloured
  4096x4096 PNG showed on the billboards at once, beside a 2048x2048 RGBA DDS with its own 12 mipmaps
  and a same-size BC3 DDS without mipmaps. With all 335 race textures replaced by tinted copies, the
  whole race came out tinted, files reused for each car's copy (one file at up to 5 addresses). At
  Eiger, with 452 tinted files, nothing on the track kept its own colours, and a 2048x2048 texture that
  turned up at a second address during the track load got its file there too.
- Costs: with no `replace` folder and dumping off nothing is hashed; old and new build alternated on
  the same busy PC gave 37.6 / 51.2 and 53.1 / 29.3 race fps, a spread between runs of one build
  larger than any difference. Dumping or replacing hashes each texture the game decodes: about 0.2 s
  in the first minute (start-up and menus), then under 0.5 ms per 10 s in a race (1.3 ms during the
  track load). 178 files loaded in 0.4 s on the background thread; no slow frame when the 4096x4096
  file reached the graphics card.
- Not yet: the Vulkan renderer (and so Linux), cube maps, and a new size for the few textures the game
  reads by pixel (`_rect` in the name: replace those at their own size).

🏷️ Released as **v0.1.77**

🌗 **Vulkan: races no longer washed out (or too dark)** ([#38](https://github.com/MormonFTP-hub/gt5-recomp/issues/38), [#20](https://github.com/MormonFTP-hub/gt5-recomp/issues/20))
- GT5 sets its exposure from a tiny copy of the race it makes on the graphics chip: it shrinks the
  picture into a corner of one big target, halves that twice in place, and reads the 16x8 result back.
  To halve in place, the port gives each step a copy of the part it reads, and on Vulkan that copy came
  from the first surface in its list covering that memory. An older, taller surface left from an
  earlier screen covered it too (the memory is reused), so each step was made from a frozen patch of
  that old screen, and the game set its exposure for whatever the patch showed: long washed-out white
  stretches, or a race too dark. Whether that old surface was still around depended on what had been
  shown since the game started, so some sessions were fine and others not. It's the mistake DirectX 12
  had in v0.1.46, in another place.
- Now the copy comes from the closest surface (the one being drawn). In one race, switching between the
  old and new way every 20 s: the 16x8 sample the game reads averaged **0.6 (black) the old way and 92
  the new way** (45-163, following the scene); the old way's stretches came out white, the new way's
  looked right once the game's exposure caught up (it adapts over a few seconds).
- The Vulkan renderer is the only one on Linux, so this is for Linux too. `VK_WIN_FIRST=1` goes back to
  the old way. DirectX 12 doesn't use this code. `VK_RB_LOG=10` puts the sample the game reads into
  `play.err` (`[rb-meter]` lines, alpha 0 = the exposure sample), like `LD_RB_LOG` on DirectX 12.

⚡ **Vulkan: small readbacks don't wait any more, and earlier sessions' shaders are ready at start** ([#38](https://github.com/MormonFTP-hub/gt5-recomp/issues/38))
- The small copies GT5 reads back every frame (the exposure sample above and its brightness chain) land a
  frame later instead of stopping the command thread until the graphics chip is idle, as on DirectX 12
  (`VK_RB_ASYNC`, on by default on Windows now). With the exposure fixed, switching it every 20 s kept the
  same picture (frames 94 vs 83 bright on average, 4% over 180 either way) at **26 instead of 15 game
  frames a second** on a busy PC (+12% on a quiet one). `VK_RB_ASYNC=0` turns it off.
- Every Vulkan pipeline made is now noted in `vk_pipelines.lst` beside the shader cache, and at the next
  start a background thread builds them all again (about 1,800 here, done in seconds with a warm cache),
  so what you've seen before doesn't skip objects while its shaders build. The same session with and
  without the list: **322 instead of 1,813 new pipelines, 479 instead of 2,790 draws skipped**, a race
  start 295 instead of ~950. A race whose shaders aren't in the list yet still builds them as it goes,
  so it gets better the more you play. On by default on Windows; `VK_PIPELINE_PREBUILD=0` turns it off
  (`=1` turns both on on Linux).
- Tried and left out: keeping two frames in flight at the present (31.3 vs 30.7 game frames a second,
  noise: the wait there is for the command batch two back, not the frame before), and filtering GT5's
  scaled 2D copies as the PS3 does (no change to the exposure).

🏷️ Released as **v0.1.76**

🐧 **Linux: your own keyboard keys, and the small-CPU setting from Windows**
- `PS3_KEYS` works on Linux too, with the same buttons, key names and rules as on Windows: put
  `export PS3_KEYS="cross:W,square:Space,l2:Q,r2:E,circle:LAlt"` in `GT5/settings.sh` (play.sh lists the
  buttons and keys). Letters are where they are on a US keyboard. `PS3_KEYS_TEST_FILE` and `PS3_KEYS_TRACE`
  work as on Windows.
- The v0.1.65 setting that lets waiting threads sleep instead of spinning now counts a PC with up to 12
  logical CPUs as small on Linux too (it was 8 there), with the same `PS3_SMALL_HOST_CPUS` to move the line.
  It counts the CPUs the game may use, so `taskset` counts, and `play.err` says which way it went.
- Checked in an Ubuntu 24.04 build: with `PS3_KEYS=cross:W`, W held for 2 s reached the game as Cross
  (`[pad] ... cross=255`), and W no longer pressed R1. 6 CPUs counted as small, and as big with
  `PS3_SMALL_HOST_CPUS=4`. The Windows build of both files is the same as before.

🏷️ Released as **v0.1.75**

🎥 **Cockpit view: a wider field of view and the seat moved** ([#36](https://github.com/MormonFTP-hub/gt5-recomp/issues/36), asked for by Arpadiam)
- New, and nothing changes unless you set it: the launcher's **Display** page has a **Cockpit view** group.
  **Field of view** widens (or narrows) the view from the driver's seat; **Seat forward / back** and **Seat
  height** move your eye from where the game puts the driver. In `play.bat`: `PS3_COCKPIT_FOV` (degrees
  added), `PS3_COCKPIT_SEAT` (cm forward, negative = back) and `PS3_COCKPIT_HEIGHT` (cm up).
- GT5 shows every car's cockpit at 52.4° from the top of the screen to the bottom (the PS3 had no setting
  for it). +20 makes it 72.4°: the whole steering wheel, the driver's hands and more of the side windows come
  into view. Only the cockpit view changes; the chase, bumper and roof views and the menus stay as they are.
- How: GT5 builds its race cameras out of small camera objects. The port now runs the game's own in-car
  camera and then moves the eye along the camera's own axes and widens the field of view the game gives
  its in-car views. The camera is found by its name in the game's code, not at a fixed address, so other
  releases get it too: the European 2.17 update, the Asian disc and 2.01 have the same camera code.
- Tested in Arcade races on the European disc (Fiat 500): with the settings at 0 the cockpit looks as
  before, at +20° / 10 cm forward / 10 cm up it's wider, closer and higher, and the bumper and chase views
  match the unchanged game. The 2.17 build started with it and found the in-car camera; a race on 2.17
  wasn't reached in the test (its test save stops at the data-install question first).

🏷️ Released as **v0.1.74**

⌨️ **Choose your own keyboard keys** (asked for by Arpadiam on Discord)
- New, and nothing changes unless you set it: the launcher's **Controls** page has a **Keyboard keys** list,
  one row per PS3 button, each labelled with what that button does in GT5. Click a row and press a key (Esc
  cancels); right-click a row to give it a second key, **×** clears it, **Reset to defaults** brings the old
  keys back. A key that's already used moves to the row you picked. Or in `GT5\settings.bat` / `play.bat`:
  `set "PS3_KEYS=cross:W,square:Space,l2:Q,r2:E,circle:LAlt"` (W gas, Space brake, Q / E shift down / up,
  Left Alt handbrake). Every button (the D-pad and the sticks' directions too) and key name (letters,
  digits, F1-F24, Space, the Shift/Ctrl/Alt keys left and right, arrows, Insert...PageDown, the number pad,
  punctuation by name, Mouse4/Mouse5) is listed under [Your own keys](README.md#keyboard-keys).
- What each row does was read from the game's own **Options > Hardware > Change Button Assignments**
  screen (its defaults): Cross accelerate, Square brake, **R2 / L2 shift up / down**, Circle handbrake,
  Triangle reverse, L1 rear view, R1 flash and high/low beam, L3 horn, R3 windscreen wipers, Select change
  view, Start pause, D-pad up ghost, right stick up/down accelerate/brake. Left View, Right View and Flash
  Lights have no pad button there by default: give one a free button in the game (the right stick's
  left/right, D-pad down), then bind a key to that button.
- A key you bind stops doing its old job (with W as Cross, W no longer works the headlights), Esc stays the
  quit key, and with Alt or F10 bound the game window no longer opens its window menu or beeps on them
  (Alt+Tab and Alt+F4 still work). Windows only for now.
- Works the same with **mouse steering** on: GT5 takes the pad's buttons alongside the steering wheel the
  mouse plays (checked with the pad's own buttons first: shift down, change view, rear view and pause all
  worked with the wheel attached), so the keys don't need the wheel's buttons.
- Tested in Arcade races at High Speed Ring with the manual gearbox, keys held from a test file
  (`PS3_KEYS_TEST_FILE`; the real keyboard is never read then), with
  `cross:W,square:Space,l2:Q,r2:E,circle:LAlt,start:P,select:C,l1:B`, once with mouse steering off and once
  on: Space braked the Fiat 500 from 30-32 mph to a stop, Q shifted 4th down to 1st one gear at a time, W
  took it from a standstill to 10-13 mph (it's a slow car), E shifted 1st to 2nd, Left Alt lit the
  handbrake light and held the car, C went to the cockpit view and then the bumper view, B looked back,
  P paused and P went on again. The game's pad log showed each key arriving as its button (W as Cross at
  full pressure, Q as L2, and so on). A third race with `PS3_KEYS` unset did the same with the old keys
  (A, 1, Z, 2, X, Tab, Q, Enter). Alt, Alt+W, F10 and the menu command sent to the game window left it out
  of menu mode.

🏷️ Released as **v0.1.73**

🌋 **Vulkan: frame generation, even frame pacing, and a cheaper command thread**
([#38](https://github.com/MormonFTP-hub/gt5-recomp/issues/38))
- **Frame generation now works on Vulkan too.** With the launcher's **Frame generation** ticked
  (`LD_FRAMEGEN=1`) and Speed 30 fps or watch mode, Vulkan makes the in-between frames the way DirectX 12
  does (the same motion search, run as Vulkan compute shaders) and a thread of its own presents them.
  Measured in a race, switching it on and off every 20 s: **57 new pictures a second, 16.6 ms apart**
  (85-89% of them 12-21 ms apart) against 26 a second, 33-36 ms apart, with frame generation off. The
  game's own speed didn't change (54 vs 52 frames a second, within noise).
- Even without the made frames, presenting moved off the command thread: it never waits for the screen.
  On a screen faster than 75 Hz the presents are spaced by the game's frame time.
- **The command thread (the one that walks the game's GPU commands) got cheaper**, on by default:
  - Images are reused instead of made and freed. GT5 made and freed about 900 images a second in a race
    (textures it rewrites, feedback copies, depth read as colour), and on Windows each one is a trip
    into the kernel. Alternating it every 20 s: **43 against 37 game frames a second** (+17%), the
    command thread busy 620 instead of 716 ms of every second. `VK_IMAGE_POOL=0` turns it off.
  - Converted car and track geometry is kept between frames, as DirectX 12 does (`LD_VCACHE`'s idea:
    reused while Windows says its memory is unwritten), and the 8 KB of vertex constants is copied once a
    draw instead of three times. Together: the command thread **402 instead of 484 ms** of every second.
    `VK_VCACHE=0` and `VK_CONST_DIRECT=0` turn them off.
- Tried and left out: a Vulkan pipeline cache kept on disk didn't reduce the draws skipped while new
  shaders build at a race start (450-720 skipped with it, 400-570 without), so it isn't in. Small
  readbacks without a wait (`VK_RB_ASYNC=1`, DirectX 12 does the same) gained 12% but feed GT5's
  auto-exposure, which needs more checking first: it's in, but off.
- Measured on the test PC with other games running, 1080p mode, Speed 30 fps, Arcade races. Vulkan stays
  experimental on Windows and DirectX 12 the recommended renderer.

🏷️ Released as **v0.1.72**

🖱️ **Steer with the mouse** ([#37](https://github.com/MormonFTP-hub/gt5-recomp/issues/37), asked for by Arpadiam)
- New, and off unless you turn it on: **Steer with the mouse** on the launcher's Controls page (or
  `set "PS3_MOUSE_STEER=1"` in `play.bat`). In races, move the mouse left and right to steer; **left button
  = throttle, right button = brake, middle button = straighten the wheel**. The keyboard and pads keep
  working alongside (Z still accelerates).
- The mouse plays the emulated Logitech G27 that real wheels use, so GT5 steers straight from it, with the
  wheel's fine steering (16,384 steps from lock to lock). Feeding the pad's left stick instead was tried
  too: it steers the car as well, but with 256 steps, and a stick held off-centre moves GT5's menu cursor.
- Only in races. At the menus and while paused (when GT5 plays the wheel's menu spring) the mouse
  steers, presses and moves nothing and the cursor is yours, so the menus work as before with the
  keyboard or a pad. While it steers, the cursor is hidden and kept inside the window, so a click can't
  land on another program mid-corner; Alt+Tab frees it at any time.
- Two settings: **Mouse steering speed** (`PS3_MOUSE_STEER_SENS`, percent; at 100, mouse movement about
  the width of a 1080p screen turns the wheel from straight ahead to full lock. The mouse is read raw, so
  Windows' pointer speed and acceleration don't change it) and **Wheel straightens by itself**
  (`PS3_MOUSE_STEER_CENTER`, off by default: once the mouse stops, the wheel goes back to straight ahead).
- A real wheel isn't used while mouse steering is on. Tested with a virtual mouse in Arcade races (the
  real mouse can't be moved here): GT5's own wheel driver read the steering step by step as the mouse
  moved, the left button took a Fiat 500 from standstill to 44 mph and the car turned with the mouse, and
  pausing with Start made the mouse do nothing until the race went on. At the menus, a mouse moving and
  clicking every 3 seconds changed nothing the game read, and the menus were driven to a race as usual.

## 15 Nov 2026

🏷️ Released as **v0.1.71**

🖥️ **Vulkan draws at 1080p in the 1080p mode, and its window title shows FPS and the resolution**
([#38](https://github.com/MormonFTP-hub/gt5-recomp/issues/38))
- In the 1080p mode (launcher: Resolution 1080p) the game draws 1920x1080 frames, and the DirectX 12
  renderer shows them at that size. The Vulkan renderer copied each one into a 1280x720 target first, then
  stretched that to the window, so on Vulkan the 1080p mode looked like 720p. Its target is now the video
  mode's size (1920x1080; wider on an ultrawide screen). The window still opens at the same size as before.
- The Vulkan window's title now shows what the DirectX 12 one does: `FPS: 29.97 (game 60) | 1920x1080`.
  FPS is frames drawn a second. The game's own rate is added when the 30 fps Speed setting or watch mode
  leaves frames undrawn, since that's the rate the game runs at.
- The DirectX 12 title said `1280x720` in the 1080p mode too: it gave the window's starting size, not the
  size frames are drawn at. It now says `1920x1080`.

🏷️ Released as **v0.1.70**

🏁 **Update now opens an update window instead of a console**
([#39](https://github.com/MormonFTP-hub/gt5-recomp/issues/39), ideas from Arpadiam, brocolison and wurtexxd-dot)
- The launcher's **Update now** used to open `update.ps1` in a black console window full of build text. It
  now opens a window of its own: your newest Photo Mode pictures behind it, changing every few seconds (the
  GT logo from your disc when you have none), the current step in plain words, and a car that drives along
  the progress bar. The bar follows the real work: the build's own count of steps ("1,234 of 2,000 steps"),
  and a slow creep through the steps that print no count (getting the new version, a re-lift).
- **Details** shows the full text. Everything also goes to `GT5\logs\update.log` (the one before is kept as
  `update.prev.log`). If the update stops, the window stays open with the reason, the text and a button
  that opens the launcher; when it finishes, it opens the new launcher by itself.
- How: the launcher has to close so the build can replace `GT5Launcher.exe`, so it starts a copy of itself
  (`GT5Updater.exe` in your Temp folder) as the window, which runs `update.ps1` without a console.
  `Update.bat` still runs the console version.
- This version's own update still runs in the console, because your current launcher starts it. The
  window comes with the next one.

🩺 **Crash reports name the DLL a crash happened in**
([#40](https://github.com/MormonFTP-hub/gt5-recomp/issues/40), brocolison: Vulkan closes at its first frame on Intel Iris Xe)
- `play.err`'s `[CRASH]` block gave a crash's place as an offset in "the module" without saying which
  one. In #40 the offset was the same in v0.1.60 and v0.1.64, so it wasn't gt5.exe, but the graphics
  driver, the Vulkan loader and SDL2 were all possible.
- The block now names the module and its file, the thread that crashed (the runtime names its threads),
  and the whole call stack from the crash on as `module+offset`. A crash inside a driver then also shows
  the first gt5.exe frame that called into it.
- `PS3_CRASH_TEST=dll` (or `=exe`) in `settings.bat` crashes on purpose at start, to see the report.

🏷️ Released as **v0.1.69**

🎮 **Steering wheels: Thrustmaster pedals the right way round, a button as a pedal, reverse on a button**
- Thrustmaster wheels (a player's T-GT, on Discord): the accelerator braked and the brake accelerated, because
  every wheel got Logitech's pedal layout. Thrustmaster wheels now get Thrustmaster's own: accelerator on Rz,
  brake on Y, clutch on the slider.
- In the Thrustmaster control panel's "combined" pedal mode both pedals share the Y axis, and the brake read
  half pressed all the time. A throttle axis and a brake axis that both rest in the middle aren't two pedals,
  so the port now splits Y, as it already did for wheels like the Genius Speed Wheel 3.
- The pedals' rest points were taken from the port's very first reading of the wheel, which comes before the
  wheel has sent anything: the T-GT's log showed every axis in the middle. The port now waits for the wheel's
  first real reading; until then the pedals count as released. A clutch axis resting in the middle is left
  out (half a clutch would slip in a manual car).
- A button can be a pedal, for a wheel with no pedals plugged in or a broken one:
  `set "PS3_WHEEL_BUTTONS=brake=4"` brakes fully while button 4 is held (`throttle=` and `clutch=` too).
- Reverse on a wheel without an H-shifter: `gearr=6` in the same line makes button 6 the shifter's R while it
  is held. GT5 takes it with the **manual gearbox (MT)**: hold it and accelerate to back up, then a shift-up
  paddle puts you in 1st again. With the automatic gearbox GT5 ignores the shifter, even one attached from the
  start, so automatic has no reverse on a wheel.
- Checked without a wheel: a desk test of the T-GT in both pedal modes and of the Genius's split axis, and
  races with a simulated wheel: button 4 stopped the car from 56 mph; button 6 with throttle backed up at
  10 mph in MT (R on the dashboard) and did nothing in automatic. The T-GT's buttons still follow the G29's
  numbers: if one is wrong, `PS3_WHEEL_BUTTONS` moves it (see the README's wheel section).

🏷️ Released as **v0.1.68**

🔊 **Sound on busy and small CPUs: no more 85 ms holes, and the sound no longer runs late** (the "audio lag"
reported on Discord with 2.11; it shows on any version once every core is busy)
- GT5 makes its sound in blocks of 5.33 ms on one thread (a chain of 13 SPU jobs) and keeps two blocks ready
  ahead of the sound card. When that thread lost the CPU for longer than that, the port played the missing block
  as silence and moved on. GT5's late blocks then landed behind it: the port played the whole 85 ms ring empty
  before it came round to them, and from then on everything sounded up to 80 ms late, until the next hole.
- Two changes, no re-lift:
  - The thread that runs the sound chain now runs above normal priority, so the SPU workers and waiting
    threads, all at normal priority, no longer hold it up when every core is busy.
  - When a block is still late, the port waits for it (that 5.33 ms plays silence, at most 16 in a row)
    instead of reading past it, so GT5 stays exactly two blocks ahead and nothing plays late.
- Measured on 4 efficient cores of the test PC with another game running, Arcade races at 6-12 fps, each change
  switched on and off every 10 s in one race:
  - priority: silent periods in the race 35% → 6% (in the menus too: 253 → 64 dropouts a minute);
  - waiting for late blocks, priority on in both halves: silence 7.6% → 2.1%, 85 ms holes 51 → 0, and the time
    the sound ran late (GT5 five or more blocks ahead, 27-85 ms) 32% → about 1%;
  - the frame rate was the same both ways.
- `SPURS_JC_PRIO=0` and `AUDIO_HOLD_MAX=0` in `settings.bat` bring back the old behaviour. When the port had to
  wait for sound, `play.err` says so once a minute (`[audio] waited for late sound blocks ...`).
- Also new in `play.err`: GT5's sound thread stops listening for the sound clock after a second without it, and
  its sound then comes in one-second bursts for the rest of the session. If that happens, a line now says when
  the clock last reached it. If you see `notify queue ... removed` in your log, please send it.

🏷️ Released as **v0.1.67**

🧰 **Launcher: lines you add to `settings.bat` by hand are kept**
- The launcher stays open in the background while the game runs and saves `GT5\settings.bat` each time
  you press Play. It saved the copy it had read when it started, so a line added by hand meanwhile, like
  a wheel's `set "PS3_WHEEL_PEDALS=throttle=B,brake=A"`, was gone again on the next Play (Discord, Thrustmaster TGT
  test report).
- It now reads the file again before saving. A setting you changed in the launcher keeps the launcher's
  value, everything else keeps what the file says, and a line of your own that you delete stays deleted.
  Lines that aren't `set "NAME=value"` are still dropped.
- Put your own settings in `settings.bat`, not in `play.bat`: updates replace `play.bat`.

🏷️ Released as **v0.1.66**

🔊 **The check that a job's code is still the same costs half as much (a little less CPU for the sound on small CPUs)**
- Every time an SPU job starts, the port makes sure the job's code is still the code it translated, by
  comparing it byte for byte with a copy: about 2,200 times a second in a race, 33.6 KB each time, most of
  them on the thread that runs the sound chain, the one thread that runs out of time on 4-core laptops.
- It now compares a 128-bit CRC32C of the code (four streams at once) with the CRC of that copy, which
  reads the code once instead of reading it and the copy. On 4 efficient cores of the test PC, switching
  between the two every 10 s in one race: 6.4-7.7 µs per check with the old compare, 3.3-4.5 µs with the
  CRC, about 7 ms of CPU a second less. Too small to show in the frame rate; the sound is unchanged.
- `SPU_FP_CHECK=memcmp` brings back the byte compare; `SPU_FP_STATS=1` logs what the checks cost.

🏷️ Released as **v0.1.65**

⚙️ **6-core/12-thread CPUs: the game's waiting threads sleep instead of spinning, 5-14% less CPU at the same frame rate**
- Since v0.1.28 and v0.1.41, on a PC with 8 logical CPUs or fewer the game's waiting threads nap or sleep
  instead of spinning on a core: a PPU thread waiting for an SPU result, the command-buffer walker and the
  DirectX 12 recording thread. The 6-core/12-thread CPUs most player reports come from (Ryzen 5 3600 and
  5600X, Core i5-10400F) still spun.
- They now get the same treatment. On the test PC's 6 hyper-threaded cores, switching it on and off every
  10 s during a race: the same game frame rate (−0.3% ± 2.8% and +0.4% ± 1.7% in two races) with 5% and 14%
  less CPU (7.7 → 7.1 and 7.4 → 6.5 cores busy). With 16 threads the difference was within the noise, so
  CPUs with more threads keep spinning.
- `PS3_SMALL_HOST_CPUS=<n>` in `play.bat` sets the limit (8 before); `PS3_YIELD_NAP=0` turns it all off.
  Windows only: the Linux build keeps its own 8-CPU test until it is measured there.
- Tried and left out: waits shorter than Windows' half-millisecond timer tick with the CPU's
  `umwait`/`tpause` (27% fewer frames on 4 small cores, where the waiting thread has to give its core back;
  no gain on 12 threads), and building the runtime with the game code's AVX2 instructions and with ThinLTO
  (no measurable change in two rounds of races).

🏷️ Released as **v0.1.64**

🐧 **Linux and the Vulkan renderer: races start at the right brightness, and car reflections and shadows are drawn in full**
([#20](https://github.com/MormonFTP-hub/gt5-recomp/issues/20), melroy89: scenes sometimes far too dark until the
licence test was restarted, flicker, and "rejected colour feedback" lines in the logs; Radeon RX 7900 XTX)
- Brightness: GT5 sets its exposure from the scene itself. It shrinks each frame step by step and reads the
  smallest copy back. Since v0.1.49 new shaders are built in the background, and a draw whose shader isn't
  ready yet is left out. At a scene's first frames, while most of its shaders were new, that included those
  shrinking steps, so GT5 measured a leftover image and opened the race far too dark (or too bright). Now only
  the scene's own objects wait for their shaders; full-screen steps like these, and the HUD, get theirs
  straight away (a few dozen per new scene, so no freezes come back). Checked on the Windows Vulkan test copy
  from an empty shader cache, Tokyo R246 in Arcade: at the start line the old way showed a near-black road
  under a deep blue sky, the new way the right picture. Here the old way recovered within a few seconds. If a
  scene still stays dark for longer, `play.err` now says which image each brightness reading comes from.
- Car reflections: each of the six sides of the reflection map gets its smaller, blurrier copies drawn in the
  rows below it, in the same image. The renderer refused the first of those draws, because it reads the side
  while drawing into the same image. So that level was never drawn, and every smaller one was made from it.
  Those refusals were the "rejected colour feedback" lines in melroy89's logs. That draw now reads a copy of
  the side.
- Shadows: GT5's shadow pass also reads a 1x1 texture at its own image's address. Those draws were refused
  too, hundreds a second already at the top menu, so objects were missing from the shadow map. They're drawn
  now.
- The flicker itself didn't show here: an NVIDIA card with the same renderer, presenting every frame the way
  the Linux build does, at the top menu, before a Tokyo R246 race and in it. Vulkan's validation layer, with
  its synchronisation checks on, found no errors from boot to a race. The reflection and shadow fixes change
  what is drawn every frame, so they may help with it; that is untested on AMD.

🏷️ Released as **v0.1.63**

⚡ **Less CPU for every frame: the Steam Deck work from a community pull request** (by AlexWooz, #33;
one re-lift of the game's code and SPU jobs, done by `update.ps1`)
- The pull request's six changes, measured on Linux by AlexWooz (a Steam Deck, about 4%: 109.5 to 105.2 ms
  a frame with the race paused on the grid) and here on Windows. Each has its own entry below. Five are on;
  the sixth, keeping PPU registers in host locals, is in but off.
- Windows, the tested rig limited to 4 of its efficient cores, with the same race paused on the grid,
  runs of the old and new builds back to back: **8% and 17% fewer** CPU cycles a frame in the two clean
  pairs (1,620 to 1,484 million, and 1,747 to 1,443), most of it in the SPU job threads. Another game and
  other test builds were running on the PC the whole time, so the frame rate moved too much between runs
  of the same build (4 to 12 fps) to show a gain of this size.

🧩 **Two kinds of SPU code no longer run in the interpreter** (#33, AlexWooz)
- A jump table placed right after its `bi`, with the table's address held in a register
  (position-independent code), had no `il`/`ila` naming it, so its handlers were never lifted. The Wws
  job at `0x015DF100` is a bytecode machine dispatched this way: 2 KB of handlers ran in the
  interpreter on every dispatch. `find_spu_functions.py` now tries the words after every `bi` as a
  table (three or more small offsets landing on instructions).
- A function that starts right after another's `bi` and is only called through a pointer kept in data
  was folded into the function before it, so every call missed the lookup. The instruction after an
  unconditional `bi` is now an entry.
- 38 Wws jobs: 27,624 to 27,910 entries, 9 KB more code lifted in five jobs. In a race on the Steam
  Deck the interpreter went from 6-8 million steps a second to 0.8 million (`SPU_INTERP_STATS=5`).

🔀 **The SPU dispatch cache outlives a job's code being loaded again** (#33, AlexWooz)
- The cache of resolved indirect-branch targets was keyed by a counter bumped on every change to a
  context's resident code, and a job manager loads a job's code for every job it runs: each short job
  resolved its branch targets from nothing, through the registry walks (`spu_lifted_lookup_full`,
  `spu_lookup`: about 4% of each of GT5's four graphics workers in a race).
- The key is now a 64-bit hash of what is resident (each slot's span, image and verify state), so a
  job's entries are still valid the next time it is loaded. 4,096 entries a thread, from 512, and the
  per-thread registry memo 2,048 from 512.
- Steam Deck, race paused on the grid: 105.2 ms a frame, from 107.3 (about 2%).

✍️ **A lifted guest store reads one byte before storing, not three globals** (#33, AlexWooz)
- Every lifted PPU store made its own three checks (the null-sweep and barrier-watch diagnostics and
  the SPU reserved-line bitmap): four loads and four branches, about twenty host instructions a store.
- A table with one byte per 4 KiB guest page (`g_ppu_st_page`, `spu_coh_bitmap.h`) now says whether a
  page needs any of that: an SPU has reserved a line of it (set just before the line's bit, never
  cleared), or a store diagnostic is armed (every page). A store reads that byte; only on a flagged
  page does it go on to the exact test, out of line.
- On x86-64 a lifted load or store also no longer tests whether its address is aligned: the atomic
  and the memcpy path are the same single `mov` there.
- Steam Deck, race paused on the grid: 106.5 ms a frame, from 109.5 (about 3%).

🎨 **Vulkan renderer: a draw no longer re-parses its two programs** (#33, AlexWooz)
- Every draw parsed its fragment program six times (its size twice, the sampled, gamma and
  sign-expansion units, the inline constants, the structural hash) and its vertex program four. The
  draw engine now keeps those answers with a copy of each program's bytes and reuses them while the
  guest's bytes still equal the copy (`RSX_ENGINE_PROGRAM_MEMO=0` parses every draw, as before).
  DirectX 12, the default renderer, already did this.
- `PS3_FTRACE` records the SPURS job chain: a span per job (who 200) and per guard wait.
- Steam Deck, race paused on the grid: the RSX thread works 755 ms of each second, from 800.

🐧 **Linux: the command-buffer walker sleeps and is woken, as on Windows** (#33, AlexWooz)
- On a CPU of 8 logical CPUs or fewer, v0.1.41's sleep-after-spin now works on Linux too: the walker
  spins 20 us, then waits on the kick event, which an SPU job writing the word it is parked on sets.
  Before, `ps3_host_sleep_on` was a `sched_yield` there. `PS3_SPIN_SLEEP`, `PS3_YIELD_NAP` and
  `GCM_WALK_SLEEP` work as on Windows; with the sleep off, Linux keeps its 50 us nap (`GCM_FIFO_NAP`).
- `PS3_AB_PERIOD` now works with the Vulkan renderer and reports CPU use on Linux (from `getrusage`).

⚙️ **PPU registers in host locals: in, but off** (#33, AlexWooz)
- The lifter can keep the GPRs, FPRs, CR, XER, LR and CTR a function uses in locals instead of in
  `ctx`, writing them back only before a call or a return. The compiler otherwise has to reload them
  after nearly every guest load and store, since each has an out-of-line slow path.
- It's off unless `PPU_REG_CACHE=1` is set for a re-lift. On Windows it made the generated code 43%
  larger (345 to 492 MB), the lift twice as long (229 to 479 s), a full build 44% longer (17.5 to 25
  minutes on 6 jobs) and gt5.exe 249 to 287 MB, and the paused-grid test found no gain: the game's PPU
  threads are about a tenth of a frame's CPU cycles here, and in two back-to-back pairs they used 6% more
  and 7% fewer cycles a frame with it: run-to-run noise. On the Steam Deck it measured about 7%, close to
  that race's run-to-run noise too.

⚡ **SPU lifter: GT5's sound filters no longer wait on their own memory writes**
- A loop that writes back the 16-byte block it just read now gets the written block straight from a
  register: exact, the same bytes memory holds.
- The sound mixer's job is about 4% cheaper per audio block (1.59 to 1.53 ms on this PC, two back-to-back
  pairs of races, and -4% in three earlier pairs; another game and other test games running); its biquad filter
  loop alone runs 17-22% faster.
- Checked with the differential lift test on every SPU image in the build except one (72,600 runs, 0
  mismatches) and in races with all SPU code re-lifted.

🏷️ Released as **v0.1.62**

💨 **Cockpit view: dust and smoke from the cars ahead no longer cover the dashboard** (Keduboneur78 on Discord,
Toscana in cockpit view, DirectX 12)
- GT5 draws dust, smoke and gravel at a lower resolution (640x360 down to 256x144, depending on load). To keep
  them behind anything closer, it first writes a small copy of the scene's depth into a picture, then
  uses that picture's memory as the depth buffer for the particles. On the PS3 a depth buffer is just
  memory, so this works there. The PC version keeps its depth buffers apart from pictures, so that depth
  never arrived: the particles' depth buffer stayed empty, all the dust passed the depth test, and in the
  cockpit it was drawn over the dashboard, the wheel and the roof.
- Now, before a draw tests against a depth buffer whose memory the game last wrote as a picture (same
  address and row length), the picture is turned into that depth first. `play.err` shows
  `[zeta-import] ... first time` once for each buffer; `LD_ZETA_IMPORT=0` turns it off.
- In the second screenshot, the car seen on the right of the windscreen is really there: it's alongside,
  seen through the glass.
- Checked here: update 2.17 (the one in his log) and the European disc, Arcade, Toscana, in the Ford Focus
  RS WRC and the Fiat 500 in cockpit view, at 1080p. Before: a cloud of dust over the dashboard as soon as
  the leader kicked it up. After: dust visible through the windscreen, the dashboard clear, and the dust
  still drawn behind the cars in the pre-race flyover. The Vulkan renderer wasn't checked.

🏷️ Released as **v0.1.61**

⏱️ **Updates recompile only what changed** (from the update after this one: this one is still installed by
the old update.ps1)
- An update that changed the port's runtime took the old ps3recomp patch out and put the new one in, which
  rewrote every patched file, about 100 of them, shared headers included. The build saw them all as new
  and recompiled all of the game's generated code: the 20 to 50 minutes players saw on most updates.
- `update.ps1` now notes each patched file's content and timestamp first, and afterwards gives the
  unchanged ones their timestamps back, so only what really changed is recompiled. It says how many
  changed ("8 patched file(s) changed in this version, 91 unchanged").
- Checked here on a scratch copy of ps3recomp, updating from v0.1.55's patch to v0.1.60's: exactly the 8
  files those versions changed got new timestamps, and the other 91 kept theirs. The generated PPU code (the
  long part) depends on one runtime header, `ppu_vmx.h`, so it's recompiled only when that changes; the
  lifted SPU jobs, a few minutes, when `spu_context.h` or `spu_helpers.h` does. A re-lift still rebuilds
  everything, as before.

🏷️ Released as **v0.1.60**

🛠️ **Adding Vulkan works with Visual Studio 18; setup no longer picks an incomplete Visual Studio**
- Adding Vulkan stopped at SDL2's link with Visual Studio 18 (clang 22): `undefined symbol: strlen` and
  `wcslen` (reported by brocolison on Discord). SDL2 was built without a C library, and newer clang turns
  its own string loops into calls to those functions. It's now built with the C runtime gt5.exe already
  uses, so nothing more needs installing. Run **Display > Renderer: Vulkan** again: the sources already
  fetched are reused.
- The build could stop at once with "The system cannot find the path specified" (#35). Setup, the
  installer and `build.cmd` took any Visual Studio folder with `clang-cl.exe` in it, and a copy left behind
  by an uninstall can keep that without `vcvars64.bat`, which the build calls first. All three now look for
  one with both, checking every Visual Studio installed, newest first. When none is complete, the
  installer installs the Build Tools as it does on a PC without Visual Studio. `build.cmd` now says which
  Visual Studio it builds with.
- Checked here: SDL2 builds with the C runtime (its DLL needs only what gt5.exe already loads), and a
  Visual Studio folder with `clang-cl.exe` but no `vcvars64.bat` is passed over for the complete one.
  Visual Studio 18 itself isn't on this PC.

🏷️ Released as **v0.1.59**

🎮 **Steering wheels: the steering and pedals no longer get stuck in a race on a busy PC**
(brocolison on Discord: on his laptop at about 20 fps the car kept turning the way it was last steered, with
the throttle or brake held on)
- GT5's wheel driver gets each report from the wheel through a queue of 40 events on its own USB thread, and
  asks for the next report once it has handled one. On a small PC at full load that thread falls behind, the
  queue fills up with force-feedback completions, and a report's event can be dropped. The game then never
  asked for another report and kept the last one, so the car steered and accelerated (or braked) on its own.
- Now a report the game hasn't taken within 1.5 s is sent again, and the wheel comes back by itself. Only one
  report at a time waits to be taken, and the port's two wheel threads run ahead of the game's busy threads.
- If the PC wheel itself stops answering for a quarter of a second (its driver stuck, or the PC too busy to
  read it), the rim is centred and the pedals released until it answers again.
- `PS3_WHEEL_TRACE=1` now also writes, every 10 s, how far the PC wheel moved, how many reports the game took
  and how often it read them, so a log shows where the input stops.
- Checked with a simulated wheel that never rests, like a real rim, with the game on 4 small cores (10-15 fps
  in the race): the queue overflowed as on the laptop. With one report's event thrown away on purpose every
  few seconds, the wheel came back 1.5 s later each time, in the menus and in the race. Not tried on a real
  wheel.

🏷️ Released as **v0.1.58**

🪟 **Vulkan's window takes the window size, resizes, and survives being minimised** (Arpadiam on Discord: the
Vulkan window didn't open at 1920x1080 windowed)
- The Vulkan window now opens at the launcher's **Display > Window size** (`PS3_WINDOW_SIZE`), as the
  DirectX 12 one does, and can be resized and maximised, with the picture stretched to fit. It was held at
  1280x720: SDL keeps a window that isn't marked resizable at its first size, so the setting and the
  maximise button did nothing (borderless worked, as it changes the window's style).
- Minimising the Vulkan window gave it up for good: Windows gives a minimised window no drawing area, the
  backend couldn't make its swapchain for that, and it went on with no window. Now nothing is shown while
  it's minimised and the window draws again when it's back (`play.err`: "window minimised" / "window back").
- Checked on this PC with the window at 1920x1080 windowed: it opened at 1920x1080, a resize to 1600x900
  rebuilt the swapchain at the new size with the picture in it, and after 8 s minimised it came back drawing.
  The v0.1.56 entry's "kept drawing after the window was resized" was wrong: that test's resize didn't
  happen, for the same reason.

🏷️ Released as **v0.1.57**

🔊 **Sound on small CPUs: GT5's audio chain a little cheaper, its EQ a third cheaper**
(players on 4-core laptops and on 2.11 hear the audio lag; GT5's sound runs on one thread, the one that runs
flat out on 4 small cores)
- GT5 makes its sound in blocks of 5.33 ms, each a chain of 13 small SPU jobs run one after another: Sony's
  mixer, EQ, delay, reverb, AC3, up- and downmix and compressor modules.
- Each job began by clearing all 256 KB of the SPU's local store: 5 µs on a fast core, 12 on a small one, 13
  times a block. Now only the part the previous job used is cleared, which leaves the store exactly as a full
  clear would (checked before every job through a whole race with `SPURS_JOB_LS_CHECK=1`). Every small module
  got 4-8 µs a block cheaper.
- The EQ module does part of its work in double precision, and the sound thread runs in the SPU's own
  rounding mode, so every double operation switched the processor's rounding mode and back: 13-20 ns each
  time, ten times the operation. It now rounds those doubles toward zero, a difference in the 16th digit.
- Measured on this PC (European disc, races at High Speed Ring, the game's own timer for each SPU job;
  WoW and other test games running): on all cores, three interleaved pairs of races, the whole chain
  3.13 → 2.96 ms per block (-5.6%) and the EQ 0.43 → 0.28 ms (-35%); confined to 4 small cores, two pairs,
  5.60 → 5.42 ms per block (the jobs besides the mixer -9%) and the EQ 0.66 → 0.49 ms. The recorded sound
  had no invalid samples in any race. On those 4 small cores the chain still takes a little more than a
  block's 5.33 ms when the PC is busy; the mixer, 2.9 ms of it, is next.
- Tried and left out: the mixer's quicker float rules for every sound module. The downmix module filled the
  sound with invalid samples under them.
- No re-lift: update.ps1 rebuilds.

🏷️ Released as **v0.1.56**

🧪 **Renderer choice: DirectX 12 or Vulkan (experimental)** (asked about by Arpadiam and brocolison on Discord)
- The launcher's **Display > Renderer** picks DirectX 12 (the default) or Vulkan, the renderer the Linux
  build and the Android work use. `set "GT5_RENDERER=vulkan"` in `GT5\settings.bat` does the same.
- A normal build has no Vulkan in it. The first time you pick it, the launcher offers to add it
  (`GT5\scripts\add_vulkan.ps1`). It fetches the source code of SDL2 2.32.10 and glslang 15.4.0 with git
  (about 60 MB, each checked against its release's commit), builds them into `GT5\deps\vulkan` with the
  game's own compiler, and rebuilds the runtime: 10 to 20 minutes, and the game's generated code isn't
  rebuilt. DirectX 12 keeps working whatever happens: `gt5.exe` loads SDL2 only when Vulkan runs, and
  without `build\SDL2.dll` play.bat stays on DirectX 12. `add_vulkan.ps1 -Remove` takes Vulkan out again.
- **Slower on Windows for now.** Same race and save, alternating runs from the same `gt5.exe` on the
  tested rig, with other programs open: DirectX 12 about 31 fps, using about 34 ms of CPU time per frame;
  Vulkan 17 to 22 fps, about 55 ms per frame. DirectX 12 has had months of speed work on Windows and Vulkan
  hasn't, so it's there to try and compare, and reports help it catch up.
- With Vulkan, frame generation, the F9 watch-mode key and the window size setting are DirectX 12 only
  (watch mode by itself and Speed: 30 fps are in Vulkan's draw path too, not yet tried with it on
  Windows). The window can be made borderless. (The window size and maximising came the same day, below.)
  Its shaders go in `GT5\shadercache` (ClearShaders.bat clears them too). Esc asks before quitting, as in
  the DirectX 12 window.
- Checked on this PC: Vulkan picked → the game came up on Vulkan (`[RSX vulkan] using device 0`, window
  "Gran Turismo 5 - Vulkan"); Vulkan picked with `SDL2.dll`
  missing → DirectX 12; Esc → the question, No kept playing, Yes quit. Adding Vulkan to a build here
  rebuilt 167 files (the runtime and the link), none of the generated code.

🏷️ Released as **v0.1.55**

⌨️ **Esc asks before it quits the game** (asked for by Arpadiam on Discord: with the keyboard it is easy to
hit by accident and lose what the game hadn't saved)
- Esc in the game window now shows "Quit Gran Turismo 5?". No is the default, so Esc or Enter on the box
  keeps playing; while it is open the game holds still. `set "PS3_ESC_CONFIRM=0"` in `play.bat` quits at
  once, as before. Closing the window with its X button still quits straight away. Windows only (on Linux
  Esc still quits at once).
- Checked on this PC: Esc showed the question and the game kept running; No went back to the game at full
  speed; Esc again and Yes ended it.

🏷️ Released as **v0.1.54**

🎞️ **Frame generation (beta): 30 fps and watch mode look like 60**
- The launcher's **Speed: 30 fps** and **watch mode** (B-Spec, replays, F9) leave every other game frame
  undrawn to spare the CPU, so the picture moved at 30. Tick **Display > Frame generation (beta)**
  (`set "LD_FRAMEGEN=1"` in `play.bat`) and the graphics card makes those frames from the drawn ones before
  and after: it finds how each part of the picture moved and places it halfway. What holds still (the HUD,
  the cockpit, the sky) stays sharp, and where it can't find a good match (very fast or repeating things)
  it shows the newer frame instead of smearing it. It does nothing while every frame is drawn.
- Presenting now happens on a thread of its own, so the game never waits for the screen. The version in
  v0.1.53 (switched off) presented on the game's render thread and slowed the game from 60 frames a second
  to about 24; now, switching it on and off every 10 s in one run on the title scene, the game ran the same
  either way (18.3 / 18.2, 19.7 / 21.0, 20.2 / 19.5, 17.4 / 17.0 frames a second, with another game
  running on the PC). `set "LD_FRAMEGEN_AB=1"` runs that comparison.
- The cost: a few small passes on the graphics card, and the newest frame reaches the screen one refresh
  later. Checked on this PC (European disc, watch mode always on): one made frame for every drawn one, none
  dropped, saved made frames compared with the frames around them. Windows only for now. Please tell us how
  it looks and feels on your PC.

🏷️ Released as **v0.1.53**

💾 **Saves over 2 MB load again** (brocolison and Keduboneur78 on Discord: after a long session, or after
getting every car, the game closed at every start and the progress seemed lost)
- Once GT5 2.xx's save data passes 2 MB, the game writes it as two files, `GT5.0` (exactly 2 MB) and
  `GT5.1`. Keduboneur78's log shows his save going from 387 KB in one file to 3.3 MB in two within one
  session. Nothing was wrong with those files.
- When loading, the game asks how big the save is, and the port answered 0 KB (a PS3, and RPCS3, give the
  real size). From that, GT5 read only `GT5.0`, had two thirds of its save, and stopped with its own
  error at every start.
- The port now reports the save's size the way the PS3 does (every file rounded up to 1 KB, plus 35 KB
  for the system files), puts the file list in the game's own buffer, and recognises the protected-file
  marks a save made in RPCS3 or on a PS3 carries. The log now prints the list the game is given
  (`[cellSaveData] stat:`).
- Checked on this PC with a save cut into `GT5.0` + `GT5.1`: before, the game read `GT5.0` only; now it
  reads both. **If your game closed at start: put your save folder back as it was, with `GT5.0` and
  `GT5.1` together, and start the game.** Please tell us if it loads.

🧹 Logs: when GT5 stops itself it prints `abort() is called from` and a list of the game's own addresses.
Just before that it writes a text the port never sets up, and the port copied 16 KB of zeros into
`play.out` for it, which looked like a lost error message. It now writes one line saying so.

🎞️ Under the hood: the first part of frame generation (making the frames that 30 fps and watch mode leave
out, on the graphics card) is in the code, switched off (`LD_FRAMEGEN`). It works, but in its present form
it slows the game itself (60 game frames a second down to about 24 in a test with the graphics card busy),
so it isn't in the launcher yet.

🎮 **Steering wheels: a wheel with no d-pad moves through the menus**
(brocolison on Discord: his Genius Speed Wheel 3 needed the keyboard for the menus)
- GT5's menus move only with the d-pad. A G27 has one on its shifter, but many cheaper wheels have
  none (that Genius has eight buttons and no d-pad), so the menus needed the keyboard. Now, on such a
  wheel, while the game is at its menus, turning the rim past a quarter of its travel is d-pad left or
  right, and the throttle and the brake are down and up; hold them to scroll. The wheel's buttons
  already reached the menus: Cross (button 1) chooses, Circle (button 3) goes back. The game plays a
  centring spring on the wheel at its menus and the pause screen and stops it when a race loads; that
  is how the port tells them from driving, so in a race the rim and pedals only drive.
- Any wheel can put the d-pad on spare buttons: `set "PS3_WHEEL_BUTTONS=up=7,down=8,left=5,right=6"`.
  A button given a job that way leaves the one it had by default, so `start=8` (for wheels without a
  button 10, where Start sits by default) makes button 8 Start only. `set "PS3_WHEEL_MENU=off"` turns
  the rim-and-pedals menu control off; `on` gives it to wheels with a d-pad as well.
- `logs\play.err` now says how many buttons the wheel has, whether it has a d-pad, and when the game
  goes to and leaves its menus.
- Checked with a simulated wheel with no d-pad (not yet on a real one): at the top menu each turn of
  the rim moved one icon left or right; Cross opened Arcade Mode, the throttle went down a row and the
  brake back up, Circle went back. In a race, steering, throttle and brake reached the game with no
  d-pad. The wheel's Start paused the race, and the pause screen counts as a menu.

🏷️ Released as **v0.1.52**

🐧 **Linux and the Vulkan renderer: new shaders are handed over a little faster on scene changes**
- Since v0.1.49 new shaders build in the background. A scene change brings a few hundred at once, and
  the renderer still did two slow searches for each one before handing it over: one through every
  shader made so far, for a free place in its table, and one through the shader's text sixteen times
  over, for cube-map textures. Each is now a single quick step. On this PC that saves about 18
  microseconds per shader, or a few milliseconds on a big scene change; slower processors save more.
  The Android port's measurements in the Android emulator pointed at it.
- When the renderer shuts down, shaders still waiting in the queue are dropped instead of built first.
  GT5 doesn't run that code when it quits, so players won't see a difference; the runtime's own test
  program does.
- Checked: the new cube-map search gives the same answers as the old one on 200,000 generated
  shader texts and on GT5's own; the Windows Vulkan test copy builds and runs, with shaders built in
  the background.

## 14 Nov 2026

🏷️ Released as **v0.1.51**

📷 **Photo Mode: Auto Focus no longer breaks the viewfinder, and photos after the first are exposed right**
(brocolison and Arpadiam on Discord: a ghosted viewfinder after Auto Focus, later photos too dark)
- Auto Focus: GT5 measures the distance at the focus point by copying a small patch of the scene's
  depth back to the game for a few frames, and it reads that copy straight away. The port delivered it a
  frame or more late whenever the renderer fell behind (in the viewfinder's first seconds, or with a busy
  graphics card), so the camera focused on whatever that memory held before. Everything came out far
  out of focus, and the viewfinder showed a fan of overlapping copies of the car and the scenery. Copies
  the game asks for only now and then now arrive before it looks; the auto-exposure copy it makes every
  frame still arrives a little late, as the game expects.
- Exposure after a photo (1080p): the camera meters a tiny version of the viewfinder image every frame.
  A photo leaves a large working image in video memory that overlaps where that meter is drawn, and
  after the first photo the meter's last two steps read that old image instead of the fresh one. So the
  camera metered a frozen picture and drove the viewfinder, and every later photo, much too bright or
  too dark. The meter now reads its own fresh image. 720p wasn't affected.
- Checked on this PC with the European disc in Photo Travel (Kyoto - Gion), at 720p and 1080p: Auto
  Focus right after the viewfinder opens, and with the renderer slowed down on purpose, now gives a
  clean, sharp preview, where it showed overlapping copies before. At 1080p the viewfinder keeps its
  brightness after a photo (before, it climbed to nearly three times as bright within 40 seconds), and
  three photos in a row came out equally bright. Replay Photo Mode wasn't tried.

🚗 **Garage: new cars get a proper thumbnail again, not a black or scrambled square** (found out on
15 Nov that v0.1.51 fixes this too; Arpadiam on Discord, US 2.11 + Master Mod)
- When you get a car, GT5 draws its picture for the Garage and copies it back from the graphics card in
  small pieces, the same way as the Auto Focus patch above, and saves it. The port delivered those
  copies late, so the saved picture came out black or as coloured noise; with update 2.17 here a late
  copy even landed in memory the game had already reused, and the screen went black.
- Checked on this PC with the European disc and with update 2.17: a car bought in the Used Car
  Dealership gets a correct picture. Pictures saved by older versions stay black: repainting the car
  at GT Auto (Paint Body, 2,000 Cr and a paint item; buying a car also gives you a paint item of its
  colour) makes the game draw a new one.

🏷️ Released as **v0.1.50**

🐧 **Linux: the game window opens on KDE Plasma under Wayland**
([#32](https://github.com/MormonFTP-hub/gt5-recomp/issues/32), lotushomerun: the game ran, with sound,
but no window ever appeared; Arch Linux, Radeon RX 5500 XT)
- To open its window the Vulkan renderer asks the graphics driver which picture formats the window
  can take. It made room for 64 answers. KDE Plasma on Wayland, with Mesa's driver, pairs every format
  with several colour spaces (for HDR) and gives well over 64. The driver then fills the 64 and says
  "there are more", which is a normal answer, but the renderer took it for an error, gave up on the
  window and carried on without one. It now takes all of them, and picks the ordinary (non-HDR) colour
  space when a format comes in several.
- If the window still can't be opened, `play.err` now says so in plain words, and that Ctrl+C in the
  terminal stops the game. On Wayland, `SDL_VIDEODRIVER=x11 bash GT5/play.sh` (through XWayland) is a
  way around it.
- Checked: with the same renderer on Windows the window still opens as before. There's no Plasma
  Wayland session here, so the case with more than 64 formats itself is untested.

🏷️ Released as **v0.1.49**

🐧 **Linux: no more freezes when something new comes on screen, and road markings sit on the road**
([#20](https://github.com/MormonFTP-hub/gt5-recomp/issues/20), melroy89: single frames of 0.3 to 0.7 s,
once 1.7 s, in races and licence tests on a Radeon RX 7900 XTX)
- The Vulkan renderer (the Linux build's) made every shader the game hadn't used before on the spot,
  while the game waited: translated into Vulkan's form, then compiled by the graphics driver. A new
  car, track or menu asks for 100 to 300 of them at once, so the picture stopped for up to seconds.
  Now they're built on spare CPU cores while the game keeps running (`PS3RECOMP_VK_ASYNC_SHADERS`, on
  in `play.sh`). The cost: something can be missing for its first few frames on screen.
  `export PS3RECOMP_VK_ASYNC_SHADERS=0` in `GT5/settings.sh` goes back to the old way.
- Checked on this PC with the Windows build of the same Vulkan renderer (NVIDIA card), from the title
  screen to the B-1 licence test with no shaders cached yet. Before: 16 frames of 250 ms or more, 10 s
  frozen in all, the worst 2.2 s (and 5.9 s with the driver's own cache empty too). Now: none of
  250 ms or more, the worst 210 ms, and the pictures were complete once the shaders were built.
- GT5 moves road markings, skid marks and its shadow maps a tiny step towards or away from the camera
  (the PS3's polygon offset), so they don't flicker against the surface under them. The Vulkan
  renderer ignored that and now applies it (`PS3RECOMP_VK_DEPTH_BIAS=0` turns it off). Whether it is
  also what made the car bodies flicker on the 7900 XTX isn't known yet: that didn't show here.
- `play.err` now has a `[slow-frame]` line for every frame of 250 ms or more, as the Windows build
  does, splitting it into new shaders (translation and driver), textures, waiting for the GPU and the
  rest (`PS3RECOMP_VK_HITCH_MS` sets the limit, 0 turns it off). A texture the renderer can't create
  is now logged too.
- Not seen on this PC yet (NVIDIA, European disc): the flickering car bodies, the see-through road in
  the B-2 intro, the missing textures in a braking test and the dark title-screen scene. With
  `PS3_SKIP_INTRO=1` (play.sh's default) the scene behind the title is GT5's own replay of a race at
  dusk or at night, drawn live, and it is just as dark in the Windows build. Questions for those are
  on #20.

🏷️ Released as **v0.1.48**

📷 **Photo Mode pictures have their real colours again: no more yellow-green tint or coloured ghost copies**
(brocolison and Arpadiam on Discord: tinted, ghosted and very dark photos)
- GT5 builds a photo from about 50 slightly shifted renders, averaged one after another into a
  high-precision image (that gives photos their smooth edges and depth of field). Each render is blended
  in with a weight the game sets as a constant blend colour. For this kind of image the PS3 keeps that
  constant in two registers, and the port read its second half (blue and alpha) from the wrong one, so
  blue was blended with a weight of 0 and kept whatever the image held before: black, which gave the
  yellow-green tint, or part of an older render, which gave the coloured ghost copies. The weight was
  also taken per colour channel where the PS3 uses the alpha value for all of them.
- Checked on this PC with the European disc in Photo Travel (Kyoto - Gion): before, the saved photo had
  no blue at all (pink blossom came out yellow, the night sky green); now its colours match the
  viewfinder, and a 3840 x 2160 photo (16:9, x2) has no ghost copies.
- Not fixed yet: after a photo is taken, the viewfinder and the next photos can come out much darker.
  That happened before this change too and is being looked into. Auto Focus did not show ghost copies
  on this PC.

🏷️ Released as **v0.1.47**

🛡️ **The game starts on PCs where Windows forces Control Flow Guard on every program**
([#31](https://github.com/MormonFTP-hub/gt5-recomp/issues/31), Fantomas3228: the game closed a second
after Play, with a log that just stopped)
- Windows Security's *Exploit protection* can turn Control Flow Guard (CFG) on for all programs. With it
  on, Windows checks every `longjmp` the program makes, and the SPU code's fast jumps (which skip
  unwinding, for speed) failed that check: Windows ended the game on the spot (fast fail, exit code
  `0xC0000409`) as the first SPU job ran.
- Now the game asks Windows whether CFG is on for it, and if so makes those jumps the standard way.
  play.err then says `[spu] Control Flow Guard is on for this process`. PCs without CFG forced on keep
  the faster jumps.
- Checked on this PC by starting the game with CFG switched on for that one run: before, it closed
  after 2 s with `0xC0000409` at the same line as in #31; now it runs normally. No Windows setting needs
  changing any more (Fantomas3228's workaround, `Set-ProcessMitigation -Name gt5.exe -Disable CFG`,
  can stay or be undone).

📺 **Options > Display no longer crashes the game** (reported by Arpadiam on Discord: the Resolution
list was empty, and leaving the screen with OK closed the game)
- The port described the TV's video modes to the game with a block of data missing (the TV's colour
  information comes before the list of modes), so GT5 read its list 20 bytes too early, found a mode
  with resolution 0, showed an empty Resolution list and failed when the screen was confirmed.
- The list now has the layout the PS3 uses, with one mode: the one the port draws (1080p or 720p, as
  set in the launcher), at 59.94/60 Hz, the way RPCS3 reports it.
- Checked on this PC with the European disc: Display Settings shows *1920 x 1080 59.94p*, and OK
  returns to Options with the game still running. The 2.11 update's Resolution row is not tested here.

🏷️ Released as **v0.1.46**

☀️ **Races at 1080p get the right brightness: no more blown-out white or too-dark tracks** (reported by
Keduboneur78 on Discord: Speed Test's track near-white even at −1.0 EV)
- GT5 sets each frame's exposure from a small 16x8 copy of the scene that it reads back from the
  graphics chip. In 1080p mode that copy sits in memory that a bigger image left over from an earlier
  scene also covers, and the Windows renderer read it from that leftover image. So auto-exposure judged
  every race by a frozen patch of whatever was shown before (often the title screen's white city), and
  a race came out as bright or as dark as that patch suggested: Speed Test blown out to white, or High
  Speed Ring almost black on this PC.
- It now reads the image the game actually measures into, as the Linux and Android renderer already
  did. 720p mode was never affected.
- Checked on this PC with the European disc: High Speed Ring at 1080p averages a brightness of 81, like
  the same start at 720p (84); before, with the leftover patch, it was 17. The play.err now says which
  image feeds the exposure (`[blit-readback] source now ...`). `LD_RECT_PICK=0` brings back the old
  behaviour. Speed Test itself is a DLC course not tested here.

🏷️ Released as **v0.1.45**

📦 **Install game updates and DLC from their .pkg files** (asked for by Keduboneur78 on Discord: "just
import PKG files the way RPCS3 does")
- The launcher's *Help > Install a game update or DLC (.pkg)* takes the packages you pick
  (`GT5\scripts\install_pkg.ps1 <files or folder>` does the same). Before, an update meant installing it
  in RPCS3, copying its folder into `GT5\hdd0` by hand and unlocking its executable.
- It reads each package's header, puts updates in version order, and has RPCS3 install them: RPCS3's
  window opens for each, and the script answers its *Do you want to install this package?* and closes
  it when the install is done. Updates RPCS3 already has are skipped, and its errors are passed on
  (*Update is for version 01.12, but you don't have any data installed*).
- Then the game's folder is copied from RPCS3's `dev_hdd0` to `GT5\hdd0` (all but `EBOOT.BIN`), and
  `update_exe.ps1` unlocks the update's executable into `GT5\input\EBOOT.elf`. What's left is one
  `setup.bat` to build the game for that version.
- Checked with the European updates 1.05 and 1.06 and a copy of RPCS3 0.0.37:
  - 1.05, already installed, was skipped; 1.06 installed with no clicks.
  - The folder landed in `GT5\hdd0` as version 01.06.
  - `EBOOT.elf` became 1.06's executable, with the disc's kept beside it.
- RPCS3 doesn't start from a temporary folder: run it from a normal one. DLC vouchers still need
  their licence keys (README, DLC).

🏷️ Released as **v0.1.44**

🩺 **The launcher says why the game stopped, when Windows knows** (for reports like
[#31](https://github.com/MormonFTP-hub/gt5-recomp/issues/31), where the game closed at once and its log
just stopped)
- Some crashes skip the game's own crash report: a "fast fail" (exception `0xC0000409`) ends the game
  on the spot. So does Microsoft Defender stopping it. Either way `play.err` stops mid-start with no
  error in it. Windows records both, so the launcher now looks when the game ends. It checks the
  Application log for an *Application Error* in `gt5.exe` (the exception, the module and the offset)
  and Defender's history, for the time the game ran.
- What it finds is shown in the launcher ("The game stopped. Windows says: ...") and added to
  `play.err` as a `[launcher] Windows says:` line, so *Help > Save logs* carries it into a report.
- Checked against this PC's own records: they hold three earlier `gt5.exe` fast-fails from test builds,
  read back with the code, module and offset. The update rebuilds the launcher (`Update.bat`, or the
  launcher's update).

🏷️ Released as **v0.1.43**

🎵 **Leaner SPU code: GT5's sound mixer 22% faster, all SPU jobs 14% less CPU** (one re-lift, done by
`update.ps1`)
- The sound mixer is one SPU job run on one host thread 187.5 times a second (once per 256-sample block
  of 48 kHz audio), and nothing else can share that work out. On a stand-in for a 4-core laptop (the
  tested rig's i9 limited to 4 of its efficient cores) it was the one thread running flat out: about a
  full core, and short of real time.
- Its hottest loops filter the sound sample by sample. In the lifted code, a loop that starts where its
  function starts wrote every SPU register it changed back to memory and read it again on every pass.
  Such a loop now keeps them in CPU registers from one pass to the next, as other loops already did.
- The SPU stores a single number by building a mask for its position and shuffling it into the 16
  bytes it writes back. Where that mask was just built, the shuffle is now a direct insert.
- Measured with the per-job SPU timer, alternating the old and new builds three times on all cores:
  the mixer from 1.99 to **1.56 ms** per block (−22%), all SPU jobs from 40.0 to **34.4 s** of CPU per
  minute (−14%). On the 4 efficient cores, with another game running at the same time: the mixer from
  3.9-4.2 to 3.1 ms per block.
- Checked by lifting every SPU job both ways and running both from every entry point on random states:
  the same registers, local store and channel traffic in about 15,000 runs (15 job images and the 4
  busiest Wws job images). A race looks and sounds as before.
- The lifter's `--no-loop-head-loads` and `--no-insert-peephole` give the old output, for comparison.
- Needs one re-lift (the SPU lifter changed); `update.ps1` does it.

## 13 Nov 2026

🏷️ Released as **v0.1.42**

🛞 **A steering wheel that stops answering no longer keeps driving the car** (reported by brocolison on
Discord, Genius Speed Wheel 3)
- Once, mid-race, the car accelerated by itself and steered left with no control. The button setting he
  added afterwards (`PS3_WHEEL_BUTTONS=cross=1,square=2,circle=3,triangle=4,start=10`) is the same as the
  defaults, so it can't be what fixed it. The likeliest cause is the wheel briefly not answering (a USB
  hiccup, a loose cable): the port went on giving the game the wheel's last state, throttle and steering
  included, for a second or more, then dropped the wheel and looked for it again only every few seconds.
- Now after 50 ms without an answer the rim is centred and the pedals released, with the wheel still
  attached. After a second it's closed and looked for every half second. When the same wheel comes back,
  it keeps the rest points measured at the start, so a pedal held at that moment doesn't count as released.
- `play.err` says `[wheel] not answering` and `answering again` when it happens. Not tried on a real wheel
  (this PC has none); it builds and the game runs as before without one.

📺 **Linux: the main menu at about 25-30 fps on a Steam Deck (was about 17)**
- With the waits above gone, the menu's GPU waits came from two places, both every frame: each new
  render target was cleared in a submission of its own, waited on after first waiting for the open
  batch, and each released target made the renderer wait for every batch before it destroyed its
  cached framebuffers. GT5's menu releases and recreates a target or two a frame.
- A new colour or depth target is now cleared in the open batch, before the draws that use it (and
  a new texture is readied there too: in a race those waits were about 1.2 s in every 5 s), and
  framebuffers go to the same deferred list as released images, destroyed once the batches that may
  use them are done. (When a batch's list has no room left, that batch is waited on and they go at
  once, where before the extra entries were dropped.)
- On a Steam Deck in the main menu: time waiting for the GPU from about 1.2 s to 0.01 s per 5 s, the
  median frame from 53-58 to 35-36 ms, about 25-27 frames a second, most of them at two vblanks.

🪟 **Linux: a present no longer waits for the frame to be drawn**
- Each frame is copied into the window, and that copy shared the present's command buffer, so it was
  waited on at once; it sits in the GPU queue behind the frame being drawn, so every present waited
  for the whole frame's GPU work: about 11 ms of the graphics thread per frame on a Steam Deck. The
  window copy now has three command buffers, fences and acquire semaphores of its own, and one is
  waited on only when its turn comes round again, three presents later.
- On a Steam Deck in the main menu: a present 11 ms to 0.4 ms, the median frame 62-64 to 53-58 ms
  (the menu is still paced to three or four vblanks).

⚡ **Game code: vector multiply-adds four lanes at a time (Windows and Linux)**
- The PS3's vector multiply-add (vmaddfp) and negated multiply-subtract (vnmsubfp) were done one lane
  at a time, with several checks per lane, and without the CPU's fused multiply-add: about a tenth
  of a busy GT5 game thread. Now all four lanes go at once with one fused multiply-add, the same
  flush and NaN rules applied to the four together. The results are bit for bit the same: checked on
  4 million inputs per operation (random values, zeros, denormals, infinities, NaNs, rounding
  midpoints, both flush modes) against the per-lane code.
- The recompiled game code is built with FMA for it (every AVX2 CPU has FMA). Floating-point
  contraction stays off, so no other result changes.
- On a Steam Deck in a race: the game thread from about 69% to 62% busy.

🧮 **Linux: texture binding no longer re-hashes every car's cube map at every draw**
- Each car's small ambient cube map was checked for changes at every draw that bound it, by hashing
  its six faces byte by byte: in a race on a Steam Deck, about 87% of the time spent binding
  textures, itself about 13% of the graphics thread. It is now checked once a frame, as the texture
  cache checks a texture, and hashed 8 bytes at a time: texture binding fell to about 0.2% of all
  the CPU time. The cars look the same.

⚡ **Linux on small CPUs: the graphics thread naps instead of spinning while it waits for SPU work**
- GT5's SPU jobs fill gaps in the graphics command stream, and the graphics thread waits at each gap
  (on a Steam Deck about 1,000-1,400 times a second in a race). It watches the gap so it carries on
  the moment it is filled, yielding between looks; on Linux that yield returns at once when nothing
  else is queued on the core, so the wait was a spin, about a fifth of the thread's time, on cores
  the SPU threads filling the gaps could have used.
- With 8 logical CPUs or fewer it now watches for the first 30 us, then naps 50 us between looks: a
  short wait still ends at once, a longer one at most 50 us late. `GCM_FIFO_NAP=0` spins as before,
  `=1` naps on any CPU.
- On a Steam Deck over the same ~70 s of a race: the graphics thread 70% to 56% busy, frames about
  135 to 110 ms on average (82-100 ms on the faster stretches).

🧊 **Linux: the shader cache survives updates**
- Each cached shader was stamped with the time its translator was built, and any other stamp counted
  as stale. Every update re-patches and rebuilds that code, so after every update the whole cache was
  thrown away and the next races compiled every shader again (on a Steam Deck, close to half the
  graphics thread for minutes). The stamp is now a version number, raised when the translator's
  settings change; the shader text and the glslang version still decide a hit as before.
- Checked on a Steam Deck: after the translator code was rebuilt, the menus wrote 7 shaders instead
  of all 295 again. Caches written by older builds miss once, the first time.

🌗 **Linux: race shadows read on the GPU, and the dark band ahead of the car is gone**
- GT5 reads its 1024x2048 shadow map as a 16-bit depth texture twice a race frame. The Vulkan
  renderer could convert depth on the GPU only for the other way GT5 reads depth, so each read
  waited for the GPU, copied the depth back, converted it on the CPU and uploaded it again. Now a
  compute pass rounds every depth to 16 bits on the GPU, in command order, exactly as the CPU did
  (round(d * 65535) in 32-bit integers), into the R16 texture the texture unit samples.
- The shadow map is drawn in three bands of 682 rows, and only the last band counted as drawn, so
  the copy through memory carried that band alone; the other two came from stale memory, which
  showed as a darker band a little ahead of the car. All three bands are read now.
- On a Steam Deck in a race: time waiting for the GPU from about 1.6-2.1 to 0.7-0.9 s per 5 s, the
  depth readbacks gone, frames about 113-143 ms.

⚡ **Linux: races about 30% faster on a Steam Deck (the CPU and GPU now work at the same time)**
- The Vulkan renderer waited for the GPU after every batch of draws and at every present, so the
  walker idled while the GPU drew and the GPU idled while the walker recorded the next batch (a Steam
  Deck's GPU went from 100% to 0% in turns). Now up to three batches are in flight, each with its own
  command buffer, fence, descriptor pool, share of the vertex, index, uniform and upload rings and
  list of objects to destroy; a full batch or a present is submitted and recording carries on. Only
  what needs the GPU done still waits for it: readbacks, destroying objects, the engine's own wait.
  `VK_ASYNC_FLUSH=0` brings back a wait after every batch.
- The present isn't waited on either: the next present finishes it, so the window shows a frame when
  the next one is presented.
- 16-car races kept only 8 of the cars' small ambient cube maps, so they evicted each other every
  frame, and each miss created a texture and decoded the cube again (about 7 a frame). Now 64 are
  kept: texture creations in a race went from 259 to 2 per 5 s.
- On a Steam Deck (OLED, SteamOS 3.9), same race: frames ~175-185 ms to ~122-127 ms (about 5.5 to
  8.3 a second), time waiting for the GPU from ~1.8-2.2 to ~1.4-1.5 s per 5 s; nothing drawn
  differently. Menus are unchanged: their frames are paced to four vblanks either way.

⚡ **Update 2.17: the sound job gets its fast floating-point setting back**
- GT5's sound job (the "soc-job" chain, run 125 times a second whatever the frame rate) is built
  with `SPU_X86_FAST_FLOAT`: its numbers are sound samples, so the exact SPU range and signed-zero
  handling can go. Update 2.17 replaces that job (`jD58063CD`) with a new version (`jCB9BD024`), and
  the setting named only the old one, so on 2.17 the heaviest job in a race ran the slower exact
  path. Both versions get it now, on Windows and Linux.
- On a Steam Deck in a race: the job's time went from about 390 to about 340 ms a second, its thread
  from 86% to 82% busy, and frames from about 135-150 to 130-140 ms. Sound unchanged.

🏷️ Released as **v0.1.41**

⚡ **Small CPUs and laptops: the graphics threads stop spinning while they wait**
- Where the CPU goes, measured on a stand-in for a 4-core laptop (the tested rig's i9 limited to 4 of
  its efficient cores, Arcade race at High Speed Ring): about a third of the CPU went to threads
  waiting, waking and spinning instead of running the game. The two worst were graphics threads. The
  thread that hands draws to Direct3D 12 spun between batches, about a third of a core at raised
  priority, for 12% real work. The thread that walks the PS3 command buffer spun for up to 4 ms each
  time it waited for the game or an SPU job to write more. On a 4-core CPU that spinning takes CPU from
  exactly the threads being waited for, and on a laptop it spends the power budget.
- On CPUs with 8 threads or fewer, both now spin briefly and then sleep. The command walker is woken
  as soon as an SPU job writes the word it's waiting on; otherwise it checks again within 0.1 ms.
- Measured on those 4 cores, with the change switched off and on every 10 seconds within one race:
  - **With nothing else running:** the same frame rate (28 game fps, 1% lower), with **20% less CPU**
    (3.1 → 2.5 cores busy, 109 → 89 ms of CPU per game frame). A laptop at its power limit can spend
    the saved power on clock speed.
  - **With the CPU shared** (another game and a second test copy of the port running at the same
    time): **+25% game frames** (7.8 → 9.7 fps; +29% in a second race).
- `PS3_SPIN_SLEEP=1` turns it on for bigger CPUs too, `=0` turns it off.
- Also in this release, off by default: a SPURS scheduler change that wakes one SPU worker per job
  instead of all of them (`SPURS_SLOT_WAKE=1`, `SPURS_PROBE_PACE=2000`). It made no measurable
  difference once the threads stopped spinning (+0.6% ± 4%).
- For testers: `PS3_AB_PERIOD=10` switches the new behaviour off and on every 10 s and logs `[ab]` lines
  with game fps and CPU for each half, so one race compares both.
- The update rebuilds a few files; no re-lift.

🏷️ Released as **v0.1.40**

🚨 **Car lamps show their lights again: red tail lights, headlights with their reflectors** (reported by
Angel4eva on GitHub, [#14](https://github.com/MormonFTP-hub/gt5-recomp/issues/14), a Lancer Evolution X's
tail lights, and by brocolison on Discord, "can't see headlight details")
- Tail lights and headlights showed a plain, pale mirror of the sky and trees instead of the lamp inside:
  the Lancer's red clusters were white-grey patches, and headlights were flat discs.
- GT5 draws a lamp's glass by reading the picture already drawn under it (the red lamp, the reflector)
  at its own spot on screen and tinting it. It works out that spot with the y axis counting up from the
  bottom of the screen, as the PS3 can be told to. The Windows renderer always counted from the top, so
  each lens read the picture upside down, sky where the lamp should be. It now uses the PS3's setting
  (the Linux and Android renderer already did).
- The lens tint is a fixed blend colour the game sets for the glass (amber on an indicator); it wasn't
  passed on either, and now is.
- Checked on this PC with the European disc at 1080p: the Lancer Evolution X at car select and in a race
  (chase camera, paused, before and after), the Fiat 500 and Alfa Brera at car select, and the menus,
  cockpit, bonnet and bumper views and a replay look as before.

🏷️ Released as **v0.1.39**

🚗 **Cockpit view with a game update (2.xx): the windows are clear again** (reported on Discord by
Arpadiam and brocolison with 2.11 and the Master Mod, licence test B-1)
- In the cockpit camera, the windscreen and side windows showed a big, blown-out, speckled copy of the
  screen: a second HUD ("Best Lap", "Target"), the dashboard and the pillars. The disc's own version
  01.00 never did this, which is why the plain disc looked fine.
- Since the updates, the glass uses GT5's "no texture": a 1x1 texture set to read as transparent black
  whatever memory holds (its channel mapping takes nothing from memory). It points at the very start
  of video memory, where the last finished frame sits. The port applied that mapping to textures it
  decodes from memory, but not to one that lands on a picture it has drawn itself, so the glass got the
  whole last frame, stretched over the windows, and the glass's own fine dither pattern turned into the
  speckles.
- A texture whose mapping takes no channel from memory now reads its fixed values, as on the PS3,
  wherever it points.
- Checked on this PC with the European disc and update 2.17, at 1080p: B-1 in the cockpit camera shows
  the track through clear glass, with one HUD and working mirrors; the roof and chase cameras, menus and
  an Arcade race look as before.

🏷️ Released as **v0.1.38**

🛞 **The first real steering wheel: a Genius Speed Wheel 3 Vibration** (tested by brocolison,
[#29](https://github.com/MormonFTP-hub/gt5-recomp/pull/29), whose README line is merged)
- Their test: with `PS3_WHEEL=0458:3017` the wheel attaches as the emulated G27, and the game sends it
  its wheel commands in a race.
- **It's found by itself now.** Windows lists this wheel as a joystick with no force feedback, so the
  port's wheel check skipped it. A controller with "wheel" in its name now counts as a wheel.
- **Throttle and brake on one axis.** Its two pedals share one axis that rests in the middle. Read as
  one pedal, resting there was half throttle, and there was no brake. When the throttle's axis rests in
  the middle and the wheel has no brake axis (or both are set to the same axis), the port now splits it
  where it rests: one way is throttle, the other brake, with a small dead zone. `play.err` says so
  (`[wheel] throttle and brake share the Y axis`); if they're the wrong way round,
  `set "PS3_WHEEL_PEDALS=throttle=Yinv"`.
- Not yet driven on that wheel with this change (there's no wheel here): brocolison, a quick race
  would tell.

🏷️ Released as **v0.1.37**

🧹 **Clear the shader cache from the launcher** (asked for on Discord)
- Launcher, Help, **Clear the shader cache**, or double-click the new `ClearShaders.bat` in the install
  folder, with the game closed. It says how many files and how much space first, then deletes the
  compiled shaders in `GT5\scratch\rsx_shader_cache_v2` (and the Linux build's `GT5/shadercache`) and
  nothing else: saves, settings and game files stay.
- The game compiles its shaders again as it needs them, so the first visit to each menu, car and track
  stutters again for a while, as after a fresh install. Use it to free the space (the cache grows with
  every car and track you see) or for a fresh start.
- It doesn't change what's drawn: a cached shader is only ever used for exactly the same source, so a
  game update, a mod or a new version of the port never picks up an old one.
- The launcher gets the button when you update (Update rebuilds it). README, FAQ: "How do I clear the
  shader cache?"

🏷️ Released as **v0.1.36**

🏁 **The GT5 Master Mod runs** (Nenkai's, 1.9.2 on update 2.11, US release; reported on Discord by
brocolison)
- With 2.11's and the mod's data in `GT5\hdd0` and the mod's executable unlocked into `GT5\input`: the
  mod's boot screen, the menus, its SELECT debug menu and an A-Spec race.
- `setup.ps1` names that executable (SHA-1 `266EE27115B2AAC6318AD0122DAE56EDE94B797F`), and README and
  STATUS list 2.11 with the Master Mod as working.

🏷️ Released as **v0.1.35**

🛠️ **An installed game update gets its executable by itself** (the fix for the 2.11 black screen below)
- `setup.ps1` and `Update.bat` now do step 2 of [Game updates](README.md#game-updates) themselves. When
  `GT5\hdd0` holds an update but `GT5\input\EBOOT.elf` is still a disc's 01.00, they find the update's
  `EBOOT.BIN` and unlock it with RPCS3 (the same tool and step the installer and README use for the
  disc's). The result becomes `GT5\input\EBOOT.elf`, the disc's is kept as `EBOOT.disc-01.00.elf`, and the
  game code is generated again for it.
- Where it looks: `GT5\hdd0\game\<title ID>\USRDIR\EBOOT.BIN`, then RPCS3's `dev_hdd0` (also where a
  mod's own `EBOOT.BIN` goes), with RPCS3 found in `GT5_RPCS3`, the installer's `tools\rpcs3`, PATH or the
  usual folders (Downloads, Games, Program Files...). An `EBOOT.BIN` that's already unlocked is used as
  it is. If neither turns up, setup stops and says what to do by hand.
- Tested on this PC with update 2.01's `EBOOT.BIN` from RPCS3 (Hong Kong release): both from `GT5\hdd0`
  and found through RPCS3's `dev_hdd0`, the result was exactly the known 2.01 executable. A disc-only
  install is left alone. The script is `GT5\scripts\update_exe.ps1`.

🏷️ Released as **v0.1.34**

🛟 **Setup catches an update installed without its executable** (a black screen before the main menu,
reported on Discord with update 2.11 and the Master Mod on the US release)
- The log showed the disc's version 01.00 executable running against update 2.11's data: GT5 reads
  updated files from the hard disk whatever its own version, so 01.00 ran 2.11's newer scripts, which
  call code 01.00 doesn't have, and stopped before the main menu.
- `setup.ps1` now stops when `GT5\hdd0\game\<title ID>` holds an update (its `PARAM.SFO` says a version
  newer than 01.00, or its `EBOOT.BIN` is there) while `GT5\input\EBOOT.elf` is still a disc's 01.00, and
  says which `EBOOT.BIN` to decrypt. README, Game updates: step 2 says why it can't be skipped.

🏷️ Released as **v0.1.33**

🐧 **Linux: races drawn full screen, the right exposure, and sound effects on time** (#20)
- Races were squashed into the top half of the picture, with bands of different brightness. GT5 draws
  a race antialiased into an image twice the size and shrinks it by pixel coordinates; the Linux
  renderer handed it the whole bigger image. It now gets just the part it reads, as on Windows.
  Checked at 720p and 1080p: Arcade and B-Spec races fill the screen.
- Images GT5 reads from partway down a bigger one (how it measures a scene's brightness level by
  level, and builds the smaller levels of the car reflections) now come from that picture instead of
  stale memory.
- Cut-out foliage such as the cherry blossoms in one title scene was drawn as solid grey cards; it is
  cut out now, as on Windows.
- Sound effects 30 seconds or more late: the Linux sound mixer ran about 4% fast, and the extra sound
  piled up in a queue, so everything you heard drifted later and later. It now keeps pace with the
  sound device (checked on Android: 187.5 blocks a second, the queue steady at 20 ms).
- The title screen's white model city is drawn white by GT5 itself (Windows shows it the same).

🏷️ Released as **v0.1.32**

⚡ **First-visit stutter about three times shorter: new shaders compile in parallel** (a community pull
request by AlexWooz, #28)
- A shader that isn't in the cache took 25-35 ms to compile, one after another on the graphics
  thread, and a new car or track brings a hundred or more at once. Now they go to up to 8 compile
  threads, and the graphics thread carries on to the next draws, whose new shaders compile
  alongside. The command recording waits for a pipeline only when it reaches it, so the GPU gets
  the same commands in the same order as before.
- Creating a new render target or depth buffer no longer waits for everything queued to be
  recorded (and so for every shader still compiling): a new slot's view was never used.
- Measured from an empty shader cache on the same route (menus, a few cars, a race start):
  stutter in all 26.6 s instead of 78.4 s; the race start with about 340 new shaders 3.4 s instead
  of 12.2 s; about 115 new shaders 1.1-1.4 s instead of 4.4 s; 3 freezes over a second instead of 16.
  No compile or pipeline errors, and nothing drawn differently. That was the US release with update
  2.17 on a Ryzen 7 9850X3D and Radeon RX 9070 XT.
- Checked again on the European disc (i9-14900KF, the same Arcade route from an empty cache, both
  builds compiling about 600 shaders): the car-select freeze with about 100 new shaders 2.1 s instead
  of 5.6 s, the pre-race screen reached 25 s sooner, no compile errors, and the race drawn the same.
- Visits after the first are unchanged: their shaders load from the cache, and building the
  pipelines from cached shaders took about half a millisecond each in a test.
- `LD_ASYNC_SHADERS=0` compiles one at a time as before. Only with the recording thread (on by
  default; `LD_RECORD_THREAD=0` turns both off).

📖 **README, Linux: the first test on a real graphics card** (melroy89, Radeon RX 7900 XTX, Linux Mint
22.3, #20)
- Setup, the build, the menus and an Xbox controller work, with no crashes. Races still draw in the
  top half of the picture, the intro's 3D scene has white textures, and sound effects play late: all
  listed under "Not there yet on Linux".
- How to turn on full screen so it survives updates: `export PS3RECOMP_VK_FULLSCREEN=1` in
  `GT5/settings.sh`.

📖 **README: the Linux setup in five steps** (a pull request by melroy89, #27)
- Install the build tools, get the firmware with RPCS3, copy the disc's `PS3_GAME`, decrypt the
  executable, then check and build, each with commands to copy, plus the file layout to expect.
  Requirements list Windows and Linux side by side; the Linux test report moved below the steps.

🏷️ Released as **v0.1.31**

🎁 **DLC works, offline, with your own licences** (US release, update 2.17; a community pull request by
AlexWooz, #25)
- The gift cars arrive in the garage, and every pack (Racing Car Pack, Car Packs 2 and 3, the 2012
  car packs, Course Pack, Speed Test, Twin Ring Motegi) is in Downloadable Content > Purchased
  Content and installs. What the game needed:
  - DLC files read the way the PS3 serves them: as stored for a plain open, decrypted for the
    licensed one. GT5 checks each voucher's header first and threw them all away when it saw the
    decrypted contents there (`PS3_EDAT_STRICT`, on in `play.bat`).
  - An answer to the PSN check of what you bought: "not owned", as for an account without
    purchases, instead of "success" with nothing filled in. The game then takes its DLC from the
    vouchers, and a voucher opens only with the key of your licence.
  - A PSN sign-in for the Downloadable Content screen: `play.bat` turns it on only when DLC is
    installed, with a local profile and no connection to anything (`PS3_NP_SIGNED_IN`).
  - Licence keys in `hdd0\home\00000001\exdata`, and the decrypted copies out of the
    vouchers' folder: the game opens every file there.
- Only DLC you bought unlocks. Tested on new saves: without the licence keys, or without the DLC
  files, the game shows no purchased content and no gift cars.
- How to install it: README, [DLC](README.md#dlc). Players without DLC see no change: on the European
  disc a race runs as before, the game opens no DLC files, and its PSN calls are the same as in v0.1.30.

📖 **README: Linux in Requirements** (asked in #20)
- Requirements now start with what the Linux build needs, and a Linux badge sits next to the Windows one.

🏷️ Released as **v0.1.30**

🏁 **Update 2.17 runs** (from a community pull request by AlexWooz, #23; one re-lift)
- 2.17, the last update, gets through menus, GT Mode and a full 16-car Arcade race at 60 fps on the
  US release (BCUS98114), tested by AlexWooz. It was built with a newer compiler than the disc, and the
  lift didn't know two of its patterns:
  - Switch tables whose address is built from constants (`lis`/`addi`) instead of read from the
    TOC. Every switch became a call into nowhere, starting with the game's start-up code: a black
    screen.
  - A switch table with more than 256 cases was cut at 256 when the index and the loaded entry
    share a register. The cases past it ran into garbage after the first menus.
  - The computed goto `version_addrs.py` finds is laid out differently too, and its labels are
    stored by code rather than copied from a table. It now finds both forms.
  - Four SPU jobs are new or changed in 2.17; setup finds them in the executable
    (`13 of 15, plus 3 that only game updates have` is complete for 2.17).
- The second fix reaches the disc too: the European 01.00 executable has one switch with more than 256
  cases, and its last 35 cases were skipped if the game ever got there (with an `unresolved indirect
  call` line in the log; none turned up in our play logs). They now run. Every other part of the disc's
  lift is unchanged, and `version_addrs.py` gives the same results as before on the European and Asian
  discs and on Asian update 2.01.
- **The European 2.17 runs too**, which the pull request couldn't test: with these changes its
  executable gives the same SPU jobs and almost the same lift as the US one (358 switches), and it
  gets through the update's first-start questions (data install, language, screen size), the opening
  movie and Arcade Mode into a 16-car race, with no missing SPU job and no unresolved call. Its frame
  rate isn't measured yet.
- Set an update up as in README, [Game updates](README.md#game-updates).

📖 **README: saves are in `GT5\gamedata`**
- The README said `GT5\hdd0`. Anyone moving to a fresh copy by hand should take `GT5\gamedata` (and
  `GT5\hdd0` if they set up a game update). `update.ps1` never touched either.

🏷️ Released as **v0.1.29**

⚡ **SPU code runs leaner: SPURS jobs take 20% less CPU, about 7% more frames on 4 cores** (one re-lift)
- Measured on 4 of the test PC's efficient cores, where the CPU is the limit as on a laptop, with five
  races per build taken in turns and timed while the car drives: 20.7 game frames a second on average,
  against 19.3 before. The CPU time per frame fell from about 208 ms to about 193 ms. The new build was
  ahead in 4 of the 5 rounds, but single races vary (19.7-22.1 against 17.6-20.7), so the frame-rate
  gain is about as big as that spread: expect it on average. The CPU saving is clearer: the sound
  thread needs 65% of a core instead of 74%, and on all cores GT5's SPURS jobs (the sound mixer and
  the other job-chain work) take 22.2 s of CPU per minute of driving instead of 27.6 s. The SPU worker
  threads (physics and the other Wws jobs) changed less than one race differs from the next. These
  races are timed while driving, so their frame rates are lower than v0.1.28's (timed partly after the
  finish): compare them with each other.
- **SPU registers stay in CPU registers.** The SPU lifter (`spu_lifter.py`) stored every SPU register
  to memory after every instruction and read it back for the next, and every local-store write made the
  compiler load them all again. A lifted function now keeps the registers it uses in local variables
  and stores them only where something else can look: before a call, a channel operation (DMA,
  mailbox), a stop or a branch out of the function; a loop inside a function keeps them from one pass
  to the next. `--no-reg-locals` gives the old output.
- **No ten-register save on every SPU function:** on Windows the compiler had to save and restore the
  vector registers xmm6-xmm15 in every lifted function that used them, and most are one basic block
  long. Lifted functions now call each other with the System V convention, which doesn't keep those
  registers; a small Windows-convention wrapper saves them once, when the runtime calls in.
- **Cheaper SPU instructions:** `shufb` handles its special selector bytes with one table lookup
  instead of three compares and two blends; `cbd`/`chd`/`cwd`/`cdd` and their `x` forms (how SPU code
  stores a byte, halfword or word) build their masks in registers instead of through memory, which the
  CPU could not forward (a stall of a dozen cycles each time); `nand`/`nor`/`andc`/`orc`, the immediate
  shifts and the quadword bit shifts are single vector instructions. Each is checked bit for bit
  against the old code.
- **Less copying per SPURS job:** each job got a fresh 256 KB local store from `calloc`, was copied
  into a context that had just been zeroed and was copied out again afterwards, about 1 MB of memory
  traffic per job at about 2000 jobs a second. Each job thread now reuses one context and builds the
  job in it. Lifted SPU code also skips the stack-cookie check (`/GS-`).
- Tried and dropped: lifting the SPURS jobs in merged pieces (`--merge-chunks`) made the sound mixer
  slower (1.42 ms a job against 1.06), and lifting the Wws jobs unmerged showed no difference.
- **Re-lift once:** the lifter changed, so `update.ps1` re-lifts by itself.
- Checked: a test runs the old and the new lift of the same SPU programs from every entry point on
  identical random states and compares registers, local store and DMA/channel traffic: no difference in
  20,316 runs (the sound mixer, a physics job, and the largest Wws job, merged and unmerged). Race sound
  recorded before and after: the same level and spectrum, no clicks.

🏷️ Released as **v0.1.28**

🚀 **About 10% faster on small CPUs**
- Measured on 4 of the test PC's efficient cores, where the CPU is the limit as on a laptop, with
  nothing else running and five races per build. They averaged 29.6 game frames a second, against 26.9
  before. The CPU time the game needs per frame fell from about 150 ms to about 135 ms. Single races
  vary a lot (21-33), so expect the gain on average, not in every race. Fast desktop CPUs, already near
  the game's 60, change little.
- **Waits that no longer spin:** GT5's threads wait for SPU results by "yielding" in a loop. On a CPU
  with few cores that loop spins at full speed, because nothing else is queued on its core, and on a
  laptop it eats the power the other game threads need. When the game may use 8 logical CPUs or fewer,
  a thread that keeps yielding now naps 50 microseconds on a high-resolution timer. The same goes for an
  SPU program re-reading memory while it waits. The idea comes from Hookshot (the Ocarina of Time recomp
  for Switch 2), which measured the same kind of gain. `PS3_YIELD_NAP=0` turns it off, `=1` turns it on
  on any CPU.
- **Four speed-ups from the upstream ps3recomp project (v0.19.0):**
  - SPU memory reads and writes in one instruction each (#637);
  - PPU vector multiply-add without a maths-library call per lane (#674);
  - per-thread caches for function lookups (#634);
  - lock-line waits that sleep after about 1 ms (#666).
- For testers: `SPURS_DISPATCH_STATS=1` prints how often each SPU workload runs and how long, every 5 s.

🧰 **Build without Visual Studio** ([#22](https://github.com/MormonFTP-hub/gt5-recomp/issues/22))
- `get-toolchain.ps1` gets the same compiler tools as Visual Studio, in one folder on any drive you
  choose: LLVM 19.1.5 (clang-cl), CMake and Ninja from their official GitHub pages, and Microsoft's C++
  libraries and Windows SDK through mmozeiko's portable-msvc.py.
- It downloads about 1.5 GB and keeps about 1.5 GB. No administrator rights, nothing installed into
  Windows, and every download is checked against a pinned hash.
- Microsoft's part comes under Microsoft's licence: the script shows the link and goes on only when you
  type `yes`.
- `setup.ps1` offers it when it finds no Visual Studio. The build uses Visual Studio when it's there,
  and these tools otherwise.
- Tested by building the game with Visual Studio hidden. The step that fetches Microsoft's part is
  the one piece not run here, so please report how it goes.

## 12 Nov 2026

🏷️ Released as **v0.1.27**

🏁 **B-Spec mode for laptops: watch mode can draw 1 frame in 3 or 4** (asked for by a laptop tester)
- Launcher, Display: **Watch mode draws** sets how many frames watch mode draws: 1 in 2 (as before),
  1 in 3 (laptops), 1 in 4 (the weakest PCs). With **Watch mode by itself** ticked, it comes on after
  10 s without touching the controls: B-Spec races, replays. F9 still switches it any time. The race
  keeps full speed; fewer drawn frames leave more of the CPU to the game. The window title shows the
  game's own rate next to the drawn one.
- In `play.bat`: `set "LD_WATCH_DRAW=3"`.
- Speed: 30 fps keeps drawing 1 frame in 2, whatever this is set to.

🎮 **Steam Deck:** the README has a route to try: the Linux build in an Ubuntu distrobox, not tried on
a Deck so far. On SteamOS, `setup.sh` now explains that route instead of failing, and inside a
distrobox it prints the line that adds the game to Steam.

🏷️ Released as **v0.1.26**

🔊 **The sound mixer costs about 12% less CPU** (from a laptop tester's HWiNFO log in the Discord)
- A tester's i5-1135G7 laptop was at its power limit 88% of the time, with its graphics only 30% busy.
  On a laptop every busy thread shares one power budget, so CPU work saved anywhere lets the cores
  that run the game clock higher.
- GT5's sound runs as an SPU job that mixes in real time, whatever the frame rate, and took about 85%
  of one core on the tested rig. Now it takes about 75%. Two changes in the SPU emulation make that
  saving:
  - The byte-shuffle instruction (`shufb`), one of the most used, does two shuffles instead of six.
    Checked against the reference version on 200,000 random cases. Every SPU program gains from it.
  - The lookup that finds the compiled code for each SPU job kept only 8 job images, while GT5 uses
    more in a race. So it kept re-reading and hashing whole images; it keeps 64 now.
- Frame rates weren't measured: the test PC's graphics card was busy with another game.

🏷️ Released as **v0.1.25**

🖥️ **The launcher fits 4K and other high-scaling screens** ([discussion #9](https://github.com/MormonFTP-hub/gt5-recomp/discussions/9))
- On a 4K screen at 200% the launcher's text grew with the scaling but its boxes didn't. The title, the
  page menu and the Play button were cut off, and scroll bars appeared.
- Now Windows scales the whole window and keeps the text sharp (Windows 10 1809 and later). At 100%
  nothing changes. Checked at 100% only: if it still looks wrong on your screen, a screenshot in #9
  helps.
- The game window itself: Display, Window, *Borderless full screen* (or *Maximized*) fills a big screen.

🏷️ Released as **v0.1.24**

🐧 **Linux: a native build** ([#20](https://github.com/MormonFTP-hub/gt5-recomp/issues/20))
- `setup.sh` does on Linux what `setup.ps1` does on Windows: checks the tools (`--install-deps`
  installs them with apt, dnf, pacman or zypper), fetches and patches ps3recomp, makes the Python
  environment, checks your game files, generates the game code from your `EBOOT.elf` and builds
  `GT5/build-linux/gt5`. `GT5/play.sh` starts it, like `play.bat`. Updating: `git pull`, then
  `bash setup.sh`.
- The game code generated on Linux is the same as on Windows (checked chunk by chunk). It's built
  with Clang with the Windows build's settings, and draws with Vulkan, the Android build's renderer.
  Window, keyboard, controllers and sound go through SDL2; movies and music through the system's
  FFmpeg (6.1 or newer).
- The keyboard works on Linux too (same keys as on Windows) while no controller is connected.
- Tested on Ubuntu 24.04 with no graphics card (Mesa's software Vulkan, a virtual display): setup
  from a fresh clone, the first-start screens (saved), the top menu with its 3D scene and menu music,
  GT Mode, and Arcade up to the pre-race screen with the 3D track. The race itself stayed black there;
  races on a real Linux PC with a graphics card are untested. Proton and Wine weren't tried.
- Fixed on the way, for Linux and the Android build: GT5 gives the graphics chip 5 seconds to finish
  a frame and otherwise rebuilds commands it hasn't run yet, which froze both the graphics and the SPU
  jobs for good. Windows already held that wait while the graphics side was still busy (compiling
  shaders on a new scene); Linux and Android now do too. Seen as a freeze after car select and on the
  race loading screen with the software driver.
- Windows: nothing changes in how `gt5.exe` runs (the new code is Linux-only or unused there).

🏷️ Released as **v0.1.23**

💬 **Discord server**: [MormonFTP's Workshop](https://discord.gg/9cVPS7r5Ha) for live help, test reports and new releases; linked from the top of the README, the issue forms and the New issue page.

⚙️ **Speed: 30 fps is now off unless you choose it, and a launcher bug that kept it on is fixed** ([discussion #1](https://github.com/MormonFTP-hub/gt5-recomp/discussions/1))
- A player's test (Ryzen 5 3600, GTX 1650 SUPER) showed what 30 fps does on a typical 6-core PC.
  It drew half the frames (15, 14 and 19 a second against 29, 27 and 35), but the game itself ran at
  the same speed. GT5's pace on most PCs is set by the CPU (the game's own threads), so drawing fewer
  frames only helps when the graphics card is the limit.
- The launcher no longer picks 30 fps by itself for small CPUs and laptops, as v0.1.20 to v0.1.22 did.
  Installs it set to 30 fps go back to drawing every frame once; you can choose 30 fps again under
  Display. The help text now says when it helps.
- **Bug fixed:** once Speed had been saved as 30 fps, the next launcher start read the extra lines it
  writes back as settings of their own. Setting Speed back to "Draw every frame" in a later session
  then left the game at 30 fps. The launcher repairs such a `settings.bat` once.
- While some frames go undrawn (30 fps, watch mode), the window title also shows the game's own rate,
  for example `FPS: 15.00 (game 30)`. The game number is the one that says how fast the game runs.

⏱️ **The 1 ms timer stays on when the window is hidden.** The game asks Windows for 1 ms timer steps,
because its short waits get much slower at Windows' default 15.6 ms. Windows 11 drops that request for
a program whose window is minimized or hidden. The game now asks Windows to keep it. Older Windows
versions ignore the request. Its effect on speed isn't measured yet: the test PC was too busy today.

🧾 **Installer:** it says the Hong Kong / Asian release (BCAS20108) runs. For a copy with an update
built in, it says update 2.01 is tested and that the update's data has to be copied too (README,
[Game updates](README.md#game-updates)). Before, it said updates don't work.

🏷️ Released as **v0.1.22**

🎮 **Game updates (2.xx) run, and the Hong Kong / Asian release is tested** ([discussion #18](https://github.com/MormonFTP-hub/gt5-recomp/discussions/18))
- Tested with the Asian release BCAS20108: its disc executable 01.00 and its game update 2.01. Both get
  through the menus and race in Arcade Mode (High Speed Ring); 2.01 also loaded a long-played career save
  made in RPCS3.
- What the update executable needed:
  - The lift scripts from v0.1.20: the jump table the game fills while it runs is found in any
    executable now, and the update reaches setjmp through linker stubs (its code needs more than one
    TOC), which count as setjmp calls.
  - Two SPU jobs (programs for the PS3's co-processors) changed in 2.01. Their new versions are on the
    list now, so setup says `SPU jobs found in EBOOT.elf: 15 of 15`. A job the port doesn't have can't
    run at all: the game stays black after the logos and `play.err` has a line ending in `is DROPPED`.
  - Two system functions 2.01 calls (the SPURS "workload flag") are in the runtime now. Without them
    it got an unset pointer back.
- **An update needs its data too.** Besides `EBOOT.BIN` it brings files the game reads from the PS3's
  hard disk (about 2.2 GB in `USRDIR\PDIPFS` for 2.01). Without them the game starts, but Arcade Mode
  has no courses to choose. How to set an update up: README,
  [Game updates](README.md#game-updates). The installer doesn't do it yet.
- Not tried yet: 2.11 (what the GT5 Master Mod needs), 2.17 (the last update) and other regions'
  updates. Each can change SPU jobs again; if setup finds fewer than 15, please post its lines.
- Frame rates on 2.01 aren't measured yet: the test races ran with shaders still compiling and other
  builds running on the same PC.
- Setup now names the Asian release's executables (01.00 and 2.01) when it checks `GT5\input\EBOOT.elf`.

🎬 **The opening movie is skipped, so it can't freeze the first start** ([#10](https://github.com/MormonFTP-hub/gt5-recomp/issues/10))
- Found while testing on a slower PC: under heavy CPU load GT5's opening movie (the Earth from space)
  could stop on one frame for good. The movie's decode jobs stopped coming, skipping did nothing, and
  only a restart helped. That fits the "stuck at the start, restarting got past it" reports.
- The port now tells the game the opening movie isn't there while it looks for it at startup, and GT5
  goes straight on to its menus: about 40 s quicker to the menus in a test run. GT TV still plays it.
- On by default (`PS3_SKIP_INTRO=1` in `play.bat`); the launcher's Game page has **Skip the opening
  movie** to switch it off.
- Speed on 4-6 core CPUs: the next step was measuring which SPU settings help; those runs were spoiled
  by other programs using the CPU at the same time, so there's no result to report yet.

🏷️ Released as **v0.1.20**

🔁 **Updating to v0.1.20 re-lifts the game once**, so it takes longer than a normal update. The lift
scripts learned about the executables of the game updates (2.xx, work in progress for the Hong Kong
release); on 01.00 discs they give exactly the same result as before.

💻 **Laptops draw on their dedicated graphics card**
- The renderer took whichever GPU Windows listed first. On a laptop with two GPUs that's usually the
  built-in one the screen is wired to, so a GTX 1650 laptop could end up drawing on Intel graphics. It now
  asks Windows for the high-performance GPU, and tells the NVIDIA and AMD drivers the same.
- `GT5\logs\play.err` now says which GPU draws: a line starting with `[gpu]`. `PS3_GPU=low` picks the
  power-saving one instead, `PS3_GPU=<n>` the n-th one Windows lists.
- Checked on the tested rig (one GPU: it picks it and logs it); not yet on a laptop with two.

🐢 **Speed setting in the launcher (Display)**
- **30 fps** draws every other frame and leaves that CPU time to the game, which keeps its normal 60 steps
  a second when it can (GT5 counts time in frames, so a frame limiter like RTSS slows the whole game down
  instead: discussion #1). Measured on 6 slow cores of the tested rig: races at about 24 game frames a
  second instead of 22, so a small gain there; more where drawing is what holds a PC back.
- The first time the launcher starts it picks for your PC: 30 fps and 720p on CPUs with 8 threads or
  fewer, and on laptops with 12 or fewer.
- What that test also showed: on those 6 cores the CPU wasn't full and the drawing wasn't the limit; the
  game's own threads handing work to each other set the pace. That's what's being looked at next.
- `LD_FRAME_SKIP=1` (experimental, off) now keeps the passes the game doesn't redraw every frame, like
  watch mode. In a full-speed test race it never needed to skip a frame.

🏷️ Released as **v0.1.19**

🧩 **Black screen from a game copy whose GT.VOL is in pieces, or missing** ([#12](https://github.com/MormonFTP-hub/gt5-recomp/issues/12))
- `GT.VOL` is the game's data (about 12 GB). Dumps made to a FAT32 USB drive split every file over 4 GB
  into pieces (`GT.VOL.66600`, `GT.VOL.66601`, ...), the installer copied them as they were, and the game,
  finding no `GT.VOL`, started to a black screen. Now `Update.bat`, setup and every build join the pieces
  into one file (new `GT5\scripts\fix_game_files.ps1`), and say plainly when `GT.VOL` is missing altogether.
- The launcher's Play page warns about it, and **Play** offers to join the pieces first.
- FAQ: what to look for in `play.err` when it's still black (`open FAIL ... GT.VOL`).

🏷️ Released as **v0.1.18**

📀 **The installer knows GT5 by its Chinese, Japanese and Korean names too**
([discussion #18](https://github.com/MormonFTP-hub/gt5-recomp/discussions/18))
- The Hong Kong release (BCAS20108, titled GT賽車5) was turned away with "not Gran Turismo 5", because
  only titles with "Gran Turismo" in them got through. Now GT賽車5 / GT赛车5, グランツーリスモ5, 跑車浪漫旅5
  and 그란 투리스모 5 do too (Prologue still doesn't), with the usual "not tried yet" note.
- A copy with a game update built in (its game version 2.xx instead of 01.00) now says so straight away:
  the port needs the disc's own 01.00 executable, and update executables don't work yet.

🏷️ Released as **v0.1.17**

⌨️ **Alt+Enter no longer freezes the picture** ([#10](https://github.com/MormonFTP-hub/gt5-recomp/issues/10))
- Alt+Enter froze the picture while the game kept running (found by Arpadiam): Windows' own Alt+Enter
  switched the window to exclusive full screen behind the renderer's back. The port now turns that off,
  so Alt+Enter does nothing. For full screen, use the launcher's **borderless full screen**.

🏷️ Released as **v0.1.16**

🛠️ **Wrong track textures fixed** ([#14](https://github.com/MormonFTP-hub/gt5-recomp/issues/14))
- Some track textures showed another texture's picture: sponsor banners across the road and noise on
  the fences at Chamonix in the snow, scrambled textures at Eiger for a player. GT5 moves textures
  around in graphics memory to make room for new ones (it defragments its texture memory), and moves
  the data with a copy command for the PS3's graphics chip (NV0039, "memory to memory format"). The
  port never carried that command out, so a moved texture showed whatever had been in its new place
  before, usually another texture of the same size. It now does the copy. Which tracks it hit depended
  on what the game had loaded before, which is why it differed from PC to PC.
- Checked at Chamonix in the snow and at Eiger in the rain: roads, fences, banners and buildings
  look right. A check of every texture in a Chamonix race went from about 100 mismatched to 26; a few
  (some trees and see-through masks) can still show another texture, and that is being worked on.
- The "Wrong textures" note further down (from earlier today) was wrong about the cause: the texture
  data was loaded and unpacked correctly all along; the copy afterwards was what went missing.

🏷️ Released as **v0.1.15**

🎛️ **The launcher** (asked for in [#14](https://github.com/MormonFTP-hub/gt5-recomp/issues/14))
- `GT5Launcher.exe` in the install folder, and **Gran Turismo 5 (PC)** on the desktop now opens it: one
  window with every setting, then **Play**. Display: 1080p/720p, screen shape, **borderless full screen**
  (or maximized, or a window size), watch mode, SPU threads. Controls: steering wheel, rotation, force
  feedback strength and direction, pad in the background. Game: the game's language, mods from
  `GT5\mods`. Updates: your version, the newest one and what's new, and **Update now**. Help: **Save
  logs for a report** (your logs on the desktop as `.txt`, which GitHub accepts), the logs folder, the
  problem form. In English or Chinese (asked for in discussion #18).
- It's built on your PC with the C# compiler that comes with Windows, each time the game is built, so it
  arrives with this update by itself. Settings go to `GT5\settings.bat`, which `play.bat` reads after its
  own lines, so `play.bat` still starts the game the same way.
- **Updating is a double-click:** the launcher's **Update now**, or `Update.bat` in the install folder.
- **README reorganised:** a link bar at the top (Install, Launcher, Updating, Version history, Changelog,
  Status, FAQ, Modding, Contributing, License), a table of contents, and the sections in order: getting
  started, how it's going, setting up by hand, about the project. The version history has its own page,
  [VERSIONS.md](VERSIONS.md); each release adds its line.
- The installer lets other GT5 releases (Japan, Asia) through with a warning instead of stopping: they
  haven't been tried, so please report how they go.

🏷️ Released as **v0.1.14**

🔧 **Update stopped with "Access is denied"** ([#15](https://github.com/MormonFTP-hub/gt5-recomp/issues/15))
- The build could stop with `ninja: error: ... GetFileAttributesEx(...GT5/spu/sources.cmake): Access is
  denied`. On Python 3.12.4 and newer, the step that compiles the SPU code made `GT5\spu` readable only
  by its owner, SYSTEM and Administrators; made from an Administrator window, a normal window then
  couldn't read it. The step now makes the folder with the install's normal permissions, and the build
  first puts them back on a `GT5\spu` an older version made (Windows asks once when only an
  administrator can do that).
- This version re-lifts once when you update (about 10 minutes), which also rebuilds `GT5\spu`.

🏷️ **Version in the window title**
- The window title shows the port's version, e.g. `Gran Turismo 5 (recomp v0.1.14) | FPS ...`, read
  from what `update.ps1` installed (asked for in #15).

📝 **Reporting problems**
- The problem form lists the installer and `update.ps1` as steps, asks for the port's version and, for
  a picture problem, the mode, track and its time or weather, and has a field for attaching logs.
  GitHub refuses `.err` files, so the form and the README FAQ say to rename `play.err` to
  `play.err.txt` (or zip it) first.

🔎 **Wrong textures on some tracks** ([#14](https://github.com/MormonFTP-hub/gt5-recomp/issues/14))
- Reproduced at Chamonix in the snow: sponsor banners across the road, noise on the fences. Traced to
  the game's texture streaming: the SPU job that unpacks each track texture into graphics memory works
  correctly (its output matches unpacking the same data on the PC), but some slots are handed another
  texture's compressed data. Which tracks it hits varies (Eiger for a player), so it looks
  timing-related. Ruled out so far: the renderer and its texture cache, file reading (including offsets
  past 4 GB in the 12 GB GT.VOL), the SPU job's compiled code (same result interpreted), extra SPU
  threads, and overlapping graphics memory. Still being narrowed down.

## 11 Nov 2026

🏷️ Released as **v0.1.13**

🩹 **Hotfix: v0.1.12 didn't install**
- Setup and `update.ps1` stopped with "this version's patch doesn't apply to ps3recomp"
  ([#15](https://github.com/MormonFTP-hub/gt5-recomp/issues/15), [#16](https://github.com/MormonFTP-hub/gt5-recomp/issues/16)).
  Two files of the patch were rebuilt for v0.1.12 without one stray carriage return that ps3recomp
  has in them, so the patch no longer matched a fresh copy. The game code is unchanged from v0.1.12.
  Stuck on that message? Run `update.ps1` again (or the installer).
- The release check now tests the patch on a copy of ps3recomp made the way setup makes it, which
  rejects the v0.1.12 patch; the old check folder converted line endings and let it through.

🏷️ Released as **v0.1.12**

🖥️ **1080p, fixed**
- The window's buffer now follows the video mode: 1920×1080 in the 1080p mode (still stretched to fit
  the window), so the whole picture is shown. `play.bat` is back to `PS3_VIDEO_MODE=1080`.
- Checked in what the window gets this time: the test frame dumps (`LD_FRAME_DUMP`) now save the part a
  present copies to the window, not the game's whole surface. A 1080p Arcade race and its menus and
  intro movie showed in full at 1920×1080; a 720p race still works.
- `logs\play.err` names textures and other reads from main memory that has no GPU mapping at that
  moment (`[io-unmapped]`), for tracking down wrong textures (Chamonix in the snow shows track
  textures on the wrong surfaces; being looked into).

🏷️ Released as **v0.1.11**

🩹 **Hotfix: 720p is the default again**
- In v0.1.10's 1080p mode the window showed only the top-left two-thirds of the picture: the game drew
  1920×1080, but the window's buffer stayed 1280×720 and took the corner of it. The tests had checked the
  game's own frames, not the window. `play.bat` is back to `PS3_VIDEO_MODE=720`; 1080p returns once the
  window follows the mode. Updated to v0.1.10? Run `update.ps1` again, or set `PS3_VIDEO_MODE=720` in
  `GT5\play.bat` yourself.

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
