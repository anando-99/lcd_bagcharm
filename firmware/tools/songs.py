# Public-domain melodies for the buzzer jukebox. One source -> C header + JS module.
import re, json, sys
NOTE = {'C':0,'D':2,'E':4,'F':5,'G':7,'A':9,'B':11}
DUR = {'s':1,'e':2,'q':4,'h':8,'w':16}
CATS = ["Christmas", "Rhymes", "Beethoven", "Classics & folk", "Pixel originals"]
# (name, category, bpm, transpose, notes). Pixel originals are new tunes written for the charm.
SONGS = [
 ("Jingle Bells", 0, 200, 12, """E4q E4q E4h E4q E4q E4h E4q G4q C4q. D4e E4w F4q F4q F4q. F4e F4q E4q E4q E4e E4e
   E4q D4q D4q E4q D4h G4h E4q E4q E4h E4q E4q E4h E4q G4q C4q. D4e E4w F4q F4q F4q F4e F4q E4q E4q E4e E4e
   G4q G4q F4q D4q C4w"""),
 ("We Wish You a Merry Christmas", 0, 180, 12, """D4q G4q G4e A4e G4e F#4e E4q E4q E4q A4q A4e B4e A4e G4e F#4q D4q D4q
   B4q B4e C5e B4e A4e G4q E4q D4e D4e E4q A4q F#4q G4h."""),
 ("Deck the Halls", 0, 150, 12, """G4q. F4e E4q D4q C4q D4q E4q C4q D4e E4e F4e D4e E4q. D4e C4q B3q C4h
   G4q. F4e E4q D4q C4q D4q E4q C4q D4e E4e F4e D4e E4q. D4e C4q B3q C4h
   D4q. E4e F4q D4q E4q. F4e G4q D4q E4e F#4e G4q A4e B4e C5q B4q A4q G4h
   G4q. F4e E4q D4q C4q D4q E4q C4q A4e A4e A4e A4e G4q. F4e E4q D4q C4h"""),
 ("Silent Night", 0, 100, 12, """G4q. A4e G4q E4h. G4q. A4e G4q E4h. D5h D5q B4h. C5h C5q G4h.
   A4h A4q C5q. B4e A4q G4q. A4e G4q E4h. A4h A4q C5q. B4e A4q G4q. A4e G4q E4h.
   D5h D5q F5q. D5e B4q C5h. E5h. C5q G4q E4q G4q. F4e D4q C4h."""),
 ("O Christmas Tree", 0, 110, 12, """D4q G4e. G4s G4q. A4e B4e. B4s B4q. B4e A4e B4e C5q F#4q A4q G4h. Rq
   D5e B4e E5q. D5e D5e C5e C5q. C5e A4e D5q. C5e C5e B4e B4q. D4q G4e. G4s G4q. A4e B4e. B4s B4q. B4e A4e B4e C5q F#4q A4q G4h."""),
 ("Joy to the World", 0, 100, 12, """D5q C#5e. B4s A4q. G4e F#4q E4q D4q. A4e B4q. B4e C#5q. C#5e D5h.
   D5e D5e C#5e B4e A4e A4e. G4s F#4e D5e D5e C#5e B4e A4e A4e. G4s F#4e F#4e F#4e F#4e F#4s G4s A4q. G4s F#4s E4e E4e E4e E4s F#4s G4q. F#4s E4s D4e D5q B4e A4e. G4s F#4e G4e F#4q E4q D4h."""),
 ("Twinkle Twinkle", 1, 120, 12, """C4q C4q G4q G4q A4q A4q G4h F4q F4q E4q E4q D4q D4q C4h
   G4q G4q F4q F4q E4q E4q D4h G4q G4q F4q F4q E4q E4q D4h
   C4q C4q G4q G4q A4q A4q G4h F4q F4q E4q E4q D4q D4q C4h"""),
 ("Mary Had a Little Lamb", 1, 130, 12, """E4q D4q C4q D4q E4q E4q E4h D4q D4q D4h E4q G4q G4h
   E4q D4q C4q D4q E4q E4q E4q E4q D4q D4q E4q D4q C4w"""),
 ("Happy Birthday", 1, 110, 12, """G4e. G4s A4q G4q C5q B4h G4e. G4s A4q G4q D5q C5h
   G4e. G4s G5q E5q C5q B4q A4q F5e. F5s E5q C5q D5q C5h."""),
 ("Row Row Row Your Boat", 1, 110, 12, """C4q. C4q. C4q D4e E4q. E4q D4e E4q F4e G4h.
   C5e C5e C5e G4e G4e G4e E4e E4e E4e C4e C4e C4e G4q F4e E4q D4e C4h."""),
 ("London Bridge", 1, 130, 12, """G4q. A4e G4q F4q E4q F4q G4h D4q E4q F4h E4q F4q G4h
   G4q. A4e G4q F4q E4q F4q G4h D4h G4q E4q C4h"""),
 ("Old MacDonald", 1, 150, 12, """G4q G4q G4q D4q E4q E4q D4h B4q B4q A4q A4q G4h Rq D4q
   G4q G4q G4q D4q E4q E4q D4h B4q B4q A4q A4q G4h"""),
 ("Ode to Joy", 2, 140, 12, """E4q E4q F4q G4q G4q F4q E4q D4q C4q C4q D4q E4q E4q. D4e D4h
   E4q E4q F4q G4q G4q F4q E4q D4q C4q C4q D4q E4q D4q. C4e C4h
   D4q D4q E4q C4q D4q E4e F4e E4q C4q D4q E4e F4e E4q D4q C4q D4q G3h
   E4q E4q F4q G4q G4q F4q E4q D4q C4q C4q D4q E4q D4q. C4e C4h"""),
 ("Fur Elise", 2, 72, 12, """E5s D#5s E5s D#5s E5s B4s D5s C5s A4e Rs C4s E4s A4s B4e Rs E4s G#4s B4s C5e Rs E4s
   E5s D#5s E5s D#5s E5s B4s D5s C5s A4e Rs C4s E4s A4s B4e Rs E4s C5s B4s A4q."""),
 ("Fifth Symphony", 2, 108, 12, """Re G4e G4e G4e D#4w Re F4e F4e F4e D4w
   Re G4e G4e G4e D#4e G#4e G#4e G#4e G4e D#5e D#5e D#5e C5h."""),
 ("Eine kleine Nachtmusik", 3, 130, 12, """G4q Re D4e G4q Re D4e G4e D4e G4e B4e D5q Rq
   C5q Re A4e C5q Re A4e C5e A4e F#4e A4e D4q Rq
   G4h G4e. B4s A4e G4e G4e F#4e F#4h A4e. C5s F#4e A4e G4e G4e"""),
 ("Hall of the Mountain King", 3, 150, 12, """B3e C#4e D4e E4e F#4e D4e F#4q F4e C#4e F4q E4e C4e E4q
   B3e C#4e D4e E4e F#4e D4e F#4e B4e A4e F#4e D4e F#4e A4h"""),
 ("Brahms' Lullaby", 3, 100, 12, """E4e E4e G4q. E4e E4e G4q. E4e G4e C5q B4q. A4e A4q G4q
   D4e E4e F4q D4q D4e E4e F4q. D4e F4e B4e A4e G4q B4q C5h."""),
 ("Korobeiniki", 3, 150, 0, """E5q B4e C5e D5q C5e B4e A4q A4e C5e E5q D5e C5e B4q. C5e D5q E5q C5q A4q A4h
   Re D5q F5e A5q G5e F5e E5q. C5e E5q D5e C5e B4q B4e C5e D5q E5q C5q A4q A4h"""),
 ("Greensleeves", 3, 120, 12, """A4e C5q D5e E5q. F5e E5q D5e B4q G4e A4e B4q C5e A4q A4e G#4e A4q B4e G#4q E4e
   A4e C5q D5e E5q. F5e E5q D5e B4q G4e A4e B4q C5e B4e A4e G#4q F#4e G#4q A4q. A4q."""),
 ("Village Morning", 4, 110, 0, """E5q G5e E5e C5q D5q F5e D5e B4q C5q E5e G5e C6q B5h Rq
   A5q F5e A5e C6q G5q E5e G5e C6q F5q D5e F5e B5e A5e G5h.
   C6q B5e A5e G5q F5q E5e D5e C5q D5e E5e F5q A5q G5h Rq E5q G5e E5e C5q D5q F5e A5e G5q F5e E5e D5q B4q C5h."""),
 ("Overworld Run", 4, 160, 0, """C5e C5e G4e C5e E5e G5e E5e C5e D5e F5e A5e F5e D5q B4q C5e E5e G5e C6e B5e G5e E5e G5e A5q. G5e E5h
   F5e A5e C6e A5e G5e E5e C5e E5e D5e F5e A5e F5e E5q C5q D5e E5e F5e G5e A5e B5e C6q G5q C6q C6h"""),
 ("Hero's March", 4, 120, 0, """G4e. G4s C5q C5e. D5s E5q E5e. E5s D5e C5e D5q G4q A4e. A4s D5q D5e. E5s F5q E5e. D5s C5e B4e C5h
   E5e. E5s G5q G5e. F5s E5q D5e. C5s D5e E5e D5q G4q C5e. D5s E5q F5e. G5s A5q G5e. F5s E5e D5e C5h"""),
 ("Save Point", 4, 80, 0, """C5e E5e G5e E5e C6q G5q A4e C5e E5e C5e A5q E5q F4e A4e C5e A4e F5q C5q G4e B4e D5e B4e G5h
   C5e E5e G5e E5e C6q G5q A4e C5e E5e C5e A5q G5q F5e E5e D5e C5e B4q D5q C5w"""),
 ("Boss Battle", 4, 170, 0, """A4e A4e C5e A4e D5e A4e D#5e D5e A4e A4e C5e A4e G5e F5e D#5e D5e
   A4e A4e C5e A4e D5e A4e D#5e D5e F5e E5e D#5e D5e C5e B4e A4q
   D5e D5e F5e D5e G5e D5e A5e G5e F5e E5e D5e C#5e D5h"""),
 ("Night Market", 4, 96, 0, """E5q. D5e C5q A4q G4q A4e C5e D5h E5q. G5e A5q G5e E5e D5h. Rq
   C5q. D5e E5q G5q A5q G5e E5e D5q C5q D5q E5e D5e C5q A4q C5w"""),
 ("Level Clear", 4, 150, 0, """G4s C5s E5s G5s C6e. G5s E5q C6q D6e. C6s B5e A5e G5q Rq C6e G5e E5e G5e C6h"""),
]
def parse(s, tr):
    out=[]
    for t in s.split():
        m=re.fullmatch(r'([A-GR])(#?)(\d?)([sqehw])(\.?)',t)
        if not m: sys.exit('bad token '+t)
        n,sh,o,d,dot=m.groups()
        dur=DUR[d]*(3 if dot else 2)//2
        midi=0 if n=='R' else 12*(int(o)+1)+NOTE[n]+(1 if sh else 0)+tr
        out.append((midi,dur))
    return out
data=[(name,cat,bpm,parse(notes,tr)) for name,cat,bpm,tr,notes in SONGS]
c=['// Generated by songs.py. Public-domain melodies plus original "Pixel" tunes. Each note: MIDI number (0 = rest), length in sixteenths.',
   'struct Song { const char *name; uint8_t cat; uint8_t bpm; uint16_t n; const uint8_t *notes; };',
   'const char *SONG_CATS[] = {%s};'%', '.join('"%s"'%x for x in CATS), '#define CAT_COUNT %d'%len(CATS)]
for i,(name,cat,bpm,ns) in enumerate(data):
    c.append('const uint8_t SONG%d[] PROGMEM = {%s};'%(i,','.join('%d,%d'%x for x in ns)))
c.append('const Song SONGS[] = {')
for i,(name,cat,bpm,ns) in enumerate(data): c.append('  {"%s", %d, %d, %d, SONG%d},'%(name.replace("'","\\'"),cat,bpm,len(ns),i))
c.append('};\n#define SONG_COUNT %d\n'%len(data))
open('songs.h','w').write('\n'.join(c))
js=[{'name':n,'cat':ct,'bpm':b,'notes':[x for p in ns for x in p]} for n,ct,b,ns in data]
open('songs.js','w').write('// Generated by songs.py. Same melodies as the charm.\nwindow.SONG_CATS = '+json.dumps(CATS)+';\nwindow.SONGS = '+json.dumps(js,separators=(',',':'))+';\n')
for n,ct,b,ns in data: print('%-28s %-16s %3d notes %5.1fs'%(n,CATS[ct],len(ns),sum(d for _,d in ns)*60/b/4))
