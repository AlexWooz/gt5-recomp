#!/usr/bin/env python3
"""Turn RPCS3 game patches (patch.yml) into the port's game-code mods (GT5/mods), where they fit.

    python scripts/rpcs3_mods.py <patch.yml>                        list the patches for this copy
    python scripts/rpcs3_mods.py <patch.yml> --add "Disable Dithering" [--add "..."]

RPCS3 patches are listed per game executable under "PPU-<hash>"; this works out that hash for the
EBOOT the port was lifted from (GT5/input/EBOOT.elf, the same one relift.ps1 and mods.py read), so
only the patches written for exactly this release and update are offered.

A patch fits when every write in it swaps one instruction in the game's code (be32 into an
executable segment) for one that doesn't branch: the same shape a mod has (scripts/mods.py). RPCS3
writes its patches into the game's code while it runs; here the code was turned into PC code ahead
of time, so a patch that adds new code of its own (most long ones), changes data, takes a
configurable value or branches somewhere new can't be brought over this way, and is left out whole.

--add writes GT5/mods/rpcs3-<name>/mod.json, on by default (GT5_MOD_RPCS3_<NAME>=0 turns it off),
with each instruction's original read from your EBOOT. Then run scripts\\relift.ps1 and
scripts\\build.cmd gt5 (as for any mod, see MODDING.md). The patches stay RPCS3's authors' work:
the mod names them, and you get them from your own patch.yml; don't put the generated mods in a
repository of your own without their permission.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

OP_RE = re.compile(r"^\s*-\s*\[\s*([A-Za-z0-9_]+)\s*(?:,\s*([^,\]]+?)\s*)?(?:,\s*(.+?)\s*)?\]\s*(?:#.*)?$")
PATCH_NAME_RE = re.compile(r'^  "(.+)":\s*(?:#.*)?$')
FIELD_RE = re.compile(r'^    (Author|Notes):\s*(.*?)\s*$')


def read_elf(path):
    """The ELF's file SHA-1 (mods.py's key), RPCS3's PPU hash, and a reader for code words."""
    d = path.read_bytes()
    phoff = struct.unpack(">Q", d[0x20:0x28])[0]
    es, n = struct.unpack(">HH", d[0x36:0x3A])
    h = hashlib.sha1()
    code = []
    for i in range(n):
        o = phoff + i * es
        p_type, p_flags, p_offset, p_vaddr, _, p_filesz, p_memsz = struct.unpack(">IIQQQQQ", d[o:o + 48])
        # RPCS3's PPU hash (ppu_load_exec): type and flags of every program header, then the
        # address, size and file bytes of each loadable one.
        h.update(struct.pack(">II", p_type, p_flags))
        if p_type == 1 and p_memsz:
            h.update(struct.pack(">QQ", p_vaddr, p_memsz))
            h.update(d[p_offset:p_offset + p_filesz])
            if p_flags & 1 and p_filesz:          # executable: the code the lifter turned into C++
                code.append((p_vaddr, p_offset, p_filesz))

    def code_word(addr):
        for va, off, size in code:
            if va <= addr <= va + size - 4:
                return struct.unpack(">I", d[off + addr - va:off + addr - va + 4])[0]
        return None
    return hashlib.sha1(d).hexdigest().upper(), "PPU-" + h.hexdigest(), code_word


def parse_anchors(lines):
    """{anchor: {"ops": [...], "text": "..."}}: the shared write lists and notes patches refer to
    with `- [ load, *name ]` and `Notes: *name`."""
    anchors, cur, indent = {}, None, 0
    for line in lines:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        ind = len(line) - len(line.lstrip(" "))
        m = re.match(r"^(\s*)[^\s:]+:\s*&(\S+)\s*(?:#.*)?$", line)
        if m:
            cur, indent = anchors.setdefault(m.group(2), {"ops": [], "text": ""}), len(m.group(1))
            continue
        if cur is None or ind <= indent:
            cur = None
            continue
        m = OP_RE.match(line)
        if m:
            cur["ops"].append((m.group(1).lower(), m.group(2) or "", m.group(3) or ""))
        elif line.strip().startswith('"') and not cur["text"]:
            cur["text"] = line.strip()[1:-1].replace('\\"', '"') if line.strip().endswith('"') else ""
    return anchors


def parse_patches(path, ppu):
    """{patch name: {"author", "notes", "ops": [(op, addr_text, value_text)]}} for one PPU hash,
    with `load` writes replaced by the anchor's own (moved by the load's offset, if any)."""
    lines = [raw.rstrip() for raw in path.read_text(encoding="utf-8", errors="replace").splitlines()]
    anchors = parse_anchors(lines)
    out, cur, in_block, in_patch = {}, None, False, False
    for line in lines:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if not line.startswith(" "):                       # a top-level key: PPU-..., Anchors:, ...
            in_block = line.split(":")[0].strip().lower() == ppu.lower()
            cur, in_patch = None, False
            continue
        if not in_block:
            continue
        m = PATCH_NAME_RE.match(line)
        if m:
            cur = out.setdefault(m.group(1), {"author": "", "notes": "", "ops": []})
            in_patch = False
            continue
        if cur is None:
            continue
        m = FIELD_RE.match(line)
        if m:
            v = m.group(2)
            if v.startswith('"') and v.endswith('"') and len(v) >= 2:
                cur[m.group(1).lower()] = v[1:-1].replace('\\"', '"')
            elif v.startswith("*") and v[1:] in anchors:
                cur[m.group(1).lower()] = anchors[v[1:]]["text"]
            in_patch = False
            continue
        if re.match(r"^    Patch:\s*$", line):
            in_patch = True
            continue
        if re.match(r"^    \S", line):                     # another field of this patch
            in_patch = False
            continue
        if in_patch:
            m = OP_RE.match(line)
            if not m:
                cur["ops"].append(("?", line.strip(), ""))
            elif m.group(1).lower() == "load":
                name = (m.group(2) or "").lstrip("*")
                shift = number(m.group(3)) if m.group(3) else 0
                if name not in anchors or shift is None:
                    cur["ops"].append(("?", line.strip(), ""))
                    continue
                for op, a, v in anchors[name]["ops"]:
                    addr = number(a)
                    cur["ops"].append((op, hex(addr + shift) if addr is not None and op != "load" else a, v))
            else:
                cur["ops"].append((m.group(1).lower(), m.group(2) or "", m.group(3) or ""))
    return out


def number(text):
    try:
        return int(text.strip().strip('"'), 0)
    except ValueError:
        return None


def convert(patch, code_word):
    """(list of (addr, orig, new), None) when the patch fits; (None, reason) when it doesn't."""
    ops = patch["ops"]
    if not ops:
        return None, "has no writes"
    words = {}                      # instruction address -> its new word, writes applied in order
    for op, a, v in ops:
        size = {"be32": 4, "be16": 2, "byte": 1}.get(op)
        if size is None:
            return None, f"has a '{op}' write (only changes to instructions fit)"
        addr, val = number(a), number(v)
        if addr is None or val is None:
            return None, "takes a configurable value"
        if addr % size:
            return None, f"writes {size} bytes at 0x{addr:08X}, not aligned"
        base = addr & ~3
        orig = code_word(base)
        if orig is None:
            return None, f"writes outside the game's code (0x{addr:08X}: data, or new code of its own)"
        shift = (4 - size - (addr & 3)) * 8              # big-endian: the bytes' place in the word
        mask = ((1 << (size * 8)) - 1) << shift
        cur = words.get(base, orig)
        words[base] = (cur & ~mask & 0xFFFFFFFF) | ((val << shift) & mask)
    swaps = []
    for base, new in sorted(words.items()):
        orig = code_word(base)
        if new == orig:
            continue
        # A branch can't be the replacement (the lifter has no labels for new targets), nor the
        # original when it never falls through (b, blr, bctr): the code after it may not be lifted
        # as reachable from there. A conditional branch (bc) may become a plain instruction.
        if (new >> 26) in (16, 17, 18, 19) or (orig >> 26) in (17, 18, 19):
            return None, f"changes a branch at 0x{base:08X} (new code paths can't be added after lifting)"
        swaps.append((base, orig, new))
    if not swaps:
        return None, "changes nothing in this EBOOT"
    return swaps, None


def taken(mods_dir, sha1, skip):
    """{instruction address: mod name} already patched by the mods in mods_dir for this EBOOT."""
    out = {}
    for mj in sorted(Path(mods_dir).glob("*/mod.json")) if Path(mods_dir).is_dir() else []:
        if mj.parent.name == skip:
            continue
        try:
            m = json.loads(mj.read_text(encoding="utf-8"))
            for p in (m.get("patches") or {}).get(sha1, []):
                out[int(str(p["addr"]), 0)] = str(m.get("name") or mj.parent.name)
        except (ValueError, KeyError, TypeError):
            continue
    return out


def slug(name):
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return ("rpcs3-" + s)[:48].rstrip("-")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("patch_yml", help="RPCS3's patch.yml (or imported_patch.yml)")
    ap.add_argument("--add", action="append", default=[], metavar="NAME", help="patch name to turn into a mod")
    ap.add_argument("--elf", default=str(ROOT / "input" / "EBOOT.elf"))
    ap.add_argument("--mods", default=str(ROOT / "mods"))
    a = ap.parse_args()

    sha1, ppu, code_word = read_elf(Path(a.elf))
    patches = parse_patches(Path(a.patch_yml), ppu)
    print(f"This copy's EBOOT: {ppu} (file SHA-1 {sha1[:12]}...)")
    if not patches:
        print("The patch file has no patches for it.")
        return 1 if a.add else 0
    results = {name: convert(p, code_word) for name, p in patches.items()}
    if not a.add:
        for name, (swaps, why) in results.items():
            by = f" ({patches[name]['author']})" if patches[name]["author"] else ""
            if swaps:
                print(f"  fits:    {name}{by}: {len(swaps)} instruction(s)")
            else:
                print(f"  doesn't: {name}{by}: {why}")
        print('Add one with --add "<name>", then run scripts\\relift.ps1 and scripts\\build.cmd gt5.')
        return 0

    status = 0
    for want in a.add:
        name = next((n for n in patches if n.lower() == want.lower()), None)
        if name is None:
            print(f"'{want}': no patch by that name for this EBOOT")
            status = 1
            continue
        swaps, why = results[name]
        if not swaps:
            print(f"'{name}' doesn't fit: {why}")
            status = 1
            continue
        p = patches[name]
        mod = slug(name)
        clash = taken(a.mods, sha1, mod)
        both = sorted({clash[ad] for ad, _, _ in swaps if ad in clash})
        if both:
            print(f"'{name}' changes an instruction that {', '.join(both)} already changes: use that mod instead")
            status = 1
            continue
        desc = f"{name}: RPCS3 patch" + (f" by {p['author']}" if p["author"] else "") + \
               (f". {p['notes']}" if p["notes"] else "")
        doc = {"name": mod, "description": desc, "default": True,
               "patches": {sha1: [{"addr": f"0x{ad:08X}", "orig": f"0x{o:08X}", "new": f"0x{n:08X}",
                                   "note": f"from RPCS3's patch.yml ({ppu})"} for ad, o, n in swaps]}}
        out = Path(a.mods) / mod / "mod.json"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        env = "GT5_MOD_" + mod.replace("-", "_").upper()
        print(f"'{name}': {len(swaps)} instruction(s) -> {out} (on; {env}=0 turns it off)")
    if status == 0:
        print("Now run scripts\\relift.ps1, then scripts\\build.cmd gt5.")
    return status


if __name__ == "__main__":
    sys.exit(main())
