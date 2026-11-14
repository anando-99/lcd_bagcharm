# Small indexed-pixel drawing kit used to paint the Quest world.
import numpy as np
from pal import IDX

class Canvas:
    def __init__(s, w, h, y0=0, wrap=False, idx=None, wrapy=False):
        s.w, s.h, s.y0, s.wrap, s.wrapy = w, h, y0, wrap, wrapy
        s.idx = idx or IDX
        s.a = np.zeros((h, w), np.uint8)

    def put(s, x, y, c):
        y -= s.y0
        if s.wrap: x %= s.w
        if s.wrapy: y %= s.h
        if 0 <= x < s.w and 0 <= y < s.h and c is not None:
            s.a[y, x] = s.idx[c] if isinstance(c, str) else c

    def get(s, x, y):
        y -= s.y0
        if s.wrap: x %= s.w
        if s.wrapy: y %= s.h
        if 0 <= x < s.w and 0 <= y < s.h: return s.a[y, x]
        return 0

    def rect(s, x, y, w, h, c):
        for j in range(y, y + h):
            for i in range(x, x + w): s.put(i, j, c)

    def hline(s, x, y, w, c): s.rect(x, y, w, 1, c)
    def vline(s, x, y, h, c): s.rect(x, y, 1, h, c)

    def outline_rect(s, x, y, w, h, c):
        s.hline(x, y, w, c); s.hline(x, y + h - 1, w, c); s.vline(x, y, h, c); s.vline(x + w - 1, y, h, c)

    def stamp(s, x, y, rows, cmap, flip=False):
        for j, row in enumerate(rows):
            if flip: row = row[::-1]
            for i, ch in enumerate(row):
                if ch in cmap and cmap[ch] is not None: s.put(x + i, y + j, cmap[ch])

    def disc(s, cx, cy, r, c, ry=None):
        ry = ry or r
        for j in range(-ry, ry + 1):
            for i in range(-r, r + 1):
                if (i / (r + 0.5)) ** 2 + (j / (ry + 0.5)) ** 2 <= 1: s.put(cx + i, cy + j, c)

# ---------- reusable scenery ----------
def shade_disc(cv, cx, cy, r, cols, ry=None, light=(-0.45, -0.6)):
    """A ball lit from the top-left: cols = dark .. light (4 shades)."""
    ry = ry or r
    for j in range(-ry, ry + 1):
        for i in range(-r, r + 1):
            u, v = i / (r + 0.5), j / (ry + 0.5)
            if u * u + v * v > 1: continue
            d = ((u - light[0]) ** 2 + (v - light[1]) ** 2) ** 0.5
            k = 3 if d < 0.55 else 2 if d < 0.95 else 1 if d < 1.3 else 0
            cv.put(cx + i, cy + j, cols[k])

def tree(cv, cx, base, size=1.0, pal=('L0', 'L1', 'L2', 'L3'), trunk=('k0', 'k1'), seed=0):
    rng = np.random.default_rng(seed)
    th = int(10 * size); tw = max(3, int(4 * size))
    cv.rect(cx - tw // 2, base - th, tw, th, trunk[1]); cv.vline(cx + tw // 2 - 1, base - th, th, trunk[0])
    cv.put(cx - tw // 2 - 1, base - 1, trunk[0]); cv.put(cx + tw // 2, base - 1, trunk[0])
    r = int(11 * size); top = base - th - r + 3
    blobs = [(0, 0, r), (-r * 0.6, r * 0.25, r * 0.7), (r * 0.6, r * 0.3, r * 0.7), (0, -r * 0.45, r * 0.75)]
    for bx, by, br in blobs:  # outline pass
        cv.disc(int(cx + bx), int(top + by), int(br) + 1, 'ink' if pal[0] == 'L0' else pal[0])
    for bx, by, br in blobs:
        shade_disc(cv, int(cx + bx), int(top + by), int(br), pal)
    for _ in range(int(14 * size)):  # leafy speckle
        a = rng.uniform(0, 6.28); d = rng.uniform(0, r * 0.9)
        x, y = int(cx + np.cos(a) * d), int(top + np.sin(a) * d * 0.8)
        if cv.get(x, y): cv.put(x, y, pal[min(3, 1 + int(rng.integers(0, 3)))])

def bush(cv, cx, base, w=10, pal=('L0', 'L1', 'L2', 'L3')):
    for k in range(-w // 2, w // 2 + 1, 4):
        cv.disc(cx + k, base - 4, 5, 'ink')
    for k in range(-w // 2, w // 2 + 1, 4):
        shade_disc(cv, cx + k, base - 4, 4, pal)
    cv.rect(cx - w // 2 - 3, base - 1, w + 7, 1, 'g0')

def flowers(cv, x, y, w, seed=1, cols=('pk', 'fy', 'fw', 'fb')):
    rng = np.random.default_rng(seed)
    for i in range(x, x + w, 3):
        if rng.random() < 0.7:
            c = cols[int(rng.integers(0, len(cols)))]
            yy = y + int(rng.integers(0, 2))
            cv.put(i, yy + 1, 'g1'); cv.put(i, yy, c)

def lamppost(cv, x, base):
    cv.rect(x - 1, base - 2, 3, 2, 'm0')
    cv.vline(x, base - 22, 20, 'm0'); cv.put(x - 1, base - 3, 'm0'); cv.put(x + 1, base - 3, 'm0')
    cv.stamp(x - 3, base - 28, [
        '.mmmmm.',
        'mgGGGgm',
        'mGFFFGm',
        'mGFfFGm',
        '.mmmmm.',
        '..mmm..'], {'m': 'm0', 'g': 'glow', 'G': 'f2', 'F': 'f3', 'f': 'f3'})
    cv.put(x, base - 29, 'm0')

def fence(cv, x, base, w):
    for i in range(x, x + w):
        cv.put(i, base - 6, 't2'); cv.put(i, base - 5, 't1'); cv.put(i, base - 3, 't2'); cv.put(i, base - 2, 't1')
    for i in range(x, x + w, 5):
        cv.rect(i, base - 8, 2, 8, 't2'); cv.put(i + 1, base - 8, 't1'); cv.vline(i + 1, base - 7, 7, 't1'); cv.put(i, base - 8, 't3')

def barrel(cv, x, base):
    cv.stamp(x, base - 9, [
        '.kkkkk.',
        'kTtTtTk',
        'kmmmmmk',
        'kTttTtk',
        'kTtTtTk',
        'kTttTtk',
        'kmmmmmk',
        'kTtTtTk',
        '.kkkkk.'], {'k': 'ink', 'T': 't2', 't': 't1', 'm': 'm1'})

def crate(cv, x, base, s=8):
    cv.rect(x, base - s, s, s, 't2'); cv.outline_rect(x, base - s, s, s, 't0')
    for i in range(1, s - 1):
        cv.put(x + i, base - s + i, 't1'); cv.put(x + s - 1 - i, base - s + i, 't1')
    cv.hline(x + 1, base - s + 1, s - 2, 't3')

def window(cv, x, y, w=6, h=8, arch=False, box=False):
    cv.rect(x - 1, y - 1, w + 2, h + 2, 't0')
    cv.rect(x, y, w, h, 'win')
    if arch:
        cv.put(x, y, 't0'); cv.put(x + w - 1, y, 't0')
    cv.vline(x + w // 2, y, h, 't1'); cv.hline(x, y + h // 2, w, 't1')
    cv.put(x + 1, y + 1, 'win2'); cv.put(x + w // 2 + 1, y + 1, 'win2'); cv.put(x + 1, y + 2, 'win2')
    cv.rect(x - 2, y + h + 1, w + 4, 1, 't2'); cv.rect(x - 2, y + h + 2, w + 4, 1, 't0')
    if box:
        cv.rect(x - 1, y + h + 3, w + 2, 2, 't1')
        for i in range(x - 1, x + w + 1, 2): cv.put(i, y + h + 2, 'pk' if (i // 2) % 2 else 'fy'); cv.put(i + 1, y + h + 2, 'L2')

def door(cv, x, base, w=8, h=13, col=('t0', 't1', 't2'), arch=True):
    cv.rect(x - 1, base - h - 1, w + 2, h + 1, 'ink')
    cv.rect(x, base - h, w, h, col[1])
    for i in range(x + 1, x + w, 2): cv.vline(i, base - h + 1, h - 1, col[0])
    cv.vline(x, base - h, h, col[2])
    if arch:
        cv.put(x - 1, base - h - 1, None); cv.put(x, base - h, 'ink'); cv.put(x + w - 1, base - h, 'ink')
    cv.put(x + w - 3, base - h // 2, 'y2')
    cv.rect(x - 1, base, w + 2, 1, 'w1')

def bricks(cv, x, y, w, h, cols=('w0', 'w1', 'w2', 'w3'), bw=6, bh=3):
    cv.rect(x, y, w, h, cols[1])
    for j in range(y, y + h):
        r = (j - y) // bh
        if (j - y) % bh == bh - 1:
            cv.hline(x, j, w, cols[0]); continue
        off = (r % 2) * (bw // 2)
        for i in range(x, x + w):
            k = (i - x + off) % bw
            if k == bw - 1: cv.put(i, j, cols[0])
            elif (j - y) % bh == 0 and k < bw - 2: cv.put(i, j, cols[2])
    for j in range(y, y + h, bh * 2):
        cv.put(x + (j * 7) % max(1, w - 2) + 1, j + 1, cols[3])

def plaster(cv, x, y, w, h):
    cv.rect(x, y, w, h, 'c1')
    for j in range(y, y + h):
        for i in range(x, x + w):
            if (i * 7 + j * 13) % 23 == 0: cv.put(i, j, 'c2')
            elif (i * 11 + j * 5) % 29 == 0: cv.put(i, j, 'c0')

def timber_frame(cv, x, y, w, h, posts):
    cv.rect(x, y, w, 2, 't1'); cv.hline(x, y, w, 't2')
    cv.rect(x, y + h - 2, w, 2, 't1')
    for p in posts:
        cv.rect(x + p, y, 2, h, 't1'); cv.vline(x + p, y, h, 't2')

def gable_roof(cv, cx, y_base, half, h, cols, over=3):
    """Front-facing gable. cols = (dark, base, light, highlight)."""
    for j in range(h + 1):
        yy = y_base - j
        hw = int((half + over) * (1 - j / (h + 1)))
        for i in range(-hw, hw + 1):
            edge = abs(i) >= hw - 1
            row = j % 3
            if edge: c = 'ink'
            elif row == 0: c = cols[0]
            else:
                lit = i < 0
                c = cols[2] if lit else cols[1]
                if row == 2 and ((i + j) // 2) % 2 == 0: c = cols[3] if lit else cols[2]
            cv.put(cx + i, yy, c)
    cv.hline(cx - half - over, y_base + 1, 2 * (half + over) + 1, 'ink')
    cv.hline(cx - half - over + 1, y_base, 2 * (half + over) - 1, cols[0])

def band_roof(cv, x, y, w, h, cols, slope=4):
    """Side-gabled roof seen from the front: a sloped band of tiles."""
    for j in range(h):
        inset = int(slope * (1 - j / max(1, h - 1)))
        for i in range(x + inset, x + w - inset):
            row = j % 3
            if j == 0: c = 'ink'
            elif row == 0: c = cols[0]
            else:
                c = cols[2] if (i - x) < w * 0.45 else cols[1]
                if row == 2 and ((i + j) // 3) % 2 == 0: c = cols[3] if (i - x) < w * 0.45 else cols[2]
            cv.put(i, y + j, c)
        cv.put(x + inset - 1, y + j, 'ink'); cv.put(x + w - inset, y + j, 'ink')
    cv.hline(x - 1, y + h, w + 2, 'ink')

def chimney(cv, x, y, h):
    bricks(cv, x, y, 6, h, bw=3, bh=2)
    cv.rect(x - 1, y - 2, 8, 2, 'w0'); cv.hline(x - 1, y - 2, 8, 'w2')
