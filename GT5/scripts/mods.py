#!/usr/bin/env python3
"""Collect the game-code mods in GT5/mods into the lifter's --insn-patches file and their switches.

    python scripts/mods.py            (relift.ps1 runs it before the lifter)

A mod is a folder GT5/mods/<name>/ with a mod.json:

    {
      "name": "unlock-fps",
      "description": "What it does, in a sentence.",
      "default": false,
      "patches": {
        "<SHA-1 of the decrypted EBOOT.elf it was written for>": [
          {"addr": "0x008CFFB4", "orig": "0x8089001C", "new": "0x38800000", "note": "why"}
        ]
      }
    }

Each patch replaces one 32-bit PowerPC instruction. The lifter emits both versions,
`if (g_mod_<name>) { new } else { original }`, so a mod built in costs nothing while it is off and
is switched at start-up by the environment variable GT5_MOD_<NAME> (1 on, 0 off; unset: "default").
Patches are listed per release because addresses differ between them; a mod with no list for
this copy's EBOOT is skipped. Every "orig" is checked against the EBOOT before anything is written.

Writes analysis/insn_patches.json and src/gen/mods.cpp when at least one mod applies; otherwise
removes them, so a build without mods is exactly a build without this script.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def eboot_words(path):
    """vaddr -> 32-bit word reader for the ELF's loadable segments."""
    d = path.read_bytes()
    e_phoff = struct.unpack(">Q", d[32:40])[0]
    e_phentsize, e_phnum = struct.unpack(">HH", d[54:58])
    segs = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, _, p_offset, p_vaddr, _, p_filesz, _ = struct.unpack(">IIQQQQQ", d[o:o + 48])
        if p_type == 1 and p_filesz:
            segs.append((p_vaddr, p_offset, p_filesz))

    def word(addr):
        for va, off, size in segs:
            if va <= addr < va + size - 3:
                return struct.unpack(">I", d[off + addr - va:off + addr - va + 4])[0]
        return None
    return hashlib.sha1(d).hexdigest().upper(), word


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", default=str(ROOT / "input" / "EBOOT.elf"))
    ap.add_argument("--mods", default=str(ROOT / "mods"))
    ap.add_argument("--patches-out", default=str(ROOT / "analysis" / "insn_patches.json"))
    ap.add_argument("--cpp-out", default=str(ROOT / "src" / "gen" / "mods.cpp"))
    a = ap.parse_args()

    sha1, word = eboot_words(Path(a.elf))
    patches, switches, seen, errors = [], [], {}, []
    mod_dirs = sorted(p for p in Path(a.mods).glob("*/mod.json")) if Path(a.mods).is_dir() else []
    for mj in mod_dirs:
        try:
            m = json.loads(mj.read_text(encoding="utf-8"))
        except ValueError as e:
            errors.append(f"{mj}: not valid JSON ({e})")
            continue
        name = str(m.get("name") or mj.parent.name)
        if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", name):
            errors.append(f"{mj}: name {name!r} must be lowercase letters, digits and dashes")
            continue
        mine = (m.get("patches") or {}).get(sha1)
        if not mine:
            print(f"[mods] {name}: no patches for this EBOOT ({sha1[:12]}...), skipped")
            continue
        ident = name.replace("-", "_")
        guard = f"g_mod_{ident}"
        ok = True
        for p in mine:
            try:
                addr, orig, new = (int(str(p[k]), 0) for k in ("addr", "orig", "new"))
            except (KeyError, ValueError):
                errors.append(f"{name}: each patch needs addr, orig and new as numbers")
                ok = False
                break
            have = word(addr)
            if have is None:
                errors.append(f"{name}: 0x{addr:08X} is not in the EBOOT")
                ok = False
            elif have != orig:
                errors.append(f"{name}: 0x{addr:08X} holds 0x{have:08X}, the mod expects 0x{orig:08X}")
                ok = False
            elif addr in seen:
                errors.append(f"{name}: 0x{addr:08X} is also patched by {seen[addr]}")
                ok = False
            else:
                seen[addr] = name
                patches.append({"addr": f"0x{addr:08X}", "orig": f"0x{orig:08X}", "new": f"0x{new:08X}",
                                "guard": guard, "note": f"{name}: {p.get('note', '')}".strip()})
        if ok:
            switches.append((name, guard, f"GT5_MOD_{ident.upper()}", bool(m.get("default", False)),
                             str(m.get("description", ""))))
            print(f"[mods] {name}: {len(mine)} patch(es), switch GT5_MOD_{ident.upper()}"
                  f" (default {'on' if m.get('default') else 'off'})")
    if errors:
        for e in errors:
            print("[mods] ERROR:", e)
        sys.exit(1)

    pout, cout = Path(a.patches_out), Path(a.cpp_out)
    if not switches:
        for f in (pout, cout):
            if f.exists():
                f.unlink()
        print("[mods] no mods for this EBOOT")
        return
    pout.parent.mkdir(parents=True, exist_ok=True)
    cout.parent.mkdir(parents=True, exist_ok=True)
    pout.write_text(json.dumps({"patches": patches}, indent=2) + "\n")
    lines = ["// Generated by scripts/mods.py from GT5/mods: one start-up switch per mod.",
             "#include <stdio.h>", "#include <stdlib.h>", "",
             "static int gt5_mod_switch(const char* env, int on_by_default, const char* name)", "{",
             "    const char* e = getenv(env);",
             "    const int on = e ? atoi(e) != 0 : on_by_default;",
             "    fprintf(stderr, \"[mods] %s %s (%s)\\n\", name, on ? \"on\" : \"off\", env);",
             "    return on;", "}", ""]
    for name, guard, env, default, desc in switches:
        if desc:
            lines.append("// " + desc.replace("\n", " "))
        lines.append(f'extern "C" {{ int {guard} = gt5_mod_switch("{env}", {int(default)}, "{name}"); }}')
    cout.write_text("\n".join(lines) + "\n")
    print(f"[mods] {len(patches)} patch(es) from {len(switches)} mod(s) -> {pout.name}, {cout.name}")


if __name__ == "__main__":
    main()
