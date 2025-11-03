# convert_png_to_rgb565.py
from PIL import Image
import os

fn = 'custom_components/tile_dashboard/img/switch_off.png'
img = Image.open(fn).convert('RGB')
w, h = img.size

# RGB → RGB565
data = []
for r, g, b in img.getdata():
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    data.append(rgb565)

hdr = os.path.join('custom_components/tile_dashboard/include', 'switch_off_raw.h')
os.makedirs(os.path.dirname(hdr), exist_ok=True)
with open(hdr, 'w') as f:
    f.write(f'// automatisch generiert aus {fn}\n')
    f.write(f'constexpr int SWITCH_ON_W = {w};\n')
    f.write(f'constexpr int SWITCH_ON_H = {h};\n')
    f.write('static const uint16_t switch_on_raw[] = {\n')
    # pro Zeile z.B. 16 Werte
    for i in range(0, len(data), 16):
        line = ', '.join(f'0x{x:04X}' for x in data[i:i+16])
        f.write('  ' + line + ',\n')
    f.write('};\n')
