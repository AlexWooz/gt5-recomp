# GT5 Recomp

An unofficial PC port of **Gran Turismo 5** (PS3) made by static recompilation: the game's
PowerPC and SPU code is translated into C++ that is compiled for Windows x64, and the PS3's graphics,
sound, input and system calls are reimplemented on Windows (Direct3D 12 for the RSX).

It is built on [ps3recomp](https://github.com/sp00nznet/ps3recomp) and borrows from the
[GT5 Prologue port](https://github.com/sp00nznet/GT5P), both by Ned Heller (sp00nznet).

> **Game version required:** your own **Gran Turismo 5** Blu-ray disc, using the disc's own
> executable (version **01.00**, with **no game update** installed):
>
> - **Europe, BCES00569**: tested. This is the copy the port was built from.
> - **North America, BCUS98114**: expected to work, not tested yet. The lift finds the few addresses
>   that differ between releases in your own executable.
>
> See [Which copy of GT5](#which-copy-of-gt5) for how to check your files.

## What this repository contains, and what it does not

This repository holds only code and scripts written for the port:

- `GT5/patches/ps3recomp.patch`: the changes to ps3recomp (runtime, graphics, SPU, audio, input).
- `GT5/CMakeLists.txt`, `GT5/play.bat`, `GT5/src/`, `GT5/config/`: the GT5 build and launcher.
- `GT5/scripts/`: the lift, build, test and analysis tools.

It does **not** contain, and must never be given:

- any part of the game: the EBOOT, disc or installed files, music, textures, models, shaders,
  menu images, save data, or screenshots;
- code generated from the game (the recompiled `generated/` and `spu/` folders, `src/gen/`): every
  user produces these from their own copy;
- PS3 firmware, system modules, or any decryption key.

You need your own copy of Gran Turismo 5 (BCES00569 or BCUS98114), dumped from a console you own, and PS3 system
software installed from Sony's official update file. Use this only with games you own.

- **Never share what you build.** `gt5.exe`, the `generated/` folder and anything else made by the
  lift contain code recompiled from the game, so each person has to build them from their own copy.
  Don't upload them or a ready-to-play package, and don't ask for them.
- This repository does not include, link to, or explain how to obtain decryption keys or game files.
- Questions about downloaded copies of the game will not be answered.

**Gran Turismo** is a trademark of Sony Interactive Entertainment. This project is not affiliated
with, endorsed by, or connected to Sony Interactive Entertainment or Polyphony Digital.

## Status

- Boots, menus, A-Spec and B-Spec races, licence tests, special events, replays, Photo Mode, the
  Course Maker and GT TV work in testing.
- Performance depends mostly on the CPU: on a fast desktop CPU menus run at 60 fps and races at roughly
  45 to 60 fps. The first visit to a scene stutters while its shaders compile; later visits load them
  from the shader cache.
- Online features do not work. Some graphics issues remain.

## Requirements

- Windows 10 or 11 (x64) and a Direct3D 12 graphics card.
- Visual Studio 2022 with *Desktop development with C++* and *C++ Clang tools for Windows*
  (provides clang-cl, CMake and Ninja).
- Python 3.11 or newer, and Git.
- About 25 GB free: the game files are about 17 GB, the build a few more.
- Your own GT5 disc (BCES00569, or BCUS98114 untested) and the means to dump it, and
  [RPCS3](https://rpcs3.net) to install PS3 firmware and decrypt the game's executable.

## Which copy of GT5

Use the Blu-ray disc's own executable, `PS3_GAME\USRDIR\EBOOT.BIN`, application version **01.00**
(the `APP_VER` in `PS3_GAME\PARAM.SFO`). Executables from game updates are not supported.

| Release | Title ID | Status | SHA-1 of the disc's files |
|---|---|---|---|
| Europe / PAL | **BCES00569** | tested | `EBOOT.BIN` `1FFABF4C029CF3099579B73425C91C1FD48CAFCA`, decrypted `EBOOT.elf` `1535B865EC7DD58E7704B378B8621532ECF844E1` |
| North America | **BCUS98114** | expected to work, not tested yet | |

Almost everything in the port works for any release, because the recompiled code is generated from
your own executable. Only three things are tied to where the code sits in one executable: the end of
the code, the game's setjmp/longjmp routines, and one jump table the game builds while it runs.
`GT5\scripts\relift.ps1` starts by running `GT5\scripts\version_addrs.py`, which finds all three in
your EBOOT by what the code looks like, not by address, and prints your EBOOT's SHA-1:

- If every line is found, carry on: your release should work.
- If it prints `NOT FOUND`, that release differs in those places. Please open an issue with that
  output (the text only, never any game file).

Check a file's hash with `Get-FileHash <file> -Algorithm SHA1` in PowerShell.

## Setup

1. Clone this repository and run `setup.ps1` in PowerShell. It clones ps3recomp at the commit the
   patch was made against and applies the patch, creates the Python environment, and downloads the
   FFmpeg LGPL build that plays the game's movies.
2. Dump your own GT5 disc (BCES00569 or BCUS98114) and copy its `PS3_GAME` folder to
   `GT5\vfs\PS3_GAME`.
3. In RPCS3, install the PS3 firmware (File > Install Firmware, with `PS3UPDAT.PUP` from Sony), then copy
   RPCS3's `dev_flash` folder to `GT5\dev_flash`.
4. In RPCS3, decrypt the executable (Utilities > Decrypt PS3 Binaries) by choosing your disc's
   `PS3_GAME\USRDIR\EBOOT.BIN`. Copy the decrypted file to `GT5\input\EBOOT.elf`, and copy the disc's
   `EBOOT.BIN` and `PS3_GAME\PARAM.SFO` to `GT5\input` as well. For the European release, check both
   hashes against the table above.
5. Generate the recompiled code from your executable: `GT5\scripts\relift.ps1` (about 10 minutes).
   Read the `version_addrs` lines it prints first (see [Which copy of GT5](#which-copy-of-gt5)).
6. Build: `GT5\scripts\build.cmd gt5` (the first build takes a while).
7. Play: `GT5\play.bat`. Saves go to `GT5\hdd0`; logs go to `GT5\logs`.

When it is set up, the folder looks like this (the parts you add are marked):

```
gt5-recomp\
  setup.ps1, README.md, LICENSE
  ps3recomp\          fetched and patched by setup.ps1
  ffmpeg\             fetched by setup.ps1
  venv\               made by setup.ps1
  GT5\
    input\            YOU ADD: EBOOT.BIN, EBOOT.elf (decrypted), PARAM.SFO
    vfs\PS3_GAME\     YOU ADD: the PS3_GAME folder from your disc
    dev_flash\        YOU ADD: from RPCS3 after installing Sony's firmware
    generated\, src\gen\   made by relift.ps1 from your EBOOT.elf
    build\            made by build.cmd (gt5.exe)
    hdd0\, logs\      made when you play
```

Optional, for speed: GT5 runs many SPU jobs. They run in an interpreter until they are captured and
recompiled. Run the game once with `SPU_DUMP_MISS=<GT5 folder>\analysis\spu_miss` set, then run
`python GT5\scripts\lift_spu_jobs.py` and build again (see the comments at the top of that script).

## Controls

A DualSense, DualShock 4 or XInput pad works. Without one, the keyboard:
Z = Cross, X = Circle, A = Square, S = Triangle, arrow keys = D-pad, Enter = Start, Tab = Select.

## Switches

`play.bat` sets the usual environment variables. Some useful ones:

| Variable | Effect |
|---|---|
| `PAD_BACKGROUND=1` | The pad keeps working when another window has focus. |
| `GCM_DRAIN_STAT=1` | Timing lines every 5 s in `logs\play.err`. |
| `PS3_MUTE=1` | No sound. |
| `RSX_SHADER_CACHE_DIR=<dir>` | Where compiled shaders are cached (default `scratch\rsx_shader_cache_v2`). |

Most speed-ups in the patch can be switched off one by one to rule them out when something looks
wrong; each is described in a comment next to its `getenv` in the patch.

## Credits

- [ps3recomp](https://github.com/sp00nznet/ps3recomp) and [GT5P](https://github.com/sp00nznet/GT5P)
  by Ned Heller (sp00nznet), MIT licensed; their licences are in `licenses/`.
- ps3recomp's live D3D12 renderer began as caner (canersaka)'s work for Yakuza: Dead Souls.
- RPCS3, envytools and Mesa's nv30 driver were consulted as references for hardware behaviour.

## License

The code in this repository is under the MIT License (`LICENSE`). The patch modifies ps3recomp, which
is also MIT licensed. FFmpeg is not included; `setup.ps1` downloads an LGPL build of it.
