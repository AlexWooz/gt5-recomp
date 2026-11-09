# Contributing

**Forks are welcome.** The code is MIT licensed: fork it, change it, take what you like.

If you make something good (a fix, more speed, a mod, wheel support, easier setup), please let me know
so I can try it and maybe bring it into the main repo:

- open a pull request,
- fill in the [fork/mod form](https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=4-my-fork-or-mod.yml)
  with a link to your fork or branch, or
- message me on Discord: **mormonftp**.

## Rules for pull requests and forks

- **No game content, ever:** no files from the disc, no `gt5.exe` builds, nothing from `GT5\generated`,
  `GT5\spu` or `GT5\src\gen`, no firmware, no keys. Everyone builds from their own copy.
- Changes to ps3recomp go into `GT5/patches/ps3recomp.patch`. Regenerate it from a ps3recomp that
  `setup.ps1` checked out, with `git -C ps3recomp diff --output=../GT5/patches/ps3recomp.patch`.
- Put a change that might not suit everyone behind an environment-variable switch, and say in a
  comment how to turn it off.
- Say how you tested: which release (BCES00569 or BCUS98114), your CPU and graphics card, and what you
  played.
- Game-code mods go in `GT5/mods/<name>/mod.json` (see [MODDING.md](MODDING.md)).

## Reports and ideas

Use the [issue forms](https://github.com/MormonFTP-hub/gt5-recomp/issues/new/choose): a report of how
it went, a problem, or an idea. Quick results and questions go in the
[discussions](https://github.com/MormonFTP-hub/gt5-recomp/discussions).
