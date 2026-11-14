# Quest look art

All pixel art for the Quest look (skin 2) is drawn by these scripts; nothing is traced or copied.

- `pal.py`: the palette. Kind 1 = window glass (glows at night), 2 = lights and fire (never darkened), 3 = far scenery (gets sky haze).
- `draw.py`: a tiny indexed-pixel drawing kit (roofs, bricks, windows, trees ...).
- `world.py`: the looping village, one landmark per feature, 128 px apart.
- `bg.py`: far mountains, hills, and the out-of-focus foreground.
- `sprites.py`: the hero, the minstrel, icons, clouds.
- `questart.py`: writes `firmware/LarpCharm/questart.h` (palette, RLE layers, sprites, pixel fonts).
- `preview.py day|night`: composites a few locations to a PNG to check the art.

Fonts: put `Jersey15-Regular.ttf` and `Micro5-Regular.ttf` (Google Fonts, SIL Open Font Licence) in `fonts/` next to the scripts, then run `python3 questart.py ../../LarpCharm/questart.h`.
