#!/usr/bin/env python3
"""Images de publication (M5Burner, Reddit, posts) pour Silicon Bubbles.

La vignette du catalogue M5Burner est une boîte 4:3 réduite à ~170x135 px :
tout doit survivre à une réduction x7. D'où l'affiche : le nom en très
grand, la bulle du produit, une ligne de sous-titre — rien d'autre. Leçon
du catalogue (relevée sur Silicon Casino) : sur une étagère saturée,
l'image lisible gagne.

Tout sort des MÊMES sources que l'appareil : les écrans de
captures/screens, le GIF de captures/gif (mode --frames du simulateur).
Aucune maquette redessinée.

    pio run -e sim
    .pio/build/sim/program --screens captures/screens
    .pio/build/sim/program --frames captures/gif 60
    python3 scripts/make_store_images.py

Sortie : docs/m5burner/ (cover, mosaïque) et docs/images/loop.gif.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SHOTS = ROOT / "captures/screens"
GIF_SRC = ROOT / "captures/gif"
OUT = ROOT / "docs/m5burner"
IMG = ROOT / "docs/images"

# La palette du produit (docs/05), en RGB888.
INK900 = (10, 16, 27)
INK700 = (24, 39, 66)
PANEL = (25, 38, 51)
BLUE500 = (41, 127, 238)
BLUE400 = (77, 163, 255)
AMBER = (240, 165, 74)
WHITE = (255, 255, 255)
SLATE = (143, 163, 188)

W, H = 1200, 900  # 4:3, la boîte du catalogue


def shot(name):
    p = SHOTS / f"{name}.bmp"
    if not p.exists():
        sys.exit(f"capture manquante : {p} (lancer --screens d'abord)")
    return Image.open(p).convert("RGB")


def rounded(img, r):
    """Coins arrondis : les captures sont des écrans, pas des rectangles."""
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, img.size[0] - 1, img.size[1] - 1],
                                           radius=r, fill=255)
    out = Image.new("RGB", img.size, INK900)
    out.paste(img, (0, 0), mask)
    return out


def bubble(d, box, fill, tail_right):
    """La bulle du produit : rectangle arrondi + ergot. Le logo EST l'objet."""
    x0, y0, x1, y1 = box
    r = (y1 - y0) // 3
    d.rounded_rectangle(box, radius=r, fill=fill)
    if tail_right:
        d.polygon([(x1 - r, y1), (x1 + r // 2, y1), (x1 - 2, y1 - r)], fill=fill)
    else:
        d.polygon([(x0 + r, y1), (x0 - r // 2, y1), (x0 + 2, y1 - r)], fill=fill)


def make_cover():
    """L'affiche du catalogue : lisible réduite sept fois."""
    im = Image.new("RGB", (W, H), INK900)
    d = ImageDraw.Draw(im)
    try:
        from PIL import ImageFont
        f_big = ImageFont.truetype("/System/Library/Fonts/SFNSRounded.ttf", 150)
        f_sub = ImageFont.truetype("/System/Library/Fonts/SFNSMono.ttf", 42)
    except Exception:
        f_big = f_sub = None

    # Le titre d'abord, sur fond nu : rien ne doit mordre dessus — c'est la
    # seule chose qui survit à la réduction en vignette de catalogue.
    d.text((72, 60), "SILICON", font=f_big, fill=WHITE)
    d.text((72, 205), "BUBBLES", font=f_big, fill=BLUE400)

    # Dessous, une conversation en trois bulles : reçue, envoyée, et la
    # bulle « en train d'écrire » qui sert de marque au produit.
    bubble(d, (72, 430, 640, 560), INK700, False)
    bubble(d, (500, 590, 1128, 720), (30, 74, 140), True)

    bx, by, bw, bh = 72, 620, 330, 170
    bubble(d, (bx, by, bx + bw, by + bh), BLUE500, False)
    for i in range(3):
        cx = bx + bw // 4 + i * (bw // 4)
        cy = by + bh // 2
        rr = 26 if i == 1 else 20
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=WHITE)

    d.text((72, 830), "iMessage on your Cardputer", font=f_sub, fill=SLATE)
    d.text((866, 830), "M5Stack ADV", font=f_sub, fill=AMBER)

    im.save(OUT / "cover.png")
    im.resize((170, 135), Image.LANCZOS).save(OUT / "cover_thumb.png")
    print("cover.png + cover_thumb.png (test de lisibilité réduite)")


def make_mosaic():
    """Mosaïque 3x2 des écrans principaux — le post Reddit, en une image."""
    names = ["01-chats", "02-messages", "04-compose",
             "12-wifi-scan", "14-qr-join", "16-about"]
    k, gap, pad = 3, 24, 32
    tiles = [rounded(shot(n).resize((240 * k, 135 * k), Image.NEAREST), 10)
             for n in names]
    tw, th = tiles[0].size
    cols, rows = 3, 2
    im = Image.new("RGB", (pad * 2 + tw * cols + gap * (cols - 1),
                           pad * 2 + th * rows + gap * (rows - 1)), INK900)
    for i, t in enumerate(tiles):
        x = pad + (i % cols) * (tw + gap)
        y = pad + (i // cols) * (th + gap)
        im.paste(t, (x, y))
    im.save(OUT / "mosaic.png")
    print(f"mosaic.png ({im.size[0]}x{im.size[1]})")


def make_gif():
    """La boucle produit : on tape, on envoie, le coeur arrive, la réponse."""
    frames = sorted(GIF_SRC.glob("f*.bmp"))
    if not frames:
        sys.exit("aucune image : lancer --frames captures/gif 60")
    imgs = [Image.open(f).convert("RGB").resize((480, 270), Image.NEAREST)
            for f in frames]
    # Palette adaptative : le GIF reste sous ~1 Mo, lisible sur Reddit.
    imgs = [im.quantize(colors=64, dither=Image.NONE) for im in imgs]
    imgs[0].save(IMG / "loop.gif", save_all=True, append_images=imgs[1:],
                 duration=90, loop=0, optimize=True)
    size = (IMG / "loop.gif").stat().st_size
    print(f"loop.gif ({len(imgs)} images, {size // 1024} Ko)")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    IMG.mkdir(parents=True, exist_ok=True)
    make_cover()
    make_mosaic()
    make_gif()


if __name__ == "__main__":
    main()
