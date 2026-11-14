# Far mountains and rolling hills, both tile every 240 logical px.
import numpy as np
from draw import Canvas, shade_disc

TW = 240
FAR_Y0, FAR_Y1 = 30, 100       # far layer rows (sky shows through index 0)
HILL_Y0, HILL_Y1 = 58, 114     # hill layer rows

def periodic(x, terms):
    return sum(a * np.sin(2 * np.pi * (k * x / TW) + p) for a, k, p in terms)

def far_layer():
    cv = Canvas(TW, FAR_Y1 - FAR_Y0, y0=FAR_Y0, wrap=True)
    # back range: tall, pale, snow capped
    for x in range(TW):
        h = 52 + periodic(x, [(9, 2, 0.3), (6, 5, 1.1), (3, 11, 2.0)])
        # sharp peaks
        for px, ph, pw in ((30, 18, 22), (104, 24, 26), (176, 16, 20), (222, 12, 16)):
            d = min(abs(x - px), TW - abs(x - px))
            if d < pw: h = min(h, 52 - ph * (1 - d / pw) + 2)
        top = int(h)
        for y in range(top, FAR_Y1):
            depth = y - top
            slope = periodic(x + 1, [(9, 2, 0.3), (6, 5, 1.1), (3, 11, 2.0)]) - periodic(x - 1, [(9, 2, 0.3), (6, 5, 1.1), (3, 11, 2.0)])
            lit = ((x * 7 + y * 3) % 5) < 3 if depth < 30 else False
            c = 'fm2' if (x % 24) < 11 else 'fm1'
            if depth < 6 and top < 40: c = 'sn1' if (x % 24) < 11 else 'sn0'
            elif depth < 9 and top < 40 and (x + y) % 3 == 0: c = 'sn0'
            cv.put(x, y, c)
    # front range: lower, darker
    for x in range(TW):
        h = 74 + periodic(x, [(6, 3, 2.1), (4, 7, 0.4), (2, 13, 1.7)])
        top = int(h)
        for y in range(top, FAR_Y1):
            c = 'fm1' if ((x + 6) % 30) < 13 else 'fm0'
            cv.put(x, y, c)
    return cv

def hill_layer():
    cv = Canvas(TW, HILL_Y1 - HILL_Y0, y0=HILL_Y0, wrap=True)
    def hill(base, terms, cols):
        for x in range(TW):
            top = int(base + periodic(x, terms))
            for y in range(top, HILL_Y1):
                d = y - top
                c = cols[3] if d == 0 else cols[2] if d < 3 else cols[1] if d < 14 else cols[0]
                if d >= 3 and d < 14 and (x * 5 + y * 3) % 11 == 0: c = cols[2]
                cv.put(x, y, c)
    hill(84, [(7, 2, 0.8), (4, 3, 2.2), (2, 7, 0.1)], ('h0', 'h1', 'h2', 'h3'))
    # distant castle on the back hill
    cx = 150
    base = int(84 + periodic(cx, [(7, 2, 0.8), (4, 3, 2.2), (2, 7, 0.1)]))
    for (tx, tw, th) in ((-10, 5, 16), (-4, 9, 11), (6, 5, 14), (12, 4, 9)):
        cv.rect(cx + tx, base - th, tw, th + 2, 'hc1'); cv.vline(cx + tx + tw - 1, base - th, th + 2, 'hc0')
        for i in range(cx + tx, cx + tx + tw, 2): cv.put(i, base - th - 1, 'hc1')
    for (tx, th) in ((-8, 16), (8, 14)):
        for j in range(5):
            for i in range(-(4 - j) // 1, (4 - j) + 1): cv.put(cx + tx + i, base - th - 2 - j, 'hc0' if i > 0 else 'hc2')
    cv.put(cx - 8, base - 24, 'R'); cv.put(cx - 7, base - 24, 'R'); cv.put(cx - 8, base - 23, 'hc0')
    # windmill on the left hill
    wx = 52
    wb = int(84 + periodic(wx, [(7, 2, 0.8), (4, 3, 2.2), (2, 7, 0.1)]))
    for j in range(14):
        hw = 3 + j // 5
        for i in range(-hw, hw + 1): cv.put(wx + i, wb - 14 + j, 'hc2' if i < 0 else 'hc1')
    for j in range(4):
        for i in range(-(4 - j), 5 - j): cv.put(wx + i, wb - 15 - j, 'hc0')
    for k in range(-9, 10):
        cv.put(wx + k, wb - 17 + k, 'hc0'); cv.put(wx + k, wb - 17 - k, 'hc0')
    # front hill with tree clumps
    hill(98, [(5, 3, 1.4), (3, 5, 0.2), (2, 9, 2.6)], ('h0', 'h1', 'h2', 'h3'))
    rng = np.random.default_rng(4)
    for i in range(26):
        x = int(rng.integers(0, TW))
        top = int(98 + periodic(x, [(5, 3, 1.4), (3, 5, 0.2), (2, 9, 2.6)]))
        r = int(rng.integers(3, 6))
        shade_disc(cv, x, top - r + 2, r, ('ht0', 'ht0', 'ht1', 'h2'), ry=r)
        cv.vline(x, top - 1, 3, 'ht0')
    for i in range(10):
        x = int(rng.integers(0, TW))
        top = int(84 + periodic(x, [(7, 2, 0.8), (4, 3, 2.2), (2, 7, 0.1)]))
        if abs(x - cx) < 20 or abs(x - wx) < 12: continue
        shade_disc(cv, x, top - 2, 3, ('ht0', 'ht1', 'h2', 'h3'), ry=3)
    return cv

FG_W, FG_Y0, FG_Y1 = 200, 118, 140
def fg_layer():
    """Out-of-focus foreground: tall grass, flowers and a fence post, blurred at bake time."""
    cv = Canvas(FG_W, FG_Y1 - FG_Y0, y0=FG_Y0, wrap=True)
    rng = np.random.default_rng(21)
    def blade(x, h, c, lean):
        for k in range(h):
            cv.put(x + int(lean * k / h), FG_Y1 - 1 - k, c)
            cv.put(x + 1 + int(lean * k / h), FG_Y1 - 1 - k, c)
    for clump in (8, 46, 77, 120, 160, 186):
        for k in range(9):
            blade(clump + int(rng.integers(-6, 7)), int(rng.integers(8, 20)), ['L0', 'L1', 'g0'][int(rng.integers(0, 3))], int(rng.integers(-4, 5)))
    for x, c in ((30, 'pk'), (96, 'fy'), (140, 'fw'), (172, 'pk')):
        blade(x, 14, 'L1', 1)
        cv.disc(x + 1, FG_Y1 - 16, 2, c); cv.put(x + 1, FG_Y1 - 16, 'fy' if c != 'fy' else 'y1')
    # a fence post and rail
    for x in range(56, 70):
        cv.put(x, FG_Y1 - 12, 't1'); cv.put(x, FG_Y1 - 11, 't0')
    cv.rect(60, FG_Y1 - 17, 4, 17, 't1'); cv.vline(60, FG_Y1 - 17, 17, 't2')
    return cv
