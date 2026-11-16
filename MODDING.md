# Modding

There are four ways into the port, from no code at all to changing the PC side itself. Whatever you
make, share **your own work only**: never the game's files, `gt5.exe` builds or the generated code.
When you've made something, tell us with the
[fork/mod form](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=4-my-fork-or-mod.yml)
or in the [Workshop Discord](https://discord.gg/9cVPS7r5Ha) (#dev-chat).

## 1. Your own game files

The game reads its data from `GT5\vfs\PS3_GAME` exactly as the disc lays it out, so changing a file
there changes the game on your PC. Keep a backup of anything you replace. To share such a mod, share
your new content and instructions or a tool, never files that came off the disc.

## 2. Textures and HD texture packs

GT5's textures can be written out as files and swapped for your own, at any size, the way HD texture
packs are made for GT4 on PCSX2. This works with the DirectX 12 renderer (the default); the Vulkan
renderer, and so Linux, doesn't do it yet.

**Dumping.** Add `set "PS3_TEX_DUMP=1"` to `GT5\settings.bat` (or `play.bat`) and play: every texture
the game shows is written once to `GT5\textures\dump\`, on a background thread. Drive the tracks and
cars you want, then take the line out again. A texture is written once it has stayed the same for 20
frames, so movies, the menus' animated backgrounds and anything else the game keeps rewriting are left
out, as are render targets (pictures the game draws itself, like mirrors), cube maps and 3D textures.
Files already in the folder aren't written again.

Each file is named `<hash>_<width>x<height>_<format>.<dds|png>`, for example
`543D80602521D493_1024x1024_dxt5.dds`:

- The hash (the first 16 characters) is worked out from the texture's pixels, so a texture gets the
  same name every time, on every PC, wherever the game happens to keep it in video memory.
- DXT1, DXT3 and DXT5 textures (nearly everything on tracks and cars) are written as **DDS**: the
  compressed data exactly as the game has it, nothing lost. Paint.NET, GIMP, Photoshop (with Intel's
  Texture Works plugin) and Microsoft's `texconv` open them.
- Everything else (A8R8G8B8 and the 16-bit formats, mostly menus and the HUD) is written as **PNG**
  with alpha, in the colours the game reads.
- `_rect` at the end of a name: the game reads that texture by pixel position, so a replacement has to
  keep exactly its size.
- Some textures are stored upside down or mirrored; that's how the game uses them, so keep them that way.

**Replacing.** Put your file in `GT5\textures\replace\` (sub-folders are fine, say one per pack) with
the same 16-character hash at the start of its name; the rest of the name is up to you. At the next
start the game uses it instead of its own.

- **Any size.** 2x or 4x the original is usual; keep the proportions. `_rect` textures only at their
  own size (`play.err` says so when a file is skipped for that).
- **Formats:** PNG (RGBA or RGB), or DDS in BC1/DXT1, BC2/DXT3, BC3/DXT5, BC7 or uncompressed 32-bit
  RGBA/BGRA. Compressed DDS sizes must be multiples of 4. No cube maps or arrays.
- **Mipmaps:** a DDS's own are used; otherwise they're made when the file loads (except for a BC7 DDS,
  which then shimmers in the distance; save BC7 with mipmaps).
- **For a big pack, use DDS with mipmaps** (BC1 for opaque textures, BC3 or BC7 with alpha). A PNG,
  or a BC1/BC3 DDS without mipmaps, is unpacked to 32-bit colour: 4 to 8 times the video memory, and
  slower to load.
- Files load in the background the first time their texture shows up (the game's own is shown for
  that moment) and are kept for the rest of the session. Changes to the folder are read at the next start.
- For DXT textures the file holds the channels as the game stores them, and the game's channel
  mapping still applies on top (normal maps, for instance, keep working). For the other formats the
  PNG is the finished picture and is used as it is.
- `set "PS3_TEX_REPLACE=0"` turns replacement off without moving any files, and
  `set "PS3_TEX_DIR=<folder>"` uses another folder instead of `GT5\textures` (it holds `dump\` and
  `replace\`).
- `logs\play.err` shows how many files were found (`[texpack] replace: N texture file(s)`), each file
  where it's first used, any file it can't use and why, and what it did every 10 s.

**Sharing.** Dumped textures come off the game disc: never share them, not even a few as examples or
"originals for comparison". Share only textures you made yourself, as your own download, and never on
this project's GitHub (no textures in issues, pull requests, releases or forks of it), the same rule
as for every other mod here.

## 3. Game-code mods (`GT5\mods`)

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

## 4. The PC side (ps3recomp)

Everything that stands in for the PS3's hardware and system software is C/C++ in `ps3recomp`, patched
by `GT5\patches\ps3recomp.patch`:

| Area | Where |
|---|---|
| Graphics (the RSX drawn with Direct3D 12) | `libs/video/rsx_live_draw.c`, `rsx_dispatch.c`, `rsx_fp_decompiler.c` (shaders) |
| Texture dumps and packs | `libs/video/rsx_texpack.c` (files, hashes), `rsx_live_draw.c` (`ld_tp_*`) |
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
