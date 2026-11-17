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

A list under "*" is for every release. Its patches name the place by the code around it instead of
an address: {"find": "8089001C 88A90019 7C8407B4", "new": "0x38800002"} replaces the first word of that
run of instruction words, which must turn up exactly once in the EBOOT (otherwise the mod is skipped
for this copy). "at" picks another word of the run (0 = the first). A list for this copy's own SHA-1
wins over "*".

Two mods may replace the same instruction (unlock-fps and half-rate both set the frame pacer's wait):
the lifter builds every version in, and when both are switched on the one whose name sorts first wins.

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
    def find(words):
        """Addresses where this run of 32-bit words stands, on a 4-byte boundary."""
        pat = b"".join(struct.pack(">I", w) for w in words)
        out = []
        for va, off, size in segs:
            i = d.find(pat, off, off + size)
            while i >= 0:
                if (i - off) % 4 == 0:
                    out.append(va + i - off)
                i = d.find(pat, i + 1, off + size)
        return out
    return hashlib.sha1(d).hexdigest().upper(), word, find


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", default=str(ROOT / "input" / "EBOOT.elf"))
    ap.add_argument("--mods", default=str(ROOT / "mods"))
    ap.add_argument("--patches-out", default=str(ROOT / "analysis" / "insn_patches.json"))
    ap.add_argument("--cpp-out", default=str(ROOT / "src" / "gen" / "mods.cpp"))
    a = ap.parse_args()

    sha1, word, find = eboot_words(Path(a.elf))
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
        mine = (m.get("patches") or {}).get(sha1) or (m.get("patches") or {}).get("*")
        if not mine:
            print(f"[mods] {name}: no patches for this EBOOT ({sha1[:12]}...), skipped")
            continue
        # "find" patches: the place by its code. Not found exactly once = this mod has no place here.
        resolved, lost = [], None
        for p in mine:
            if "find" not in p:
                resolved.append(p)
                continue
            try:
                words = [int(w, 16) for w in str(p["find"]).split()]
                at = int(p.get("at", 0))
                if not words or not 0 <= at < len(words):
                    raise ValueError
            except ValueError:
                errors.append(f"{name}: \"find\" is a run of hex instruction words, \"at\" an index into it")
                lost = "bad"
                break
            hits = find(words)
            if len(hits) != 1:
                lost = f"its code turns up {len(hits)} times in this EBOOT, not once"
                break
            q = dict(p)
            q["addr"] = f"0x{hits[0] + 4 * at:08X}"
            q["orig"] = f"0x{words[at]:08X}"
            resolved.append(q)
        if lost:
            if lost != "bad":
                print(f"[mods] {name}: {lost}, skipped")
            continue
        mine = resolved
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
            elif addr in seen and seen[addr][1] != orig:
                errors.append(f"{name}: 0x{addr:08X} is also patched by {seen[addr][0]}, from another original word")
                ok = False
            else:
                seen.setdefault(addr, (name, orig))
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
