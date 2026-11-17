#!/usr/bin/env python3
"""Make blk_7DC8.inc: the body of the lifted 0x7DC8, verbatim, with one line added.

The native 0x7DC8 is the lifted function itself (every statement, every interrupt check, the same
exits) plus a fast-forward at the head of its inner loop (label 0x7F10): NX7_FAST_FORWARD, a macro of
native_w01606C00.c, runs all but the last pass of the row natively and leaves what those passes
leave -- the local-store lines stored and the loop-carried registers r13, r15, r16 -- after which
the lifted statements do the last pass, which assigns every other register of the pass.

This script copies the function's body out of the job's spu_recomp.c and inserts the macro after the
interrupt check of loc_00007F10, followed by the twins of the three registers it changed (the lift
keeps a twin _uN beside some registers). It checks the shape the macro relies on: the statements of
the pass that carry state from one pass to the next, and that nothing else does.

EXPECT_SHA1 pins the lift this was tested against: after a re-lift, run the harness again and
then update it.

Usage: gen_7DC8.py <spu_recomp.c> <out_dir>
"""
import hashlib
import re
import sys

EXPECT_SHA1 = "7c707417c7e29d223a60ba863d49d508e2f6867d"
FUNC = "SPU_SV void gt5_w01606C00_spu_func_00007DC8_sv(spu_context* ctx) {"
LABELS = ["loc_00007E58", "loc_00007F10", "loc_00007FC4"]
INTR = re.compile(r"^\s*if \(ctx->int_enable && \(ctx->event_status & ctx->event_mask\)\) \{.*return; \}\s*$")

# The pass 0x7F10 .. 0x7FC4, statement by statement, without the twin bookkeeping (" _uN = ...;")
# and the register reloads: what the fast-forward was written against.
PASS = """r52 = spu_ai(r15, 1);
r6 = spu_ls_lq(ctx, _ls, _rd, spu_w0(r13) + 0x0);
r51 = spu_cgt(r15, r17);
r7 = spu_ls_lq(ctx, _ls, _rd, spu_w0(r13) + 0x10);
r50 = spu_cgt(r52, r17);
r10 = spu_ls_lq_fwd(ctx, _ls, _rd, spu_w0(r20) + 0x0, _fa, _fv);
r47 = spu_ah(r18, r16);
r48 = spu_gb(r51);
r45 = spu_ahi(r16, 2);
r46 = spu_gb(r50);
r9 = spu_xshw(r47);
r8 = spu_fm_lz(r6, r22, r6, r22, &_u8);
r5 = spu_fm_lz(r7, r22, _u7, r22, &_u5);
r44 = spu_rotqby(r14, r48);
r4 = spu_xshw(r45);
r43 = spu_rotqby(r14, r46);
r66 = spu_shli(r9, 3);
r65 = spu_cgt(r21, r4);
r42 = spu_ceqbi(r44, 0);
r41 = spu_ceqbi(r43, 0);
r40 = spu_xsbh(r42);
r39 = spu_xsbh(r41);
r38 = spu_ceqhi(r40, 0);
r37 = spu_ceqhi(r39, 0);
r13 = spu_ai(r13, 32);
r3 = spu_fsm(r38);
r15 = spu_ai(r15, 2);
r2 = spu_fsm(r37);
r16 = spu_ai(r16, 2);
r74 = spu_selb(r6, r8, r3);
r72 = spu_selb(r7, r5, r2);
r78 = spu_cflts(r74, 149);
r79 = spu_cflts(r74, 161);
r76 = spu_cflts(r72, 149);
r77 = spu_cflts(r72, 161);
r75 = spu_fa_lz(r10, r74, r10, _u74, &_u75);
r73 = spu_shufb(r78, r79, r19);
r71 = spu_shufb(r76, r77, r19);
r70 = spu_fa_lz(r75, r72, _u75, _u72, &_ud);
r68 = spu_shufb(r73, r74, r24);
r69 = spu_shufb(r71, r72, r23);
spu_ls_sq(ctx, _ls, _wd, spu_w0(r20) + 0x0, r70); _fa = (spu_w0(r20) + 0x0) & (SPU_LS_MASK & ~0xFu); _fv = r70;
r67 = spu_ah(r68, r69);
spu_ls_sq(ctx, _ls, _wd, spu_w0(r66) + spu_w0(r25), r67); _fa = (spu_w0(r66) + spu_w0(r25)) & (SPU_LS_MASK & ~0xFu); _fv = r67;
if (spu_w0(r65) != 0) { goto loc_00007F10; }
"""


def strip(s):
    """A statement line without twin assignments, register reloads and what the branch writes back."""
    s = s.strip()
    s = re.sub(r"r\d+ = ctx->gpr\[\d+\]; ", "", s)
    s = re.sub(r"ctx->gpr\[\d+\] = r\d+; ", "", s)
    s = re.sub(r" _u\d+ = [^;]*;", "", s)
    s = re.sub(r"^_u\d+ = [^;]*; ", "", s)
    return s


def main():
    src, outdir = sys.argv[1], sys.argv[2]
    raw = open(src, "rb").read()
    h = hashlib.sha1(raw).hexdigest()
    if h != EXPECT_SHA1:
        sys.exit("spu_recomp.c changed (sha1 %s): rebuild and re-run the harness against it, then update "
                 "EXPECT_SHA1 in gen_7DC8.py" % h)
    lines = raw.decode("latin-1").replace("\r\n", "\n").split("\n")
    start = [i for i, s in enumerate(lines) if s == FUNC]
    assert len(start) == 1, "0x7DC8 not found exactly once"
    start = start[0]
    end = next(i for i in range(start + 1, len(lines)) if lines[i] == "}")
    body = lines[start + 1:end]
    labs = [s[:-1] for s in body if re.match(r"^loc_[0-9A-F]{8}:$", s)]
    assert labs == LABELS, "labels of 0x7DC8 changed: %s" % labs
    i10 = body.index("loc_00007F10:")
    ic4 = body.index("loc_00007FC4:")
    assert INTR.match(body[i10 + 1]), body[i10 + 1]
    got = [strip(s) for s in body[i10 + 2:ic4] if strip(s) and not strip(s).startswith("/*")]
    want = [s for s in PASS.split("\n") if s]
    assert got == want, "the pass at 0x7F10 changed:\n" + "\n".join("%s\n   != %s" % (a, b) for a, b in zip(got, want) if a != b)
    decl = " ".join(s for s in body if s.strip().startswith("u128 "))
    twins = [n for n in (13, 15, 16) if re.search(r"\b_u%d\b" % n, decl)]
    out = body[:i10 + 2]
    out.append("        NX7_FAST_FORWARD;" + "".join(" _u%d = r%d;" % (n, n) for n in twins))
    out += body[i10 + 2:]
    with open(outdir + "/blk_7DC8.inc", "w", newline="\n") as f:
        f.write("/* Generated by gen_7DC8.py: the body of the lifted 0x7DC8 with NX7_FAST_FORWARD added -- do not edit. */\n")
        f.write("\n".join(out) + "\n")
    print("blk_7DC8.inc: %d lines, twins set after the fast-forward: %s" % (len(out), twins))
    return 0


if __name__ == "__main__":
    sys.exit(main())
