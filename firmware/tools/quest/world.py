# Paints the Quest village: one landmark per feature, laid out along a looping street.
import numpy as np
from draw import *
from pal import IDX

SPACING = 128
NLOC = 11
WW = SPACING * NLOC          # 1408 logical px, loops
WY0 = 4                      # world layer covers logical rows 20..139
GROUND = 113                 # buildings stand on this row; the street runs below
FEET = 119                   # the hero's feet

LOCS = ['Clock Tower', 'Tavern', "Bard's Camp", 'Owl Tree', 'Pet Cottage', 'Fortune Tent',
        'Quest Board', 'Gallery', 'Theatre', 'Guild Hall', 'Lighthouse']
DYN = {}                     # positions the firmware animates (clock hands, fire, owl eyes ...)

def cxof(i): return i * SPACING + 64

def street(cv):
    rng = np.random.default_rng(3)
    for x in range(WW):
        # grass verge behind the street, with tufts poking up
        cv.put(x, GROUND - 1, 'g2'); cv.put(x, GROUND, 'g1'); cv.put(x, GROUND + 1, 'g0')
        if x % 5 == 0 or rng.random() < 0.15: cv.put(x, GROUND - 2, 'g2')
        if x % 11 == 3: cv.put(x, GROUND - 3, 'g3')
        # cobbled street
        for y in range(GROUND + 2, 126):
            cv.put(x, y, 'p1')
        cv.put(x, 125, 'p0')
    # cobbles: staggered rounded stones
    for row, y in enumerate(range(GROUND + 2, 125, 3)):
        off = (row % 2) * 3
        for x in range(-off, WW, 6):
            w = 5
            for i in range(w):
                cv.put(x + i, y, 'p2'); cv.put(x + i, y + 1, 'p2' if i else 'p1')
            cv.put(x, y, 'p3'); cv.put(x + 1, y, 'p3')
            cv.put(x + w - 1, y + 1, 'p1')
            cv.hline(x, y + 2, w, 'p0'); cv.put(x + w, y, 'p0'); cv.put(x + w, y + 1, 'p0')
    for x in range(WW):
        # front lawn
        cv.put(x, 126, 'g3'); cv.put(x, 127, 'g2')
        for y in range(128, 140): cv.put(x, y, 'g1' if y < 133 else 'g0')
        if (x * 37) % 7 == 0: cv.put(x, 129 + (x % 4), 'g2')
        if (x * 13) % 17 == 0: cv.put(x, 134 + (x % 5), 'g1')
        if (x * 29) % 23 == 0: cv.put(x, 130 + (x % 3), 'g3')
    rng2 = np.random.default_rng(11)
    for _ in range(90):
        x, y = int(rng2.integers(0, WW)), int(rng2.integers(129, 138))
        c = ['pk', 'fy', 'fw', 'fb'][int(rng2.integers(0, 4))]
        cv.put(x, y, c); cv.put(x, y + 1, 'g0')

# ---------------------------------------------------------------- landmarks
def clock_tower(cv, cx):
    # annex on the left
    plaster(cv, cx - 38, 86, 22, GROUND - 86)
    timber_frame(cv, cx - 38, 86, 22, GROUND - 86, [0, 10, 20])
    gable_roof(cv, cx - 27, 85, 13, 13, ('r0', 'r1', 'r2', 'r3'))
    window(cv, cx - 33, 92, 5, 7, box=True)
    # tower
    tx, tw = cx - 13, 26
    bricks(cv, tx, 40, tw, GROUND - 40)
    cv.vline(tx, 40, GROUND - 40, 'w3'); cv.vline(tx + tw - 1, 40, GROUND - 40, 'w0')
    for y in (60, 84):
        cv.rect(tx - 2, y, tw + 4, 3, 'w2'); cv.hline(tx - 2, y + 2, tw + 4, 'w0'); cv.hline(tx - 2, y, tw + 4, 'w3')
    # clock face
    cv.disc(cx, 50, 10, 'ink'); cv.disc(cx, 50, 9, 'y1'); cv.disc(cx, 50, 8, 'c2')
    for k in range(12):
        a = k * np.pi / 6
        cv.put(int(round(cx + np.sin(a) * 7)), int(round(50 - np.cos(a) * 7)), 'ink' if k % 3 == 0 else 'c0')
    DYN['clock'] = (cx, 50)
    # spire
    gable_roof(cv, cx, 37, 13, 26, ('b0', 'b1', 'b2', 'b3'), over=3)
    cv.vline(cx, 4, 8, 'y1'); cv.disc(cx, 5, 1, 'y2')
    # belfry window, door
    window(cv, cx - 3, 66, 6, 10, arch=True)
    door(cv, cx - 5, GROUND, 10, 16)
    cv.rect(cx - 7, GROUND - 19, 14, 2, 'w2'); cv.hline(cx - 7, GROUND - 19, 14, 'w3')
    # right side: a well
    wx = cx + 26
    bricks(cv, wx - 7, GROUND - 7, 14, 7, bw=4, bh=2)
    cv.vline(wx - 6, GROUND - 19, 12, 't1'); cv.vline(wx + 5, GROUND - 19, 12, 't1')
    band_roof(cv, wx - 9, GROUND - 23, 18, 5, ('r0', 'r1', 'r2', 'r3'), slope=3)
    cv.vline(wx, GROUND - 18, 6, 'm0'); cv.rect(wx - 2, GROUND - 13, 4, 3, 't2')

def tavern(cv, cx):
    x0, w = cx - 34, 62
    bricks(cv, x0, GROUND - 6, w, 6)
    plaster(cv, x0, 64, w, GROUND - 6 - 64)
    timber_frame(cv, x0, 64, w, GROUND - 6 - 64, [0, 20, 40, 60])
    cv.rect(x0, 84, w, 2, 't1'); cv.hline(x0, 84, w, 't2')
    window(cv, x0 + 6, 70, 8, 9, box=True); window(cv, x0 + 46, 70, 8, 9, box=True); window(cv, x0 + 26, 70, 8, 9, box=True)
    window(cv, x0 + 46, 91, 9, 9)
    window(cv, x0 + 5, 91, 9, 9)
    door(cv, cx - 4, GROUND, 10, 15)
    band_roof(cv, x0 - 5, 46, w + 10, 19, ('r0', 'r1', 'r2', 'r3'), slope=7)
    chimney(cv, x0 + 46, 38, 10)
    DYN['smoke'] = (x0 + 49, 36)
    # dormer
    gable_roof(cv, cx - 2, 54, 7, 8, ('r0', 'r1', 'r2', 'r3'), over=2)
    window(cv, cx - 4, 55, 4, 5)
    # hanging sign: a mug
    cv.hline(x0 + w - 4, 88, 14, 't0'); cv.vline(x0 + w + 9, 88, 3, 'm0')
    cv.rect(x0 + w + 3, 91, 13, 11, 'ink'); cv.rect(x0 + w + 4, 92, 11, 9, 't2')
    cv.stamp(x0 + w + 6, 93, [
        'WWWW..',
        'yyyyyy',
        'yYYYy.y',
        'yYYYy.y',
        'yyyyyy.'], {'W': 'fw', 'y': 'y1', 'Y': 'y2'})
    barrel(cv, x0 - 8, GROUND + 1); barrel(cv, x0 - 2, GROUND + 1); barrel(cv, x0 - 5, GROUND - 8)

def bards_camp(cv, cx):
    # striped tent on the right
    tx = cx + 14
    for j in range(32):
        y = GROUND - 1 - j
        hw = int(22 * (1 - j / 32) ** 0.9) + 1
        for i in range(-hw, hw + 1):
            stripe = ((i + 40) // 5) % 2
            c = ('v1' if stripe else 'c2') if i < 0 else ('v0' if stripe else 'c1')
            if abs(i) >= hw: c = 'ink'
            cv.put(tx + i, y, c)
    for j in range(14):            # dark opening
        hw = int(6 * (1 - j / 14)) + 1
        for i in range(-hw, hw + 1): cv.put(tx + i, GROUND - 1 - j, 'ink' if abs(i) == hw else 'v0' if j > 10 else 'ink2')
    cv.vline(tx, GROUND - 40, 9, 't1'); cv.stamp(tx + 1, GROUND - 40, ['RRR', 'RRRR', 'RR'], {'R': 'R'})
    # log bench and campfire stones (fire itself is animated)
    fx = cx - 6
    cv.rect(fx - 30, GROUND - 4, 16, 4, 't1'); cv.hline(fx - 30, GROUND - 4, 16, 't2'); cv.disc(fx - 30, GROUND - 2, 2, 't3'); cv.put(fx - 30, GROUND - 2, 't1')
    for i, d in enumerate((-7, -4, -1, 2, 5)):
        cv.disc(fx + d, GROUND + 1, 2, 'w1'); cv.put(fx + d - 1, GROUND, 'w3')
    cv.rect(fx - 5, GROUND - 1, 10, 2, 't0')
    DYN['fire'] = (fx, GROUND - 1)
    DYN['bard'] = (fx - 24, GROUND - 4)
    # lute on a stand, flags string
    for k in range(-40, 40):
        y = 60 + int(((k / 40) ** 2) * 10)
        cv.put(cx + k, y, 'ink2')
        if k % 8 == 0:
            col = ['R', 'fy', 'Bc', 'q2', 'pk'][(k // 8) % 5]
            cv.stamp(cx + k - 1, y + 1, ['ccc', 'cc', 'c'], {'c': col})
    cv.vline(cx - 41, 60, GROUND - 60, 't1'); cv.vline(cx + 40, 60, GROUND - 60, 't1')

def owl_tree(cv, cx):
    # big trunk with a hollow
    tx = cx
    for j in range(GROUND - 70):
        y = GROUND - 1 - j
        hw = 7 + (3 if j < 6 else 0) + (2 if j < 2 else 0)
        for i in range(-hw, hw + 1):
            c = 'k1' if i < 2 else 'k0'
            if abs(i) == hw: c = 'ink'
            elif (i * 3 + j) % 9 == 0: c = 'k0'
            cv.put(tx + i, y, c)
    cv.rect(tx - 5, 74, 10, 2, 'k0')
    # branches
    for k in range(18):
        cv.put(tx - 7 - k, 78 - k // 2, 'k0'); cv.put(tx - 7 - k, 79 - k // 2, 'k1')
        cv.put(tx + 7 + k, 76 - k // 3, 'k0'); cv.put(tx + 7 + k, 77 - k // 3, 'k1')
    # canopy
    blobs = [(0, 44, 26), (-24, 54, 16), (24, 52, 17), (-12, 34, 16), (14, 34, 15), (0, 60, 18)]
    for bx, by, r in blobs: cv.disc(tx + bx, by, r + 1, 'ink', ry=int(r * 0.8) + 1)
    for bx, by, r in blobs: shade_disc(cv, tx + bx, by, r, ('L0', 'L1', 'L2', 'L3'), ry=int(r * 0.8))
    rng = np.random.default_rng(9)
    for _ in range(80):
        x, y = tx + int(rng.integers(-40, 40)), int(rng.integers(20, 76))
        if cv.get(x, y) and cv.get(x, y) != IDX['ink']: cv.put(x, y, ['L1', 'L2', 'L3'][int(rng.integers(0, 3))])
    # apples
    for ax, ay in ((-18, 50), (10, 40), (22, 58), (-6, 30), (-28, 60)):
        cv.put(tx + ax, ay, 'R'); cv.put(tx + ax + 1, ay, 'R'); cv.put(tx + ax, ay - 1, 'r3')
    # hollow with owl (eyes animated)
    cv.disc(tx, 92, 6, 'ink', ry=7); cv.disc(tx, 92, 5, 'ink2', ry=6)
    cv.stamp(tx - 5, 85, [
        '.b.....b.',
        '.bbbbbbb.',
        'bBBBBBBBb',
        'bWWWBWWWb',
        'bWWWBWWWb',
        'bWWWBWWWb',
        'bBBByBBBb',
        '.bBBBBBb.',
        '.bbBBBbb.',
        '..bbbbb..'], {'b': 't1', 'B': 't2', 'W': 'fw', 'y': 'y2'})
    DYN['owl'] = (tx, 88)
    # swing
    cv.vline(tx - 18, 70, 30, 't0'); cv.vline(tx - 10, 70, 30, 't0')
    cv.rect(tx - 20, 100, 13, 2, 't2'); cv.hline(tx - 20, 101, 13, 't0')
    # mushrooms
    for mx in (cx + 22, cx + 27):
        cv.vline(mx, GROUND - 3, 3, 'fw'); cv.rect(mx - 2, GROUND - 5, 5, 2, 'R'); cv.put(mx - 1, GROUND - 5, 'fw')

def pet_cottage(cv, cx):
    x0, w = cx - 26, 46
    bricks(cv, x0, GROUND - 5, w, 5)
    for j in range(78, GROUND - 5):                 # wooden plank walls
        for i in range(x0, x0 + w):
            c = 't2' if (j - 78) % 4 else 't1'
            if (i - x0) % 12 == 0 and (j // 4) % 2: c = 't1'
            cv.put(i, j, c)
    cv.vline(x0, 78, GROUND - 83, 't3')
    gable_roof(cv, cx - 3, 77, 25, 22, ('q0', 'q1', 'q2', 'q3'), over=4)
    window(cv, cx - 1, 60, 5, 6, arch=True)
    window(cv, x0 + 6, 86, 8, 8, box=True)
    door(cv, cx + 4, GROUND, 9, 15, col=('q0', 'q1', 'q2'))
    # round door window and a paw sign
    cv.disc(cx + 8, GROUND - 11, 1, 'win2')
    cv.rect(x0 + 18, 81, 9, 8, 't0'); cv.rect(x0 + 19, 82, 7, 6, 'c2')
    cv.stamp(x0 + 20, 82, ['p.p.p', '.....', '.ppp.', 'ppppp', '.ppp.'], {'p': 'pkd'})
    # pet bed + bowl in the yard
    bx = cx + 30
    cv.rect(bx - 6, GROUND - 3, 13, 3, 'R'); cv.hline(bx - 6, GROUND - 3, 13, 'pk'); cv.rect(bx - 4, GROUND - 2, 9, 1, 'Rd')
    cv.rect(bx + 10, GROUND - 2, 5, 2, 'Bc'); cv.hline(bx + 11, GROUND - 3, 3, 't3')
    DYN['pet'] = (cx + 30, GROUND)
    fence(cv, x0 - 16, GROUND + 1, 15)

def fortune_tent(cv, cx):
    # round top pavilion
    tx = cx
    for j in range(46):
        y = GROUND - 1 - j
        if j < 22: hw = 28
        else:
            t = (j - 22) / 24
            hw = int(31 * (1 - t ** 1.4)) + 1
        for i in range(-hw, hw + 1):
            seg = int((i + 64) / 7) % 2
            c = ('pk' if seg else 'fw') if i < 6 else ('pkd' if seg else 'Wd')
            if j < 22 and j > 0 and abs(i) >= hw - 1: c = 'ink'
            if j >= 22 and abs(i) >= hw: c = 'ink'
            cv.put(tx + i, y, c)
    # scalloped valance
    for i in range(-31, 32):
        for k in range(3):
            if (i % 6 - 3) ** 2 + k * k * 3 < 10: cv.put(tx + i, GROUND - 24 + k, 'v1' if i < 6 else 'v0')
    cv.hline(tx - 31, GROUND - 25, 63, 'y1')
    # entrance curtains
    for j in range(20):
        hw = 7 - j // 5
        for i in range(-8, 9):
            if abs(i) < 2 + j // 3: cv.put(tx + i, GROUND - 1 - j, 'ink2' if abs(i) < 1 + j // 3 else 'v0')
    cv.vline(tx, GROUND - 48, 4, 'y1'); cv.stamp(tx - 2, GROUND - 53, ['.y.', 'yYy', '.y.'], {'y': 'y1', 'Y': 'y3'})
    # crystal ball on a table outside
    bx = cx - 40
    cv.rect(bx - 6, GROUND - 9, 13, 2, 'v1'); cv.hline(bx - 6, GROUND - 9, 13, 'v2')
    cv.vline(bx - 4, GROUND - 7, 7, 't1'); cv.vline(bx + 4, GROUND - 7, 7, 't1')
    cv.disc(bx, GROUND - 14, 5, 'ink'); cv.disc(bx, GROUND - 14, 4, 'crys2'); cv.disc(bx - 1, GROUND - 15, 2, 'crys'); cv.put(bx - 2, GROUND - 16, 'fw')
    DYN['crystal'] = (bx, GROUND - 14)
    # big dice sign
    sx = cx + 38
    cv.vline(sx, GROUND - 26, 26, 't1')
    cv.rect(sx - 6, GROUND - 38, 13, 13, 'ink'); cv.rect(sx - 5, GROUND - 37, 11, 11, 'fw'); cv.hline(sx - 5, GROUND - 27, 11, 'Wd'); cv.vline(sx + 5, GROUND - 37, 11, 'Wd')
    for dx, dy in ((-3, -3), (2, -3), (0, 0), (-3, 3), (2, 3)): cv.rect(sx + dx, GROUND - 32 + dy, 2, 2, 'R')

def quest_board(cv, cx):
    # post office kiosk on the right
    x0, w = cx + 4, 34
    bricks(cv, x0, GROUND - 5, w, 5)
    plaster(cv, x0, 76, w, GROUND - 81)
    timber_frame(cv, x0, 76, w, GROUND - 81, [0, w - 2])
    band_roof(cv, x0 - 4, 62, w + 8, 15, ('b0', 'b1', 'b2', 'b3'), slope=5)
    window(cv, x0 + 5, 84, 8, 8)
    door(cv, x0 + 20, GROUND, 9, 14, col=('b0', 'b1', 'b2'))
    cv.rect(x0 + 6, 66, 22, 7, 'ink'); cv.rect(x0 + 7, 67, 20, 5, 'y1')   # sign plate
    cv.stamp(x0 + 13, 67, ['WWWWWWW', 'WbWWWbW', 'WWbWbWW', 'WWWbWWW', 'WWWWWWW'], {'W': 'fw', 'b': 'Bd'})
    # notice board on the left
    bx, bw = cx - 40, 34
    cv.vline(bx + 2, GROUND - 32, 32, 't0'); cv.vline(bx + bw - 3, GROUND - 32, 32, 't0')
    cv.rect(bx, GROUND - 34, bw, 22, 'ink'); cv.rect(bx + 1, GROUND - 33, bw - 2, 20, 't2')
    band_roof(cv, bx - 3, GROUND - 40, bw + 6, 6, ('r0', 'r1', 'r2', 'r3'), slope=3)
    notes = [(3, 3, 8, 7, 'fw'), (13, 2, 7, 9, 'c2'), (22, 4, 8, 6, 'fy'), (5, 12, 9, 6, 'c2'), (17, 12, 6, 6, 'pk'), (25, 11, 6, 7, 'fw')]
    for nx, ny, nw, nh, c in notes:
        cv.rect(bx + nx, GROUND - 33 + ny, nw, nh, c)
        for k in range(1, nh - 1, 2): cv.hline(bx + nx + 1, GROUND - 33 + ny + k, nw - 2, 'Wd' if c != 'fy' else 'y1')
        cv.put(bx + nx + nw // 2, GROUND - 33 + ny, 'R')
    DYN['board'] = (bx + bw // 2, GROUND - 24)
    # mailbox
    mx = cx - 2
    cv.vline(mx, GROUND - 10, 10, 't0'); cv.rect(mx - 4, GROUND - 17, 9, 7, 'ink'); cv.rect(mx - 3, GROUND - 16, 7, 5, 'Bc'); cv.hline(mx - 3, GROUND - 16, 7, 'fb')
    cv.vline(mx + 5, GROUND - 19, 4, 'm0'); cv.rect(mx + 5, GROUND - 19, 3, 2, 'R')

def gallery(cv, cx):
    x0, w = cx - 34, 60
    cv.rect(x0 - 3, GROUND - 4, w + 6, 4, 'w2'); cv.hline(x0 - 3, GROUND - 4, w + 6, 'w3'); cv.hline(x0 - 1, GROUND - 2, w + 2, 'w1')
    cv.rect(x0, 62, w, GROUND - 66, 'w2')
    for i in range(6):  # columns
        px = x0 + 3 + i * 11
        cv.rect(px, 66, 5, GROUND - 70, 'w3'); cv.vline(px + 4, 66, GROUND - 70, 'w1'); cv.vline(px + 2, 68, GROUND - 74, 'w2')
        cv.rect(px - 1, 64, 7, 2, 'w3'); cv.rect(px - 1, GROUND - 6, 7, 2, 'w1')
    for wx in (x0 + 8, x0 + 41):
        cv.rect(wx, 68, 9, 40, 'w1'); cv.vline(wx, 68, 40, 'w0')
    for (px, py, c, d) in ((x0 + 9, 74, 'R', 'fy'), (x0 + 42, 78, 'Bc', 'a3')):
        cv.rect(px - 1, py - 1, 9, 12, 'y0'); cv.rect(px, py, 7, 10, 'y1'); cv.rect(px + 1, py + 1, 5, 8, c)
        cv.put(px + 2, py + 2, d); cv.put(px + 3, py + 5, d); cv.hline(px + 1, py + 7, 5, 'g1')
    cv.rect(x0 + 19, 70, 20, 41, 'w1'); cv.rect(x0 + 21, 82, 16, 27, 'ink')
    door(cv, x0 + 23, GROUND - 4, 12, 18, col=('t0', 't1', 't2'))
    cv.disc(x0 + 29, 77, 3, 'y1'); cv.disc(x0 + 29, 77, 2, 'win'); cv.put(x0 + 28, 76, 'win2')
    # pediment
    for j in range(12):
        hw = int((w // 2 + 5) * (1 - j / 12))
        for i in range(-hw, hw + 1):
            cv.put(cx - 4 + i, 61 - j, 'ink' if abs(i) == hw or j == 0 else 'w3' if i < 0 else 'w2')
    cv.disc(cx - 4, 55, 3, 'y1'); cv.put(cx - 5, 54, 'y3')
    cv.rect(x0 - 2, 60, w + 4, 3, 'w3'); cv.hline(x0 - 2, 62, w + 4, 'w0')
    # easel with a painting
    ex = cx + 40
    cv.put(ex - 5, GROUND - 1, 't1'); cv.put(ex + 5, GROUND - 1, 't1')
    for k in range(22):
        cv.put(ex - 5 + k // 4, GROUND - 1 - k, 't1'); cv.put(ex + 5 - k // 4, GROUND - 1 - k, 't1')
    cv.rect(ex - 8, GROUND - 26, 17, 13, 'ink'); cv.rect(ex - 7, GROUND - 25, 15, 11, 'a2')
    cv.rect(ex - 7, GROUND - 18, 15, 4, 'g2'); cv.disc(ex + 3, GROUND - 22, 2, 'fy'); cv.put(ex - 4, GROUND - 19, 'L1'); cv.put(ex - 3, GROUND - 20, 'L1')
    cv.hline(ex - 8, GROUND - 13, 17, 't2')

def theatre(cv, cx):
    x0, w = cx - 36, 66
    bricks(cv, x0, 60, w, GROUND - 60, cols=('v0', 'v1', 'v2', 'v3'))
    cv.vline(x0, 60, GROUND - 60, 'v3'); cv.vline(x0 + w - 1, 60, GROUND - 60, 'v0')
    # crenellated top with a dome
    for i in range(x0 - 2, x0 + w + 2, 6): cv.rect(i, 55, 4, 5, 'v2'); cv.hline(i, 55, 4, 'v3')
    cv.rect(x0 - 2, 58, w + 4, 3, 'v1')
    shade_disc(cv, cx - 3, 54, 12, ('y0', 'y1', 'y2', 'y3'), ry=10)
    cv.rect(cx - 16, 54, 27, 6, 'v1')
    cv.vline(cx - 3, 38, 6, 'y1'); cv.disc(cx - 3, 37, 1, 'fy')
    # marquee sign (bulbs animated)
    cv.rect(x0 + 8, 64, w - 16, 12, 'ink'); cv.rect(x0 + 9, 65, w - 18, 10, 'Rd')
    DYN['marquee'] = (x0 + 9, 65, w - 18, 10)
    # arch with curtains
    ax, aw = cx - 15, 24
    cv.rect(ax - 2, 80, aw + 4, GROUND - 80, 'y1')
    cv.rect(ax, 82, aw, GROUND - 82, 'ink')
    for i in range(aw):
        fold = 'R' if (i // 2) % 2 else 'Rd'
        drop = int(abs((i - aw / 2)) * 0.6)
        if i < 8 or i >= aw - 8: cv.vline(ax + i, 82, GROUND - 82, fold)
        else: cv.vline(ax + i, 82, 6 - drop // 2 + 2, fold)
    cv.hline(ax, 82, aw, 'y2')
    # posters
    for px in (x0 + 4, x0 + w - 14):
        cv.rect(px, 84, 10, 14, 'ink'); cv.rect(px + 1, 85, 8, 12, 'fw'); cv.rect(px + 2, 86, 6, 6, 'Bc' if px < cx else 'q2'); cv.hline(px + 2, 93, 6, 'ink2'); cv.hline(px + 2, 95, 4, 'ink2')
    cv.rect(x0 - 2, GROUND - 3, w + 4, 3, 'v1'); cv.hline(x0 - 2, GROUND - 3, w + 4, 'v3')

def guild_hall(cv, cx):
    x0, w = cx - 38, 72
    bricks(cv, x0, 66, w, GROUND - 66)
    cv.vline(x0, 66, GROUND - 66, 'w3')
    band_roof(cv, x0 - 5, 44, w + 10, 23, ('b0', 'b1', 'b2', 'b3'), slope=10)
    gable_roof(cv, cx - 2, 64, 16, 22, ('b0', 'b1', 'b2', 'b3'), over=3)
    window(cv, cx - 5, 50, 6, 8, arch=True)
    # twin towers
    for tx in (x0 - 4, x0 + w - 8):
        bricks(cv, tx, 52, 12, GROUND - 52)
        cv.vline(tx, 52, GROUND - 52, 'w3'); cv.vline(tx + 11, 52, GROUND - 52, 'w0')
        gable_roof(cv, tx + 6, 51, 6, 14, ('r0', 'r1', 'r2', 'r3'), over=2)
        window(cv, tx + 4, 62, 4, 7, arch=True)
    DYN['flag'] = (x0 + 2, 33)
    cv.vline(x0 + 2, 33, 4, 'm0')
    # banners
    for bx in (x0 + 14, x0 + w - 24):
        cv.rect(bx, 70, 10, 22, 'ink'); cv.rect(bx + 1, 70, 8, 20, 'R'); cv.vline(bx + 1, 70, 20, 'r3')
        cv.put(bx + 1, 91, None); cv.put(bx + 8, 91, None); cv.rect(bx + 3, 90, 4, 1, 'R'); cv.rect(bx + 4, 91, 2, 1, 'R')
        cv.rect(bx, 69, 10, 1, 'y1')
        cv.stamp(bx + 2, 74, ['.yyyy.', 'yWWWWy', 'yWyyWy', 'yWWWWy', '.yWWy.', '..yy..'], {'y': 'y2', 'W': 'Bc'})
    # double door
    cv.rect(cx - 11, 86, 20, GROUND - 86, 'w3')
    door(cv, cx - 8, GROUND, 14, 22, col=('t0', 't1', 't2'))
    cv.vline(cx - 1, GROUND - 21, 21, 'ink')
    cv.put(cx - 3, GROUND - 11, 'y2'); cv.put(cx + 1, GROUND - 11, 'y2')
    for sx in (cx - 18, cx + 13):
        cv.rect(sx, GROUND - 14, 4, 6, 'm0'); cv.rect(sx + 1, GROUND - 13, 2, 3, 'f2')  # wall lamps
    cv.rect(cx - 13, GROUND - 2, 24, 2, 'w2'); cv.hline(cx - 13, GROUND - 2, 24, 'w3')

def lighthouse(cv, cx):
    # rocks and a little sea at the end of the street
    lx = cx + 4
    for j in range(66):
        y = GROUND - 1 - j
        hw = int(11 - j * 0.06)
        for i in range(-hw, hw + 1):
            band = ((y - 20) // 10) % 2
            c = ('R' if band else 'fw') if i < 3 else ('Rd' if band else 'Wd')
            if abs(i) == hw: c = 'ink'
            cv.put(lx + i, y, c)
    window(cv, lx - 2, 70, 4, 6, arch=True); window(cv, lx - 2, 92, 4, 6, arch=True)
    door(cv, lx - 3, GROUND, 7, 11, col=('b0', 'b1', 'b2'))
    # gallery and lantern room
    cv.rect(lx - 12, 45, 25, 3, 'ink'); cv.hline(lx - 11, 46, 23, 'm1')
    for i in range(lx - 11, lx + 12, 3): cv.vline(i, 42, 3, 'm0')
    cv.hline(lx - 11, 41, 23, 'm0')
    cv.rect(lx - 6, 32, 13, 10, 'ink'); cv.rect(lx - 5, 33, 11, 9, 'f2'); cv.rect(lx - 2, 35, 5, 5, 'f3')
    cv.vline(lx - 2, 33, 9, 'm0'); cv.vline(lx + 2, 33, 9, 'm0')
    DYN['lamp'] = (lx, 37)
    gable_roof(cv, lx, 31, 7, 9, ('r0', 'r1', 'r2', 'r3'), over=1)
    cv.disc(lx, 20, 1, 'y2')
    # rocks
    for (rx, ry, r) in ((-18, 0, 7), (16, 1, 8), (-8, 2, 5), (26, -1, 5)):
        shade_disc(cv, cx + rx, GROUND + ry - 2, r, ('w0', 'w1', 'w2', 'w3'), ry=r // 2 + 2)

BUILD = [clock_tower, tavern, bards_camp, owl_tree, pet_cottage, fortune_tent, quest_board, gallery, theatre, guild_hall, lighthouse]

def between(cv, i):
    """Scenery between landmark i and i+1."""
    x = i * SPACING + SPACING
    k = i % 4
    if k == 0:
        lamppost(cv, x, GROUND + 1); bush(cv, x - 10, GROUND + 1, 8); flowers(cv, x + 4, GROUND - 2, 10, seed=i)
    elif k == 1:
        tree(cv, x, GROUND + 1, 0.9, seed=i); lamppost(cv, x + 14, GROUND + 1)
    elif k == 2:
        lamppost(cv, x - 4, GROUND + 1); crate(cv, x + 4, GROUND + 1); crate(cv, x + 6, GROUND - 7, 6); bush(cv, x - 15, GROUND + 1, 6, )
    else:
        tree(cv, x - 2, GROUND + 1, 0.75, seed=i + 5); flowers(cv, x - 14, GROUND - 2, 8, seed=i + 9); lamppost(cv, x + 12, GROUND + 1)
    DYN.setdefault('lamps', [])

def build_world():
    cv = Canvas(WW, 140 - WY0, y0=WY0, wrap=True)
    street(cv)
    for i in range(NLOC):
        between(cv, i)
    for i, f in enumerate(BUILD):
        f(cv, cxof(i))
    # lamps list for glows at night
    lamps = []
    for x in range(WW):
        for y in range(WY0, 140):
            if cv.get(x, y) == IDX['f3'] and cv.get(x, y - 1) == IDX['f2'] and cv.get(x - 1, y) == IDX['f2']: lamps.append((x, y))
    return cv, lamps
