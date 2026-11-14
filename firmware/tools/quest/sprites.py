# Hand-drawn sprites for the Quest look (all original).
HERO_MAP = {'k': 'ink', 'H': 'b1', 'h': 'b0', 'j': 'b2', 'J': 'b3', 'F': 'R', 'f': 'r3',
            'Y': 'hr', 'y': 'hr2', 'S': 'sk', 's': 'sk2', 'e': 'ink', 'w': 'fw',
            'R': 'R', 'r': 'Rd', 'T': 'tl', 't': 'tl2', 'u': 'tl3', 'B': 't1', 'G': 'y2',
            'P': 'pn', 'p': 'pn2', 'O': 't2', 'o': 't0'}

# Upper body, facing right. 16 wide.
HERO_TOP = [
  '.......kkk......',
  '......kjJjk.....',
  '.....kjHHHk..ff.',
  '.....kHHHHHk.fF.',
  '....khHHHHHkFFk.',
  '..kkhhhhhhhhkkk.',
  '.kjjJJjHHHHHHHjk',
  '..kkkYYYYYYkkkk.',
  '...kYYYYSSSYk...',
  '..kYYySSwSSwSk..',
  '..kYySSSeSSeSk..',
  '..kYySSSeSSeSk..',
  '...kySSSSSSSsk..',
  '....kkSSSsSSk...',
  '...kRRRRRRRRk...',
  '..kRrrRRRRRRrk..',
  '..kTTTTTTTTuTk..',
  '.kSkTTTTTTuTkSk.',
  '.kSkTTGTTTuTkSk.',
  '..kkBBBGBBBBkk..',
  '...kTTTTTTTTk...',
]
# legs per frame (rows below the body). Facing right.
LEGS = {
  'stand': ['...kTTkkkkTTk...', '...kPPk..kPPk...', '...kPPk..kPPk...', '..kOOOk.kOOOk...', '..kkkkk.kkkkk...'],
  'walk1': ['...kTTkkkkTTk...', '..kPPk....kPPk..', '..kPk......kPPk.', '.kOOk.....kOOOk.', '.kkk......kkkk..'],
  'walk2': ['...kTTkkkkTTk...', '....kPPkPPk.....', '....kPPkPk......', '...kOOOOOk......', '...kkkkkkk......'],
  'walk3': ['...kTTkkkkTTk...', '...kPPk..kPPk...', '..kPPk....kPk...', '.kOOOk....kOOk..', '.kkkkk.....kkk..'],
  'jump':  ['...kTTkkkkTTk...', '..kPPk..kPPk....', '.kOOk..kOOOk....', '.kkk...kkkk.....', '................'],
}
SCARF = [  # trailing scarf tail, drawn to the left of the body at rows 14..17
  ['kRR', 'kRr', '.kk', '...'],
  ['.kR', 'kRr', 'kr.', 'k..'],
  ['kRRR', 'kRr.', 'k...', '....'],
]

def hero_frame(legs='stand', bob=0, scarf=0, wave=False):
    top = [r for r in HERO_TOP]
    rows = ['................'] * bob + top + LEGS[legs]
    rows = rows[:26] + ['................'] * (26 - len(rows))
    # scarf tail
    sc = SCARF[scarf]
    rows = [list(r) for r in rows]
    for j, s in enumerate(sc):
        for i, ch in enumerate(s):
            x, y = 3 - len(s) + i, 14 + bob + j
            if ch != '.' and 0 <= x and rows[y][x] == '.': rows[y][x] = ch
    return [''.join(r) for r in rows]

HERO_FRAMES = {
  'idle0': hero_frame('stand', 0, 0), 'idle1': hero_frame('stand', 1, 1),
  'walk0': hero_frame('walk1', 0, 2), 'walk1': hero_frame('walk2', 1, 1),
  'walk2': hero_frame('walk3', 0, 2), 'walk3': hero_frame('walk2', 1, 0),
  'jump': hero_frame('jump', 0, 2),
}
HERO_ORDER = ['idle0', 'idle1', 'walk0', 'walk1', 'walk2', 'walk3', 'jump']

# The hero asleep (inn screen): lying on its back, 24 x 10
SLEEP = [
  '....kkkk................',
  '...kjHHHk...............',
  '..kjHHHHHk.kkkkkkkkkkk..',
  '.kkhhhhhhhkTTTTTTTuTTTk.',
  'kjJJjHHHHkYSSSRRTTTTTTTk',
  '.kkkkYYYYYSeSSRRTTTGTTTk',
  '....kYySSSSSSSRrTTTTTTk.',
  '.....kkkkkkkkkkkkkkkkkk.',
]

# A wandering minstrel for the camp (seated, playing a lute), 14 x 18
BARD = [
  '....kkkkk.....',
  '...kvVVVVk.F..',
  '..kvVVVVVVkFf.',
  '.kkvvvvvvvvkk.',
  '...kYYYYYYk...',
  '..kYSSSSSSk...',
  '..kYSeSSeSk...',
  '...kSSSSSk....',
  '..kGGGGGGGk...',
  '.kGGgGGGGGGk..',
  '.kGkooOOOOkk..',
  'kGkoOOOOOooOk.',
  'kkkoOOOOOookk.',
  '..kGGGGGGGk...',
  '..kPPPPPPPPk..',
  '..kPPkkkPPPk..',
  '.kOOk...kOOk..',
  '.kkk.....kkk..',
]
BARD_MAP = {'k': 'ink', 'v': 'v0', 'V': 'v1', 'F': 'q2', 'f': 'q3', 'Y': 'hr2', 'S': 'sk', 'e': 'ink',
            'G': 'q1', 'g': 'q2', 'o': 't1', 'O': 't3', 'P': 'pn', }

NOTE = ['..kk', '..kW', '..k.', 'kkk.', 'kkk.']
NOTE2 = ['.kkkk', '.k..k', '.k..k', 'kk.kk', 'kk.kk']

COIN = [['.kkk.', 'kyYyk', 'kyYyk', 'kyYyk', '.kkk.'], ['..k..', '.kYk.', '.kYk.', '.kYk.', '..k..']]
COIN_MAP = {'k': 'y0', 'y': 'y1', 'Y': 'y3'}

# small HUD icons (8x8)
ICON_HEART = ['.kk.kk..', 'kRRkRrk.', 'kRwRRrk.', 'kRRRRrk.', '.kRRrk..', '..kRk...', '...k....', '........']
ICON_SUN = ['...y....', '.y.y.y..', '..YYY...', 'yYYYYYy.', '..YYY...', '.y.y.y..', '...y....', '........']
ICON_MOON = ['..kkk...', '.kYYk...', 'kYYk....', 'kYYk....', 'kYYYk...', '.kYYYYk.', '..kkkk..', '........']
ICON_GEM = ['..kkk...', '.kcCck..', 'kcCCCck.', '.kcCck..', '..kck...', '...k....', '........', '........']
ICON_HAND = ['.kkk.....', 'kWWWkkk..', 'kWWWWWWk.', 'kWWWkkk..', 'kWWWWk...', '.kWWWk...', '..kkk....']
ICON_MAP = {'k': 'ink', 'R': 'R', 'r': 'Rd', 'w': 'fw', 'y': 'y1', 'Y': 'y2', 'c': 'crys2', 'C': 'crys', 'W': 'fw'}

CLOUDS = [
  ['.......WWWW.........',
   '.....WWwwwwWW..WWW..',
   '...WWwwwwwwwwWWwwwW.',
   '..WwwwwwwwwwwwwwwwwW',
   '.WwwwwwwwwwwwwwwwwwW',
   'WwwwwwwwwwwwwwwwwwsW',
   'WsswwwwwwwwwwwwsssW.',
   '.WsssswwwwwwssssWW..',
   '..WWWWssssssWWWW....',
   '......WWWWWW........'],
  ['....WWW.......',
   '..WWwwwWW.WW..',
   '.WwwwwwwwWwwW.',
   'WwwwwwwwwwwwwW',
   'WsswwwwwwwwssW',
   '.WWsssssssWW..',
   '...WWWWWWW....'],
]
CLOUD_MAP = {'W': 'cl2', 'w': 'cl', 's': 'cl3'}
