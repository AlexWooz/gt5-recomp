#!/usr/bin/env python3
"""Switch hand-written native functions in for lifted SPU functions, and take a lifted job image's
branches to its own entries directly, at build time (no re-lift).

    spu_native_patch.py <lifted spu_recomp.c> <patched copy> <image> [<natives list>...] [--direct-bi <spu_jobs.c>]

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
Further lists (the build passes GT5/src/native/natives_prof.txt only when its option is on) are read
the same way, after natives.txt. With no list (--direct-bi alone) the image only gets the direct branches.

--direct-bi <spu_jobs.c> (the build passes it for every job image) rewrites the image's returns to the
dispatcher (`ctx->pc = spu_w0(rN); g_spu_trampoline_fn = spu_indirect_branch; return;` -- a `bi`, `biz`,
`binz`, `bie`/`bid` to a register: loop back edges chosen with selb, interrupt toggles to the next
instruction) and its calls through a register (`ctx->pc = _tg; ctx->host_depth++; spu_indirect_branch(ctx);`
-- a `bisl`: the opcode handlers of a job's program, function pointers) so that, when the register holds
one of the image's entries and the dispatcher would choose that entry's function and nothing else
(gt5_dbi_ready, src/native/gt5_direct_bi.h / .c, with the reasons), the function is called directly; in
every other case the code is the lift's own (the dispatcher). The entries: the image's function table
(`{ 0x<ADDR>u, gt5_<image>_spu_func_<ADDR>, ...}`), less the return points (`spu_link(0x...)`,
`spu_drain_call(ctx, 0x...)`: where a waiting host frame must see the branch), the runtime's service
addresses (LS 0, 0x9C0, 0x9D0, 0xA00, 0xA2C, 0xA70, 0x3FEC0, 0x3FF00), and, for a WWS job's code, anything
outside the span it is registered with. Which images: the WWS jobs' code, registered in spu_jobs.c as
resident regions (spu_overlay_register_verified_region), and the WWS job manager (the image whose
registered entry is 0xA00, the policy module). The other images registered there (SPURS job binaries
entered at LS 0) keep the dispatcher: few register branches, and where their code sits and which frames
wait inside it is not decided by their own code. Other lifts (not in spu_jobs.c) are copied unchanged.
Lift text this depends on (a re-lift that changes it leaves those branches with the dispatcher: the
counts printed show it): the two statements above, the table entry form, `spu_link(0x...)`,
`spu_drain_call(ctx, 0x...)`, `#include "spu_recomp.h"`, and `SPU_SV void gt5_<image>_spu_func_<ADDR>_sv(`
(each table entry's code, called by that name).
"""
import re
import sys

SERVICE = {0x0, 0x9C0, 0x9D0, 0xA00, 0xA2C, 0xA70, 0x3FEC0, 0x3FF00}


def image_info(jobs_c, image):
    """(image id, kind, lsa, size) from src/gen/spu_jobs.c, or None when the image gets no direct branches."""
    t = open(jobs_c, encoding="utf-8", errors="replace").read()
    m = re.search(r"spu_begin_image\((\d+)\);\s*gt5_%s_spu_recomp_register\(\);(.*?)(?=spu_begin_image|\Z)" % image, t, re.S)
    if not m:
        return None
    iid, rest = int(m.group(1)), m.group(2)
    r = re.search(r"spu_overlay_register_verified_region\(0x[0-9A-Fa-f]+u, 0x([0-9A-Fa-f]+)u, 0x([0-9A-Fa-f]+)u, (\d+)", rest)
    if r and int(r.group(3)) == iid:
        return iid, 1, int(r.group(1), 16), int(r.group(2), 16)
    if re.search(r"spu_workload_register_img\([^,]+, gt5_%s_spu_func_00000A00, %d," % (image, iid), rest):
        return iid, 0, 0, 0x40000
    return None


def transform_direct_bi(text, image, info):
    iid, kind, lsa, size = info
    tab = [(int(a, 16), f) for a, f in re.findall(r"\{ 0x([0-9A-F]{8})u, gt5_%s_spu_func_([0-9A-F]{8}), \"" % image, text)]
    # each entry's code by its _sv name: defined here, or (a hooked native) named here and defined by the native
    have = set(re.findall(r"\bgt5_%s_spu_func_([0-9A-F]{8})_sv\b" % image, text))
    rets = set(int(x, 16) for x in re.findall(r"spu_link\(0x([0-9A-Fa-f]+)\)", text))
    rets |= set(int(x, 16) for x in re.findall(r"spu_drain_call\(ctx, 0x([0-9A-Fa-f]+)\)", text))
    ents = sorted(set(a for a, f in tab
                      if int(f, 16) == a and not (a & 3) and a not in rets and a not in SERVICE and lsa <= a < lsa + size
                      and f in have))
    first = text.find('#include "spu_recomp.h"')
    heads = re.search(r"^SPU_SV void gt5_%s_spu_func_" % image, text, re.M)
    if not ents or first < 0 or not heads:
        return text, 0, 0
    d = f"gt5_{image}_dbi"
    disp, dchk = f"gt5_{image}_direct_bi_sv", f"gt5_{image}_direct_bi_check_sv"
    call, cchk = f"gt5_{image}_direct_call_sv", f"gt5_{image}_direct_call_check_sv"
    site_re = re.compile(r"ctx->pc = spu_w0\((r\d+)\); g_spu_trampoline_fn = spu_indirect_branch; return;")
    text, sites = site_re.subn(
        lambda m: (f"ctx->pc = spu_w0({m.group(1)}); {{ const int _db = {d}.mode; "
                   f"if (__builtin_expect(_db == 1, 1)) {{ __attribute__((musttail)) return {disp}(ctx); }} "
                   f"if (_db == 2) {{ __attribute__((musttail)) return {dchk}(ctx); }} }} "
                   "g_spu_trampoline_fn = spu_indirect_branch; return;"), text)
    call_old = "ctx->pc = _tg; ctx->host_depth++; spu_indirect_branch(ctx);"
    calls = text.count(call_old)
    text = text.replace(call_old, f"ctx->pc = _tg; ctx->host_depth++; {{ const int _db = {d}.mode; "
                                  f"if (__builtin_expect(_db == 1, 1)) {call}(ctx); else if (_db == 2) {cchk}(ctx); "
                                  "else spu_indirect_branch(ctx); }")
    decl = ("\n/* spu_native_patch.py --direct-bi: a branch or call to a register holding one of this image's entries goes\n"
            " * there directly when the dispatcher would choose the same (src/native/gt5_direct_bi.h) */\n"
            '#include "gt5_direct_bi.h"\n'
            f"static gt5_dbi_image {d};\n"
            f"SPU_SV void {disp}(spu_context* ctx);\nSPU_SV void {dchk}(spu_context* ctx);\n"
            f"SPU_SV void {call}(spu_context* ctx);\nSPU_SV void {cchk}(spu_context* ctx);\n")
    pos = heads.start()
    text = text[:pos] + decl + text[pos:]
    rows = ",\n".join(f"    {{ 0x{a:X}u, gt5_{image}_spu_func_{a:08X} }}" for a in ents)
    bcases = "\n".join(f"        case 0x{a:X}u: __attribute__((musttail)) return gt5_{image}_spu_func_{a:08X}_sv(ctx);" for a in ents)
    ccases = "\n".join(f"        case 0x{a:X}u: gt5_{image}_spu_func_{a:08X}_sv(ctx); return;" for a in ents)
    text += (f"\n/* spu_native_patch.py --direct-bi: this image's entries (image {iid}, "
             + (f"resident code at 0x{lsa:X}+0x{size:X}" if kind == 1 else "the context's own image") + ") */\n"
             f"static const gt5_dbi_ent {d}_tab[] = {{\n{rows}\n}};\n"
             f"static gt5_dbi_image {d} = {{ 0, \"{image}\", {iid}, {kind}, 0x{lsa:X}u, 0x{size:X}u, 0x{ents[0]:X}u, 0x{ents[-1] + 4:X}u, "
             f"{d}_tab, {len(ents)}u }};\n"
             f"__attribute__((constructor)) static void {d}_init(void) {{ gt5_dbi_register(&{d}); }}\n"
             "/* a branch: ctx->pc holds the target */\n"
             f"SPU_SV void {disp}(spu_context* ctx) {{\n"
             f"    if (__builtin_expect(gt5_dbi_ready(ctx, &{d}), 1)) {{\n"
             "        switch (ctx->pc) {\n" + bcases + "\n        default: break;\n        }\n    }\n"
             "    g_spu_trampoline_fn = spu_indirect_branch;\n}\n"
             f"SPU_SV void {dchk}(spu_context* ctx) {{\n"
             f"    if (gt5_dbi_ready(ctx, &{d}) && !gt5_dbi_check(ctx, &{d})) {{ g_spu_trampoline_fn = spu_indirect_branch; return; }}\n"
             f"    __attribute__((musttail)) return {disp}(ctx);\n}}\n"
             "/* a call (host_depth already counted): the entry's function as the dispatcher's fast path calls it */\n"
             f"SPU_SV void {call}(spu_context* ctx) {{\n"
             f"    if (__builtin_expect(gt5_dbi_ready(ctx, &{d}), 1)) {{\n"
             "        switch (ctx->pc) {\n" + ccases + "\n        default: break;\n        }\n    }\n"
             "    spu_indirect_branch(ctx);\n}\n"
             f"SPU_SV void {cchk}(spu_context* ctx) {{\n"
             f"    if (gt5_dbi_ready(ctx, &{d}) && !gt5_dbi_check(ctx, &{d})) {{ spu_indirect_branch(ctx); return; }}\n"
             f"    __attribute__((musttail)) return {call}(ctx);\n}}\n")
    return text, sites + calls, len(ents)


def main():
    args = sys.argv[1:]
    jobs_c = None
    if "--direct-bi" in args:
        i = args.index("--direct-bi")
        jobs_c = args[i + 1]
        del args[i:i + 2]
    src, dst, image = args[:3]
    listings = args[3:]
    text = open(src, encoding="latin-1", newline="").read()
    nl = "\r\n" if "\r\n" in text else "\n"
    tail = []
    for listing in listings:
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
    if listings and not tail and not locals().get("n_hooked"):
        sys.exit(f"[spu-native] {' '.join(listings)} has no line for {image}")
    done = []
    if jobs_c:
        info = image_info(jobs_c, image)
        if info:
            text, n, ne = transform_direct_bi(text, image, info)
            done.append(f"direct branches: {n} dispatcher branches and calls, {ne} entries")
        else:
            done.append("no direct branches (not a WWS job's code or the job manager)")
    if tail:
        text += nl + '#include "spu_native.h"' + nl + nl.join(tail) + nl
    open(dst, "w", encoding="latin-1", newline="").write(text)
    print(f"[spu-native] {image}: {len(tail) // 8 + locals().get('n_hooked', 0)} function(s) switched"
          + (f", {', '.join(done)}" if done else ""))


if __name__ == "__main__":
    main()
