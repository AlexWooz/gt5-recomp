# 📜 Changelog

Everything done on the port, newest first. All dates are 2026. Frame rates are from the European
release on the tested rig in [STATUS.md](STATUS.md) unless a day says otherwise.

> Frame rates before **9 Nov** were read from the window's FPS counter, which at times counted empty
> or repeated frames (fixed on 4 Nov and 9 Nov), so treat them as rough before-and-after comparisons.
> From 9 Nov on, numbers are the game's own frames.

## 9 Nov 2026

🎨 **Graphics**
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
