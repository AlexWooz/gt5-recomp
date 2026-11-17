#!/usr/bin/env python3
"""Switch hand-written native functions in for lifted SPU functions, at build time (no re-lift).

    spu_native_patch.py <lifted spu_recomp.c> <patched copy> <image> <natives.txt>

GT5/src/native/natives.txt lists the replacements, one a line:

    <image> <address> <native symbol or -> <source file> [pure] [hooked]

For each line of this image the lifted definition `SPU_SV void gt5_<image>_spu_func_<ADDR>_sv(...)`
is renamed to `gt5_<image>_lifted_<ADDR>_sv`, and a function with the old name is added that picks
one at run time (GT5_SPU_NATIVE: 1 = native, the default; 0 = lifted; 2 = run both and compare,
only for lines marked `pure`: functions that touch nothing but their context and local store).
Every caller, the thunk and the function table keep using the old name, so nothing else changes.
A line marked `hooked` only renames the lifted definition to `..._orig_sv`: its source file defines
the old name itself (built with NATIVE_HOOKED) and has its own switch.
A line whose function is not in this lift is an error: a native function must never be linked in
for code it was not written against.
"""
import re
import sys


def main():
    src, dst, image, listing = sys.argv[1:5]
    text = open(src, encoding="latin-1", newline="").read()
    nl = "\r\n" if "\r\n" in text else "\n"
    tail = []
    for line in open(listing, encoding="utf-8"):
        f = line.split("#", 1)[0].split()
        if len(f) < 4 or f[0] != image:
            continue
        addr, native = f[1].upper().rjust(8, "0"), f[2]
        pure = "pure" in f[4:]
        hooked = "hooked" in f[4:]
        if not hooked and not re.fullmatch(r"[A-Za-z_]\w*", native):
            sys.exit(f"[spu-native] {listing}: {native!r} is not a C identifier")
        old = f"gt5_{image}_spu_func_{addr}_sv"
        new = f"gt5_{image}_lifted_{addr}_sv"
        head = f"SPU_SV void {old}(spu_context* ctx) {{"
        if text.count(head) != 1:
            sys.exit(f"[spu-native] {image}: `{head}` stands {text.count(head)} times in {src}, not once")
        if hooked:
            # The native source defines the old name itself and calls ..._orig_sv for what it leaves.
            text = text.replace(head, f"SPU_SV void gt5_{image}_spu_func_{addr}_orig_sv(spu_context* ctx) {{")
            n_hooked = locals().get("n_hooked", 0) + 1
            continue
        text = text.replace(head, f"SPU_SV void {new}(spu_context* ctx) {{")
        tail += [f"/* native replacement of {image} 0x{addr} (GT5/src/native) */",
                 f"extern void {native}(spu_context* ctx);",
                 f"SPU_SV void {old}(spu_context* ctx) {{",
                 "    const int m = g_gt5_spu_native;",
                 f"    if (m == 1) {{ {native}(ctx); return; }}",
                 (f"    if (m == 2) {{ gt5_spu_native_compare(ctx, {native}, {new}, \"{image}:{addr}\"); return; }}"
                  if pure else "    /* not pure: compare mode runs the lifted function */"),
                 f"    {new}(ctx);",
                 "}"]
    if not tail and not locals().get("n_hooked"):
        sys.exit(f"[spu-native] {listing} has no line for {image}")
    if tail:
        text += nl + '#include "spu_native.h"' + nl + nl.join(tail) + nl
    open(dst, "w", encoding="latin-1", newline="").write(text)
    print(f"[spu-native] {image}: {len(tail) // 8 + locals().get('n_hooked', 0)} function(s) switched")


if __name__ == "__main__":
    main()
