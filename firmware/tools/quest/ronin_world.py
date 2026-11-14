# The Ronin look's world: a stone stairway up a mountain village at war. One landmark per landing, ledges alternate sides.
import numpy as np
from draw import Canvas, shade_disc, bricks
from rpal import IDX

W = 120
S = 100                 # rise between landings
N = 11
H = 1130
def GY(i): return H - 30 - i * S
SX0, SX1 = 52, 68       # the stairs
LOCS = ['Bell Tower', 'Tea House', 'War Drums', 'Fox Shrine', 'Farmhouse', 'Omikuji Stall',
        'Notice Board', "Painter's Hut", 'Noh Stage', 'Dojo', 'Watchtower']
DYN = {'fires': [], 'lanterns': [], 'foxeyes': [], 'banners': []}

def C(): return Canvas(W, H, idx=IDX)

def rock(cv, rng):
    """The spine of the mountain the stairs are cut into."""
    top = GY(N - 1) - 2
    for y in range(top, H):
        wob = int(4 * np.sin(y * 0.05) + 3 * np.sin(y * 0.13 + 1))
        x0, x1 = 40 + wob, 80 - wob // 2 + int(2 * np.sin(y * 0.09))
        for x in range(x0, x1):
            n = (x * 13 + y * 7 + (x * y) % 11) % 17
            c = 's1' if n > 4 else 's0'
            if x - x0 < 3 or x1 - x < 2: c = 's0'
            if n == 0: c = 's2'
            cv.put(x, y, c)
        cv.put(x0 - 1, y, 'ink'); cv.put(x1, y, 'ink')
        if (y * 7) % 23 < 3: cv.put(x0, y, 'g2'); cv.put(x0 + 1, y, 'g1')
        if (y * 11) % 29 < 2: cv.put(x1 - 1, y, 'g2')
    # little pines growing out of the cliff
    for k in range(14):
        y = int(rng.integers(top + 20, H - 10)); left = k % 2 == 0
        x = 40 if left else 80
        d = -1 if left else 1
        for j in range(6): cv.put(x + d * j, y - j // 2, 'k1')
        tip = (x + d * 6, y - 3)
        for (dx, dy, r) in ((0, 0, 4), (d * 3, -2, 3), (-d * 2, -3, 3)):
            shade_disc(cv, tip[0] + dx, tip[1] + dy, r, ('L0', 'L1', 'L2', 'L3'), ry=max(1, r - 2))

def stairs(cv):
    for i in range(N - 1):
        y0, y1 = GY(i + 1), GY(i)
        for y in range(y0, y1):
            k = (y - y0) % 4
            c = 'w3' if k == 0 else 'w2' if k < 3 else 'w0'
            for x in range(SX0, SX1):
                cc = c
                if k == 3 and (x * 5 + y) % 13 == 0: cc = 'ink2'
                if k in (1, 2) and (x * 7 + y * 3) % 19 == 0: cc = 'w1'
                cv.put(x, y, cc)
            cv.put(SX0, y, 'w1'); cv.put(SX1 - 1, y, 'w1'); cv.put(SX1 - 2, y, 'w2' if k else 'w3')
            cv.put(SX0 - 1, y, 'ink'); cv.put(SX1, y, 'ink'); cv.put(SX0 - 2, y, 'w1'); cv.put(SX1 + 1, y, 'w0')

def landing(cv, i):
    y = GY(i)
    cv.rect(SX0 - 8, y, SX1 - SX0 + 16, 3, 'w2'); cv.hline(SX0 - 8, y, SX1 - SX0 + 16, 'w3'); cv.hline(SX0 - 8, y + 3, SX1 - SX0 + 16, 'ink')
    # torii over the landing
    tx0, tx1 = SX0 - 3, SX1 + 2
    for x in (tx0, tx1):
        cv.rect(x - 1, y - 30, 3, 30, 'R'); cv.vline(x + 1, y - 30, 30, 'Rd'); cv.rect(x - 2, y - 3, 5, 3, 'K')
    cv.rect(tx0 - 6, y - 34, tx1 - tx0 + 13, 3, 'K')
    for k in range(4): cv.put(tx0 - 6 - k // 2, y - 35 - k // 2, 'K'); cv.put(tx1 + 6 + k // 2, y - 35 - k // 2, 'K')
    cv.rect(tx0 - 4, y - 31, tx1 - tx0 + 9, 2, 'R'); cv.hline(tx0 - 4, y - 31, tx1 - tx0 + 9, 'r3')
    cv.rect(tx0 - 2, y - 25, tx1 - tx0 + 5, 2, 'R'); cv.hline(tx0 - 2, y - 24, tx1 - tx0 + 5, 'Rd')
    cv.rect((tx0 + tx1) // 2 - 3, y - 30, 7, 5, 'K'); cv.rect((tx0 + tx1) // 2 - 2, y - 29, 5, 3, 'y1')
    # stone lanterns either side of the stairs, halfway up
    if i < N - 1:
        ly = y - S // 2
        for x in (SX0 - 9, SX1 + 4):
            stone_lantern(cv, x, ly)
            DYN['lanterns'].append((x + 3, ly - 9))

def stone_lantern(cv, x, base):
    cv.stamp(x, base - 14, [
        '..www..',
        '.wwwww.',
        'WWWWWWW',
        '.wLlLw.',
        '.wlglw.',
        '.wLlLw.',
        'WWWWWWW',
        '..www..',
        '..www..',
        '..wWw..',
        '.wwwww.',
        '.WWWWW.',
        'WWWWWWW',
        'kkkkkkk'], {'w': 'w2', 'W': 'w1', 'L': 'lp', 'l': 'win2', 'g': 'f3', 'k': 'ink'})

def ledge(cv, i, rng):
    y = GY(i); left = i % 2 == 0
    x0, x1 = (0, SX0 - 4) if left else (SX1 + 4, W)
    # floor and a stone (ishigaki) wall below it
    cv.rect(x0, y, x1 - x0, 2, 'g2'); cv.hline(x0, y, x1 - x0, 'g3')
    bricks(cv, x0, y + 2, x1 - x0, 10, cols=('w0', 'w1', 'w2', 'w3'), bw=5, bh=3)
    cv.hline(x0, y + 12, x1 - x0, 'ink')
    # rock hanging under the ledge, tapering back into the mountain
    for j in range(13, 40):
        t = (j - 13) / 27
        if left: xa, xb = int(x0 + (x1 - x0) * t * 0.9), x1
        else: xa, xb = x0, int(x1 - (x1 - x0) * t * 0.9)
        for x in range(xa, xb):
            n = (x * 73 + (y + j) * 151 + (x * (y + j)) % 13) % 23
            cv.put(x, y + j, 's1' if n < 5 else 's2' if n == 7 else 's0')
        cv.put(xa, y + j, 'ink'); cv.put(xb - 1, y + j, 'ink')
    # arrows stuck in the wall (it is war)
    for k in range(2):
        ax = int(rng.integers(x0 + 4, x1 - 6)); ay = y + 4 + int(rng.integers(0, 6))
        for d in range(5): cv.put(ax + (d if left else -d), ay - d // 2, 't3')
        cv.put(ax + (5 if left else -5), ay - 3, 'W'); cv.put(ax + (5 if left else -5), ay - 2, 'W')
    return x0, x1

# ───── Japanese building parts ─────
def jroof(cv, cx, yb, half, h, cols=('b0', 'b1', 'b2', 'b3'), lift=4):
    for i in range(-half, half + 1):
        a = abs(i)
        bot = yb - int(max(0, a - (half - 7)) ** 1.6 * lift / 7 ** 1.6)
        top = yb - h + int(a * h / (half + 2) * 0.75)
        if a > half - 3: top = min(top, bot - 2)
        for y in range(top, bot + 1):
            if y == top or y == bot: c = 'ink'
            elif y == bot - 1: c = cols[3] if i < 0 else cols[2]
            elif (i + 100) % 3 == 0: c = cols[0]
            else: c = cols[2] if i < 0 else cols[1]
            cv.put(cx + i, y, c)
    # ridge with upturned ends
    cv.hline(cx - half // 3, yb - h, 2 * (half // 3) + 1, 'ink')
    cv.rect(cx - half // 3 - 1, yb - h - 2, 3, 3, 'ink'); cv.rect(cx + half // 3 - 1, yb - h - 2, 3, 3, 'ink')

def jwall(cv, x, y, w, h, posts=12):
    for j in range(y, y + h):
        for i in range(x, x + w):
            c = 'c1'
            if j > y + h * 0.6: c = 't1' if (i - x) % 4 else 't0'
            elif (i * 5 + j * 3) % 31 == 0: c = 'c2'
            cv.put(i, j, c)
    for p in range(x, x + w, posts): cv.rect(p, y, 2, h, 't0'); cv.vline(p + 1, y, h, 't1')
    cv.rect(x, y, w, 2, 't0'); cv.rect(x + w - 2, y, 2, h, 't0')

def shoji(cv, x, y, w, h, lit=True):
    cv.rect(x - 1, y - 1, w + 2, h + 2, 't0')
    for j in range(h):
        for i in range(w):
            c = ('win2' if (i + j) % 7 else 'win') if lit else 'wd'
            if i % 3 == 2 or j % 4 == 3: c = 't1'
            cv.put(x + i, y + j, c)

def chochin(cv, x, y):
    cv.stamp(x, y, ['.k.', 'kLk', 'LlL', 'RlR', 'LlL', 'kLk', '.k.'], {'k': 'ink', 'L': 'lp', 'l': 'win2', 'R': 'R'})
    DYN['lanterns'].append((x + 1, y + 3))

def nobori(cv, x, base, h=34, col='R'):
    cv.vline(x, base - h, h, 't1')
    cv.rect(x + 1, base - h + 1, 6, h - 10, col); cv.vline(x + 6, base - h + 1, h - 10, 'Rd' if col == 'R' else 'ink2')
    cv.hline(x, base - h + 1, 7, 't0')
    cv.disc(x + 4, base - h + 7, 2, 'fw'); cv.put(x + 4, base - h + 7, col)
    for k in range(3): cv.put(x + 2, base - h + 13 + k * 4, 'fw'); cv.put(x + 3, base - h + 13 + k * 4, 'fw')

# ───── landmarks (x0..x1 is the ledge) ─────
def bell_tower(cv, x0, x1, y):
    cx = (x0 + x1) // 2
    cv.rect(cx - 16, y - 4, 33, 4, 'w2'); cv.hline(cx - 16, y - 4, 33, 'w3')
    for px in (cx - 13, cx + 12): cv.rect(px, y - 34, 3, 30, 't1'); cv.vline(px + 2, y - 34, 30, 't0')
    cv.rect(cx - 14, y - 34, 30, 2, 't2')
    jroof(cv, cx, y - 34, 22, 16, cols=('b0', 'b1', 'b2', 'b3'))
    # the bell
    cv.vline(cx, y - 32, 3, 'm0')
    for j in range(16):
        hw = 4 + j // 3
        for i in range(-hw, hw + 1): cv.put(cx + i, y - 29 + j, 'bronze2' if i < -1 else 'bronze' if i < 3 else 'k1')
        cv.put(cx - hw - 1, y - 29 + j, 'ink'); cv.put(cx + hw + 1, y - 29 + j, 'ink')
    cv.hline(cx - 9, y - 13, 19, 'ink'); cv.hline(cx - 8, y - 20, 17, 'k1')
    for k in range(3): cv.put(cx - 3 + k * 3, y - 25, 'y1')
    DYN['bell'] = (cx, y - 21)
    cv.rect(cx + 6, y - 23, 12, 2, 't3'); cv.vline(cx + 17, y - 30, 7, 'm0')

def tea_house(cv, x0, x1, y):
    cx = (x0 + x1) // 2; w = x1 - x0 - 4
    jwall(cv, x0 + 2, y - 26, w, 26)
    shoji(cv, x0 + 6, y - 22, 9, 8); shoji(cv, x1 - 15, y - 22, 9, 8)
    cv.rect(cx - 5, y - 13, 10, 13, 'ink2'); cv.rect(cx - 5, y - 16, 10, 6, 'Bd' if 'Bd' in IDX else 'b2')
    cv.vline(cx, y - 16, 6, 'ink'); cv.put(cx - 3, y - 13, 'fw'); cv.put(cx + 3, y - 13, 'fw')
    jroof(cv, cx, y - 26, w // 2 + 5, 9)
    jwall(cv, x0 + 8, y - 46, w - 12, 12, posts=8)
    shoji(cv, cx - 6, y - 43, 12, 6)
    jroof(cv, cx, y - 46, w // 2 - 1, 12)
    chochin(cv, x0 + 3, y - 24); chochin(cv, x1 - 6, y - 24)
    DYN['fires'] += [(cx - 8, y - 52), (cx + 6, y - 49), (x1 - 8, y - 30)]

def war_drums(cv, x0, x1, y):
    cx = (x0 + x1) // 2
    cv.rect(cx - 16, y - 3, 33, 3, 't1'); cv.hline(cx - 16, y - 3, 33, 't2')
    for px in (cx - 12, cx + 11): cv.rect(px, y - 14, 2, 11, 't0')
    # big drum on its side
    for j in range(-11, 12):
        hw = int(12 * (1 - (j / 13) ** 2)) + 2
        for i in range(-hw, hw + 1):
            c = 't2' if i < 2 else 't1'
            if abs(i) >= hw - 1: c = 'ink'
            cv.put(cx + i, y - 26 + j, c)
    cv.disc(cx, y - 26, 9, 'W', ry=10); cv.disc(cx, y - 26, 8, 'fw', ry=9)
    for a in range(3):                    # three comma swirl (tomoe)
        ang = a * 2.094
        px, py = int(cx + np.cos(ang) * 4), int(y - 26 + np.sin(ang) * 4)
        cv.disc(px, py, 2, 'R'); cv.put(int(cx + np.cos(ang + 0.9) * 6), int(y - 26 + np.sin(ang + 0.9) * 6), 'R')
    for k in range(-10, 11, 4): cv.put(cx + k, y - 37, 'y2'); cv.put(cx + k, y - 15, 'y2')
    DYN['drum'] = (cx, y - 26)
    nobori(cv, x0 + 1, y, 40, 'R'); nobori(cv, x1 - 8, y, 36, 'K')
    DYN['banners'] += [(x0 + 1, y - 40), (x1 - 8, y - 36)]

def fox_shrine(cv, x0, x1, y):
    cx = (x0 + x1) // 2
    cv.rect(cx - 7, y - 18, 14, 14, 't2'); cv.rect(cx - 5, y - 15, 10, 11, 'ink2'); cv.rect(cx - 2, y - 13, 4, 6, 'y1')
    cv.rect(cx - 9, y - 4, 18, 4, 'w2'); cv.hline(cx - 9, y - 4, 18, 'w3')
    jroof(cv, cx, y - 18, 12, 8, cols=('r0', 'r1', 'r2', 'r3'), lift=3)
    cv.rect(cx - 2, y - 27, 4, 3, 'y1')
    for fx, flip in ((cx - 16, False), (cx + 11, True)):           # stone foxes
        rows = ['.w...', 'ww...', 'www..', '.wRR.', '.wwww', 'wwwww', 'wwwww', 'kkkkk']
        cv.stamp(fx, y - 12, rows if not flip else [r[::-1] for r in rows], {'w': 'Wd', 'R': 'R', 'k': 'w0'})
        cv.rect(fx - 1, y - 4, 7, 4, 'w1')
        DYN['foxeyes'].append((fx + (1 if not flip else 3), y - 10))
    # a small torii
    for x in (cx - 13, cx + 12): cv.vline(x, y - 26, 26, 'R')
    cv.rect(cx - 16, y - 28, 32, 2, 'K'); cv.hline(cx - 14, y - 23, 28, 'R')

def farmhouse(cv, x0, x1, y):
    cx = (x0 + x1) // 2; w = x1 - x0 - 6
    jwall(cv, x0 + 3, y - 18, w, 18, posts=8)
    cv.rect(cx - 4, y - 13, 8, 13, 'ink2'); shoji(cv, x0 + 6, y - 14, 8, 6, lit=False)
    # thatched roof
    for j in range(26):
        hw = int((w // 2 + 6) * (1 - j / 30))
        for i in range(-hw, hw + 1):
            c = 'y0' if (i * 3 + j * 5) % 7 else 't2'
            if j < 3: c = 't1'
            if abs(i) >= hw: c = 'ink'
            cv.put(cx + i, y - 18 - j, c)
    cv.rect(cx - 4, y - 46, 9, 3, 'ink')
    DYN['fires'] += [(cx - 6, y - 36), (cx + 5, y - 30), (cx, y - 42)]
    DYN['pet'] = (x1 - 9 if x0 < 50 else x0 + 9, y)

def omikuji(cv, x0, x1, y):
    cx = (x0 + x1) // 2
    for px in (cx - 14, cx + 13): cv.rect(px, y - 22, 2, 22, 't1')
    jroof(cv, cx, y - 22, 19, 8, cols=('r0', 'r1', 'r2', 'r3'), lift=3)
    for rope in (y - 16, y - 9):
        cv.hline(cx - 13, rope, 26, 't3')
        for k in range(cx - 12, cx + 13, 2):
            if (k + rope) % 3: cv.rect(k, rope + 1, 1, 3, 'fw'); cv.put(k, rope, 'W')
    cv.rect(cx - 4, y - 6, 8, 6, 't2'); cv.hline(cx - 4, y - 6, 8, 't3'); cv.vline(cx + 1, y - 9, 3, 't3')
    chochin(cv, cx - 1, y - 30)

def notice_board(cv, x0, x1, y):
    cx = (x0 + x1) // 2
    for px in (cx - 15, cx + 14): cv.rect(px, y - 30, 2, 30, 't0')
    cv.rect(cx - 15, y - 28, 31, 18, 't1'); cv.rect(cx - 14, y - 27, 29, 16, 't2')
    for k, (sx, sw) in enumerate(((cx - 12, 7), (cx - 3, 7), (cx + 6, 7))):
        cv.rect(sx, y - 26, sw, 14, 'fw'); cv.hline(sx, y - 26, sw, 'Wd')
        for r in range(2, 13, 2): cv.hline(sx + 2, y - 26 + r, 3, 'ink2' if (r + k) % 3 else 'R')
    jroof(cv, cx, y - 30, 20, 7, lift=3)

def painter(cv, x0, x1, y):
    cx = (x0 + x1) // 2
    jwall(cv, x0 + 4, y - 22, x1 - x0 - 8, 22, posts=10)
    jroof(cv, cx, y - 22, (x1 - x0) // 2 + 2, 10)
    # hanging scroll: a red sun over mountains
    cv.rect(cx - 6, y - 19, 12, 17, 't0'); cv.rect(cx - 5, y - 18, 10, 15, 'fw')
    cv.disc(cx, y - 14, 2, 'R')
    for k in range(-4, 5): cv.vline(cx + k, y - 9 - (4 - abs(k)) // 2, 4 + (4 - abs(k)) // 2, 'ink2')
    cv.hline(cx - 6, y - 20, 12, 't3'); cv.hline(cx - 6, y - 2, 12, 't3')
    shoji(cv, x0 + 6, y - 18, 6, 8)

def noh_stage(cv, x0, x1, y):
    cx = (x0 + x1) // 2
    cv.rect(x0 + 2, y - 6, x1 - x0 - 4, 6, 't2'); cv.hline(x0 + 2, y - 6, x1 - x0 - 4, 't3')
    for k in range(x0 + 4, x1 - 4, 6): cv.rect(k, y - 4, 2, 4, 't0')
    cv.rect(x0 + 6, y - 32, x1 - x0 - 12, 26, 't3')
    # the painted pine on the back wall
    shade_disc(cv, cx, y - 22, 8, ('L1', 'L2', 'L3', 'q3'), ry=4); shade_disc(cv, cx - 6, y - 26, 5, ('L1', 'L2', 'L3', 'q3'), ry=3)
    cv.vline(cx, y - 18, 12, 'k1')
    for px in (x0 + 4, x1 - 6): cv.rect(px, y - 34, 2, 28, 't0')
    jroof(cv, cx, y - 34, (x1 - x0) // 2 + 2, 12)
    chochin(cv, x0 + 6, y - 30); chochin(cv, x1 - 9, y - 30)

def dojo(cv, x0, x1, y):
    cx = (x0 + x1) // 2
    jwall(cv, x0 + 2, y - 26, x1 - x0 - 4, 26, posts=10)
    cv.rect(cx - 5, y - 18, 10, 18, 't0'); cv.rect(cx - 4, y - 17, 8, 17, 'ink2')
    jroof(cv, cx, y - 26, (x1 - x0) // 2 + 4, 14)
    cv.rect(cx - 9, y - 34, 18, 6, 't2'); cv.outline_rect(cx - 9, y - 34, 18, 6, 'ink')
    for k in range(3): cv.vline(cx - 5 + k * 5, y - 33, 4, 'ink')         # brush strokes on the sign
    cv.hline(cx - 6, y - 31, 12, 'ink')
    # a rack of spears
    rx = x0 + 6 if x0 < 50 else x1 - 10
    for k in range(3):
        cv.vline(rx + k * 2, y - 24, 22, 't3'); cv.put(rx + k * 2, y - 25, 'm2'); cv.put(rx + k * 2, y - 26, 'm2')
    cv.hline(rx - 1, y - 10, 7, 't0')

def watchtower(cv, x0, x1, y):
    cx = (x0 + x1) // 2
    for px in (cx - 12, cx + 11):
        for j in range(70):
            cv.put(px + (j // 12 if px < cx else -j // 12), y - j, 't1'); cv.put(px + 1 + (j // 12 if px < cx else -j // 12), y - j, 't0')
    for j in range(8, 66, 10):
        sh = j // 12
        cv.hline(cx - 11 + sh, y - j, 22 - 2 * sh, 't2')
        for k in range(22 - 2 * sh): cv.put(cx - 11 + sh + k, y - j + (k * 10) // max(1, 22 - 2 * sh) - 5, 't0') if j < 60 else None
    cv.rect(cx - 10, y - 72, 21, 4, 't2'); cv.hline(cx - 10, y - 72, 21, 't3'); cv.hline(cx - 10, y - 68, 21, 'ink')
    cv.rect(cx - 6, y - 78, 13, 6, 'm0'); cv.hline(cx - 6, y - 78, 13, 'm1')     # the beacon basket
    DYN['beacon'] = (cx, y - 80)
    nobori(cv, cx + 9, y - 72, 22, 'R')

BUILD = [bell_tower, tea_house, war_drums, fox_shrine, farmhouse, omikuji, notice_board, painter, noh_stage, dojo, watchtower]

def build_world():
    rng = np.random.default_rng(5)
    cv = C()
    rock(cv, rng)
    stairs(cv)
    for i in range(N):
        x0, x1 = ledge(cv, i, rng)
        BUILD[i](cv, x0, x1, GY(i))
        landing(cv, i)
    # banners along the stairs
    for i in range(N - 1):
        y = GY(i) - S // 2 + 30
        x = SX0 - 12 if i % 2 else SX1 + 6
        nobori(cv, x, y, 30, 'R' if i % 3 else 'K')
    # the ground at the bottom
    for y in range(GY(0) + 3, H):
        for x in range(W):
            if not cv.get(x, y): cv.put(x, y, 's0' if (x + y) % 5 else 's1')
    return cv

# ───── backgrounds ─────
FAR_H = 300
def far_layer():
    """Distant ridges and a burning valley. Row 0 is high in the sky, the bottom is the valley floor."""
    cv = Canvas(W, FAR_H, idx=IDX, wrap=True)
    def ridge(base, terms, col, rim, fires=0, seed=0):
        rng = np.random.default_rng(seed)
        tops = []
        for x in range(W):
            h = base + sum(a * np.sin(2 * np.pi * k * x / W + p) for a, k, p in terms)
            t = int(h); tops.append(t)
            for y in range(t, FAR_H): cv.put(x, y, col)
            cv.put(x, t, rim)
        for _ in range(fires):
            x = int(rng.integers(0, W)); y = tops[x] + int(rng.integers(3, 20))
            cv.put(x, y, 'f2' if rng.random() < 0.5 else 'f1')
    ridge(120, [(10, 1, 0.3), (6, 3, 1.2), (3, 7, 2.1)], 'fm1', 'fm3', 0, 1)
    ridge(170, [(8, 2, 1.1), (5, 5, 0.2), (2, 11, 1.3)], 'fm2', 'fm3', 8, 2)
    # a castle burning on the far ridge
    cx = 30; base = 150
    for (tx, tw, th) in ((-6, 13, 10), (-4, 9, 18), (-2, 5, 26)):
        cv.rect(cx + tx, base - th, tw, th, 'fm0'); cv.hline(cx + tx - 2, base - th, tw + 4, 'fm0')
    cv.put(cx - 1, base - 20, 'f2'); cv.put(cx + 1, base - 12, 'f1'); cv.put(cx - 3, base - 6, 'f2')
    DYN['castle'] = (cx, base - 22)
    ridge(230, [(6, 2, 2.0), (4, 4, 0.7), (2, 9, 0.4)], 'fm0', 'fm2', 14, 3)
    # the burning village down in the valley
    rng = np.random.default_rng(8)
    for k in range(16):
        x = int(rng.integers(0, W - 8)); y = int(rng.integers(256, 290))
        cv.rect(x, y, 7, 4, 'h1'); cv.rect(x - 1, y - 2, 9, 2, 'h0')
        if k % 3 == 0: cv.put(x + 3, y + 1, 'f2'); cv.put(x + 2, y - 3, 'f1')
        else: cv.put(x + 2, y + 1, 'win')
    return cv

MID_H = 200
def mid_layer():
    """Dark pines on the slopes, repeating as you climb."""
    cv = Canvas(W, MID_H, idx=IDX, wrap=True, wrapy=True)
    rng = np.random.default_rng(12)
    for k in range(18):
        x = int(rng.integers(0, W)); y = int(rng.integers(0, MID_H))
        side = x < 30 or x > 90
        if not side and rng.random() < 0.7: x = int(rng.integers(0, 28)) if rng.random() < 0.5 else int(rng.integers(92, W))
        h = int(rng.integers(18, 34))
        for j in range(h):
            hw = int((h - j) * 0.35) + 1
            for i in range(-hw, hw + 1):
                cv.put(x + i, y - j, 'h1' if i < 0 else 'h0')
        cv.vline(x, y, 4, 'h0')
    return cv

FG_H = 160
def fg_layer():
    cv = Canvas(W, FG_H, idx=IDX, wrap=True, wrapy=True)
    rng = np.random.default_rng(31)
    for x in (2, 8, 113, 117):                          # bamboo at the edges
        for y in range(FG_H):
            c = 'q1' if (y % 22) else 'q0'
            cv.put(x, y, c); cv.put(x + 1, y, 'q2' if y % 22 else 'q0'); cv.put(x + 2, y, 'q1')
        for k in range(5):
            y = int(rng.integers(0, FG_H)); d = 1 if x < 60 else -1
            for j in range(9): cv.put(x + 1 + d * j, y - j // 3, 'q2'); cv.put(x + 1 + d * j, y - j // 3 + 1, 'q1')
    return cv
