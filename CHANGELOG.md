# 📜 Changelog

Everything done on the port, newest first. All dates are 2026. Frame rates are from the European
release on the tested rig in [STATUS.md](STATUS.md) unless a day says otherwise.

> Frame rates before **9 Nov** were read from the window's FPS counter, which at times counted empty
> or repeated frames (fixed on 4 Nov and 9 Nov), so treat them as rough before-and-after comparisons.
> From 9 Nov on, numbers are the game's own frames.

## 13 Nov 2026

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
