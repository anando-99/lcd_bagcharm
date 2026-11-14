# The ronin (original design): black lacquer helmet with a gold crescent, red lacquer armour, a mask, a katana.
SAM_MAP = {'k': 'ink', 'H': 'b1', 'h': 'b0', 'j': 'b3', 'y': 'y2', 'Y': 'y3', 'S': 'sk', 's': 'sk2', 'e': 'ink',
           'M': 'r2', 'm': 'r0', 'T': 'tl', 't': 'tl2', 'u': 'tl3', 'G': 'y1', 'A': 'tl2', 'a': 'tl',
           'P': 'pn', 'p': 'pn2', 'O': 't2', 'o': 't1', 'w': 'fw', 'K': 'K', 'B': 'Wd', 'x': 'm2'}

# side view, facing right
SIDE_TOP = [
  '.....y.....y....',
  '.....yy...yy....',
  '......yyyyy.....',
  '.....kkkkkkk....',
  '....kHHHHHHjk...',
  '...kHHHHHHHHjk..',
  '..khHHHHHHHHHk..',
  '.khhkkkkkkkkkkk.',
  '.khhkSSSSSeSk...',
  'khhhkSSSSSSSk...',
  'khhhkMMMMMMMMk..',
  '.kkkkMmMmMMMk...',
  '..kAAkkkkkkkk...',
  '.kAaATTTTTuTk...',
  '.kAAATGGGGGTkk..',
  '.kAaATTTTTTTkSk.',
  '.kAAATGGGGGTkSk.',
  '..kkkBBBBBBBkk..',
  '..kKkTtTtTtTkwKk',
  '..kKktTtTtTtkKk.',
]
LEGS = {
  'stand': ['...kPPPkPPPk....', '...kPPpkPPpk....', '...kPPpkPPpk....', '..kOOOk.kOOOk...', '..kkkkk.kkkkk...', '................'],
  'walk1': ['...kPPPPkPPPk...', '..kPPpk..kPPpk..', '.kPPpk....kPPk..', '.kOOk.....kOOOk.', '.kkk......kkkk..', '................'],
  'walk2': ['...kPPPPPPk.....', '....kPPpPPk.....', '....kPPkPk......', '...kOOOOOk......', '...kkkkkkk......', '................'],
  'walk3': ['...kPPPkPPPPk...', '...kPPpk.kPPk...', '..kPPk....kPk...', '.kOOOk....kOOk..', '.kkkkk.....kkk..', '................'],
}
# back view (climbing up the stairs)
BACK_TOP = [
  '....y.......y...',
  '....yy.....yy...',
  '.....yyyyyyy....',
  '....kkkkkkkk....',
  '...kHHHjjHHHk...',
  '..kHHHHHHHHHHk..',
  '.kHHHHHHHHHHHHk.',
  'khhhhhhhhhhhhhhk',
  'khHhHhHhHhHhHhhk',
  '.khhhhhhhhhhhhk.',
  '..kkkkkkkkkkkk..',
  '.kAAkTTTTTTkAAk.',
  'kAaAkTGGGGTkAaAk',
  'kAAAkTTTTTTkAAAk',
  'kAaAkTGGGGTkAaAk',
  '.kkkkTTTTTTkkkk.',
  '...kBBBBBBBBk...',
  '...kTtTtTtTtk...',
  '...ktTtTtTtTk...',
]
BACK_LEGS = {
  'a': ['...kPPPkkPPPk...', '...kPPpk.kPPk...', '...kPPpk.kPPk...', '...kOOOk.kPPk...', '...kkkkk.kOOk...', '.........kkk....', '................'],
  'b': ['...kPPPkkPPPk...', '...kPPk.kpPPk...', '...kPPk.kpPPk...', '...kPPk.kOOOk...', '...kOOk.kkkkk...', '....kkk.........', '................'],
}
# front view (coming down)
FRONT_TOP = [
  '....y.......y...',
  '....yy.....yy...',
  '.....yyyyyyy....',
  '....kkkYYkkk....',
  '...kHHHHHHHHk...',
  '..kHHHHjjHHHHk..',
  '.kHHkkkkkkkkHHk.',
  'khhkSSSSSSSSkhhk',
  'khhkSeSSSSeSkhhk',
  '.kkkMMMMMMMMkkk.',
  '...kMmMmMmMMk...',
  '.kAAkkkkkkkkAAk.',
  'kAaAkTTuuTTkAaAk',
  'kAAAkTGGGGTkAAAk',
  'kAaAkTTTTTTkAaAk',
  'kSkkkTGGGGTkkkSk',
  '.kk.kBBBBBBk.kk.',
  '...kTtTtTtTtk...',
  '...ktTtTtTtTk...',
]

def side(legs, bob=0):
    rows = ['................'] * bob + SIDE_TOP + LEGS[legs]
    return (rows + ['................'] * 26)[:26]

def vert(top, legs, bob=0):
    rows = ['................'] * bob + top + legs
    return (rows + ['................'] * 26)[:26]

SAM_FRAMES = {
  'idle0': side('stand', 0), 'idle1': side('stand', 1),
  'walk0': side('walk1', 0), 'walk1': side('walk2', 1), 'walk2': side('walk3', 0), 'walk3': side('walk2', 1),
  'up0': vert(BACK_TOP, BACK_LEGS['a'], 0), 'up1': vert(BACK_TOP, BACK_LEGS['b'], 1),
  'down0': vert(FRONT_TOP, BACK_LEGS['a'], 0), 'down1': vert(FRONT_TOP, BACK_LEGS['b'], 1),
}
SAM_ORDER = ['idle0', 'idle1', 'walk0', 'walk1', 'walk2', 'walk3', 'up0', 'up1', 'down0', 'down1']

# drawing the sword (shake): arm out, blade to the right. 28 x 26
SLASH = [r + '............' for r in SIDE_TOP[:15]] + [
  '.kAaATTTTTTTkSSkxxxxxxxxxxx.',
  '.kAAATGGGGGTkkSkwwxxxxxxxxxxx',
  '..kkkBBBBBBBkk..kk..........',
  '..kKkTtTtTtTk...............',
  '..kKktTtTtTtk...............',
] + [r + '............' for r in LEGS['walk1']]
SLASH = [(r + '.' * 28)[:28] for r in SLASH][:26]

# kneeling in meditation (charging), 18 x 20
SEATED = [
  '......y.....y.....',
  '......yy...yy.....',
  '.......yyyyy......',
  '......kkkkkkk.....',
  '.....kHHHHHHjk....',
  '....kHHHHHHHHjk...',
  '...khHHHHHHHHHk...',
  '..khhkkkkkkkkkkk..',
  '..khhkSSSSSeSk....',
  '..khhkSSSSSSSk....',
  '...kkkMMMMMMMk....',
  '...kAAkkkkkkkk....',
  '..kAaATTTTTuTk....',
  '..kAAATGGGGGTkk...',
  '..kAaATTTTTTSSk...',
  '..kkkkBBBBBBBkk...',
  '.kPPPPPPPPPPPPPk..',
  'kPPpPPPPPPPpPPPPk.',
  'kkkkkkkkkkkkkkkkk.',
  '..kKKKKKKKKKKKKwk.',
]
# the banner on his back (sashimono), drawn separately so it can flutter: 6 x 10
FLAG = ['kkkkk.', 'kRRRk.', 'kRwRk.', 'kRRRk.', 'kRRRk.', 'kRRRk.', 'kkkkk.']
FLAG_MAP = {'k': 'ink', 'R': 'R', 'w': 'fw'}
