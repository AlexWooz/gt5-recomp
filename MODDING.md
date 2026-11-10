# Modding

There are three ways into the port, from no code at all to changing the PC side itself. Whatever you
make, share **your own work only**: never the game's files, `gt5.exe` builds or the generated code.
When you've made something, tell us with the
[fork/mod form](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=4-my-fork-or-mod.yml)
or on Discord (**mormonftp**).

## 1. Your own game files

The game reads its data from `GT5\vfs\PS3_GAME` exactly as the disc lays it out, so changing a file
there changes the game on your PC. Keep a backup of anything you replace. To share such a mod, share
your new content and instructions or a tool, never files that came off the disc.

## 2. Game-code mods (`GT5\mods`)

A game-code mod changes instructions in the game's own code. It lives in a folder with a `mod.json`:

```json
{
  "name": "unlock-fps",
  "description": "What it does, in a sentence.",
  "default": false,
  "patches": {
    "1535B865EC7DD58E7704B378B8621532ECF844E1": [
      {"addr": "0x008CFFB4", "orig": "0x8089001C", "new": "0x38800000", "note": "why this works"}
    ]
  }
}
```

- Each patch replaces one 32-bit PowerPC instruction at `addr`. `orig` is the instruction there now
  (checked against your EBOOT, so a mod can't land in the wrong place), and `new` is its replacement.
- Patches are listed per release, under the SHA-1 of the decrypted `EBOOT.elf` that
  `scripts\version_addrs.py` prints. A mod has no effect on a release it has no list for.
- `relift.ps1` collects every mod (`scripts\mods.py`) and builds both versions of each patched
  instruction into the game, so a mod costs nothing while it's off. It is switched at start-up by the
  environment variable `GT5_MOD_<NAME>` (`set "GT5_MOD_UNLOCK_FPS=1"` in `play.bat`), or by
  `"default"` when that isn't set. `logs\play.err` lists each mod as on or off.
- After adding or changing a mod, run `scripts\relift.ps1` and `scripts\build.cmd gt5` again.

`GT5\mods\unlock-fps` is a working example. It's experimental: GT5's clock counts frames, so with it
on, races run faster than real time whenever the frame rate goes over 60.

Some changes are data rather than instructions, such as a constant the game reads. Try one without
building anything with `set "PS3_POKE32=<address>=<value>"` (hex, big-endian 32-bit words, comma
separated; `logs\play.err` shows the old values). To keep one, do as `GT5\src\gt5_widescreen.cpp`
does for ultrawide: a function registered with `ps3_on_loaded` runs once the EBOOT is loaded and
before the game starts, checks the values it expects, and writes the new ones.

Finding what to change: the lifted code in `GT5\generated` is readable C++ with one function per game
function (`func_<address>`), and the scripts help you watch the running game:

| Script | Use |
|---|---|
| `gstack.py`, `hoststack.py` | Where each game thread is right now (game and PC call stacks). |
| `gwatch.py`, `gmem.py`, `gvar.py` | Watch or read the game's memory while it runs. |
| `showfn.py`, `opdrefs.py` | Show a game function, and who refers to it. |
| `profsym.py` (with `prof.ps1`) | Where the CPU time goes, per thread. |

## 3. The PC side (ps3recomp)

Everything that stands in for the PS3's hardware and system software is C/C++ in `ps3recomp`, patched
by `GT5\patches\ps3recomp.patch`:

| Area | Where |
|---|---|
| Graphics (the RSX drawn with Direct3D 12) | `libs/video/rsx_live_draw.c`, `rsx_dispatch.c`, `rsx_fp_decompiler.c` (shaders) |
| Controller | `libs/input/cellPad.c` |
| Sound | `libs/audio/cellAudio.c` |
| Movies | `libs/codec/` |
| SPU (the PS3's co-processors) | `runtime/spu/` |
| Files and saves | `runtime/ppu/ppu_fs.cpp`, `libs/system/cellSaveData.c` |

Change the files in `ps3recomp\`, then `GT5\scripts\build.cmd gt5`. Put anything that might not suit
everyone behind an environment-variable switch (`getenv`), as the existing changes are. To share it,
fork this repository and refresh the patch from your `ps3recomp` folder:

```
git -C ps3recomp diff --output=../GT5/patches/ps3recomp.patch
```

## Ideas people would like

Tuning the new steering-wheel support (button and pedal defaults for more wheels, rev lights), a
resolution or render-scale setting, a wider field of view,
a frame-rate unlock that keeps the clock at real time, HUD toggles for screenshots, and anything that
makes setup easier. If you start on one, say so in the
[discussions](https://github.com/MormonFTP-hub/gt5-recomp/discussions) so work isn't doubled.
