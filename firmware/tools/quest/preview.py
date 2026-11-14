# Composites the Quest layers the way the firmware will, to check the art on the desktop.
import sys, numpy as np
from PIL import Image, ImageFilter
from pal import P, IDX
import world, bg, sprites

def lut(light=(1, 1, 1), night=0.0):
    out = np.zeros((256, 3), np.float32)
    for i, (n, rgb, kind) in enumerate(P):
        r, g, b = rgb
        if kind == 2: out[i] = rgb
        elif kind == 1:
            lit = np.array(rgb) * light
            out[i] = lit * (1 - night) + np.array((255, 196, 104)) * night
        else:
            out[i] = np.array(rgb) * light
    return np.clip(out, 0, 255)

SKY = {'day': ((70, 140, 236), (176, 222, 255)), 'dusk': ((52, 50, 120), (250, 150, 110)), 'night': ((8, 12, 40), (40, 52, 110))}
LIGHT = {'day': ((1, 1, 1), 0), 'dusk': ((0.92, 0.72, 0.72), 0.5), 'night': ((0.34, 0.40, 0.66), 1.0)}

def compose(cam, tod='day', show_hero=True):
    L = lut(*LIGHT[tod])
    top, hor = SKY[tod]
    img = np.zeros((140, 120, 3), np.float32)
    for y in range(140):
        t = min(1, y / 96)
        img[y, :] = np.array(top) * (1 - t) + np.array(hor) * t
    # far: blurred at 2x
    far = bg.far_layer()
    farimg = np.zeros((bg.FAR_Y1 - bg.FAR_Y0, 120, 3), np.float32)
    for y in range(farimg.shape[0]):
        for x in range(120):
            idx = far.a[y, int(cam * 0.25 + x) % bg.TW]
            farimg[y, x] = L[idx] if idx else img[y + bg.FAR_Y0, x]
    big = Image.fromarray(farimg.astype(np.uint8)).resize((240, farimg.shape[0] * 2), Image.NEAREST).filter(ImageFilter.BoxBlur(2))
    img2 = np.array(Image.fromarray(img.astype(np.uint8)).resize((240, 280), Image.NEAREST)).astype(np.float32)
    img2[bg.FAR_Y0 * 2:bg.FAR_Y1 * 2] = np.array(big)
    hill = bg.hill_layer()
    for y in range(hill.a.shape[0]):
        for x in range(240):
            idx = hill.a[y, int(cam * 0.5 + x / 2) % bg.TW]
            if idx: img2[(y + bg.HILL_Y0) * 2:(y + bg.HILL_Y0) * 2 + 2, x] = L[idx]
    W, lamps = world.build_world()
    for y in range(W.a.shape[0]):
        for x in range(240):
            idx = W.a[y, int(cam + x / 2) % world.WW]
            if idx: img2[(y + world.WY0) * 2:(y + world.WY0) * 2 + 2, x] = L[idx]
    if show_hero:
        fr = sprites.HERO_FRAMES['idle0']
        hx, hy = 30, world.FEET - 25
        for j, row in enumerate(fr):
            for i, ch in enumerate(row):
                if ch in sprites.HERO_MAP:
                    img2[(hy + j) * 2:(hy + j) * 2 + 2, (hx + i) * 2:(hx + i) * 2 + 2] = L[IDX[sprites.HERO_MAP[ch]]]
    return Image.fromarray(np.clip(img2, 0, 255).astype(np.uint8))

if __name__ == '__main__':
    tod = sys.argv[1] if len(sys.argv) > 1 else 'day'
    locs = [int(a) for a in sys.argv[2].split(',')] if len(sys.argv) > 2 else list(range(11))
    ims = [compose(i * world.SPACING, tod) for i in locs]
    sheet = Image.new('RGB', (len(ims) * 250, 290), (30, 30, 36))
    for k, im in enumerate(ims): sheet.paste(im, (k * 250 + 5, 5))
    sheet.save('prev_%s.png' % tod)
