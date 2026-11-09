"""Show one frame's draws from a RSX_DRAW_CSV log: drawframe.py <csv> <frame>"""
import csv
import sys

want = sys.argv[2]
for r in csv.DictReader(open(sys.argv[1])):
    if r["frame"] == want:
        print(r["draw"], r["outcome"], "surf", r["surf"], "prim", r["prim"], "v", r["verts"], "/", r["source_verts"],
              "pso", r["pso_key"], "blend", r["blend"], "dt", r["dtest"], "dw", r["dwrite"], "cm", r["cmask"],
              "vp", r["vpx"], r["vpy"], r["vpw"], r["vph"], "sc", r["sclx"], r["scly"], "tr", r["trnx"], r["trny"],
              "attr", r["used_attr_mask"])
        print("    tex:", r["tex"])
