"""Contact sheet of frame dumps: sheet.py <dir> <out.png> [--every N] [--from K] [--max M] [--cols C]"""
import glob, os, sys
from PIL import Image, ImageDraw
def arg(n, d): return int(sys.argv[sys.argv.index(n) + 1]) if n in sys.argv else d
fs = sorted(glob.glob(os.path.join(sys.argv[1], "frame_*.png")))
fs = fs[arg("--from", 0)::arg("--every", 1)][:arg("--max", 40)]
cols = arg("--cols", 6)
W, H = 320, 180
rows = (len(fs) + cols - 1) // cols
sheet = Image.new("RGB", (cols * W, rows * (H + 14)), "black")
d = ImageDraw.Draw(sheet)
for i, f in enumerate(fs):
    im = Image.open(f).convert("RGB").crop((0, 0, 1280, 720)).resize((W, H))
    x, y = (i % cols) * W, (i // cols) * (H + 14)
    sheet.paste(im, (x, y + 14))
    d.text((x + 2, y + 1), os.path.basename(f)[6:-4], fill="yellow")
sheet.save(sys.argv[2])
print(len(fs), "frames")
