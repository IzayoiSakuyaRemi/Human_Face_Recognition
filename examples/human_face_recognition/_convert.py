import os, sys
from PIL import Image
import pillow_avif  # register AVIF handler with PIL

W, H = 1024, 600
d = sys.argv[1] if len(sys.argv) > 1 else 'D:/123/参考'
out_dir = sys.argv[2] if len(sys.argv) > 2 else 'D:/works/esp-who-master/examples/human_face_recognition/wallpapers'

for f in sorted(os.listdir(d)):
    ext = f.lower()
    if not (ext.endswith('.jpg') or ext.endswith('.jpeg') or ext.endswith('.png')
            or ext.endswith('.bmp') or ext.endswith('.avif') or ext.endswith('.webp')):
        continue
    inp = os.path.join(d, f)
    out = os.path.join(out_dir, f'{os.path.splitext(f)[0]}.bin')
    print(f'Processing: {f}')
    try:
        img = Image.open(inp).convert('RGB')
        img = img.resize((W, H), Image.LANCZOS)
        px = img.load()
        buf = bytearray(W * H * 2)
        idx = 0
        for y in range(H):
            for x in range(W):
                r, g, b = px[x, y]
                c16 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                buf[idx] = c16 & 0xFF
                buf[idx + 1] = (c16 >> 8) & 0xFF
                idx += 2
        os.makedirs(out_dir, exist_ok=True)
        with open(out, 'wb') as fh:
            fh.write(buf)
        print(f'  -> {out} ({len(buf)} bytes)')
    except Exception as e:
        print(f'  ERROR: {e}')
        import traceback
        traceback.print_exc()
print('Done!')
