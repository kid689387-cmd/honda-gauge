#!/bin/sh
# Build the desktop simulator and render every screen to tools/sim/out/sheet.png
set -e
cd "$(dirname "$0")/../.."
mkdir -p tools/sim/out
g++ -std=c++17 -O1 -Itools/sim -Isrc tools/sim/sim_main.cpp src/gfx.cpp src/screens.cpp src/thai_shape.cpp -o tools/sim/out/sim
OUT=tools/sim/out tools/sim/out/sim
python3 - <<'PY'
from PIL import Image; import glob
fs = sorted(glob.glob('tools/sim/out/*.ppm'))
cols = 3; rows = (len(fs) + cols - 1) // cols
sheet = Image.new('RGB', (cols * 330, rows * 250), (60, 60, 60))
for i, f in enumerate(fs):
    sheet.paste(Image.open(f), ((i % cols) * 330 + 5, (i // cols) * 250 + 5))
sheet.save('tools/sim/out/sheet.png'); print(len(fs), 'screens')
PY
