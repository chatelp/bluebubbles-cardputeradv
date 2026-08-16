#!/usr/bin/env python3
"""Images du README, régénérées depuis les captures du simulateur.

Doctrine (reprise de Silicon Casino, elle-même de Daoa Mini) : chaque
image du README sort du simulateur — jamais d'une maquette. La chaîne :

    pio run -e sim
    .pio/build/sim/program --screens captures/screens
    python3 scripts/readme_images.py

`captures/` n'est pas versionné ; `docs/images/` l'est. Ce script est la
seule passerelle entre les deux, pour que les images publiées soient
toujours reconstruisibles.

Pur Python (BMP 24 bits → PNG via zlib) : aucune dépendance à installer.
"""
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "captures", "screens")
DST = os.path.join(ROOT, "docs", "images")

# Écrans publiés. Échelle 2 : net sur GitHub sans peser.
SHOTS = [
    ("01-chats", 2), ("02-messages", 2), ("03-messages-scrolled", 2),
    ("04-compose", 2), ("05-info", 2), ("06-settings", 2),
    ("07-calibrating-modal", 2), ("08-calibrating-initial", 2),
    ("09-setup", 2), ("10-splash-sync", 2), ("11-splash-connect", 2),
    ("12-wifi-scan", 2), ("13-text-input", 2), ("14-qr-join", 2),
    ("15-qr-portal", 2), ("16-about", 2),
]

# Le héros : quatre écrans, deux par deux — la boucle produit d'un coup
# d'œil (liste, conversation, composition, réglages).
HERO = [["01-chats", "02-messages"], ["04-compose", "06-settings"]]
HERO_GAP = 6
BG = (10, 16, 27)  # C_INK900, le fond du produit


def read_bmp(path):
    raw = open(path, "rb").read()
    off = struct.unpack_from("<I", raw, 10)[0]
    w, h = struct.unpack_from("<ii", raw, 18)
    rowb = ((w * 3 + 3) // 4) * 4
    px = []
    for y in range(h - 1, -1, -1):
        base = off + y * rowb
        row = []
        for x in range(w):
            b, g, r = raw[base + x * 3: base + x * 3 + 3]
            row.append((r, g, b))
        px.append(row)
    return px


def scale(px, k):
    if k == 1:
        return px
    out = []
    for row in px:
        big = []
        for p in row:
            big.extend([p] * k)
        out.extend([big] * k)
    return out


def write_png(px, path):
    h, w = len(px), len(px[0])
    raw = bytearray()
    for row in px:
        raw.append(0)  # filtre None : les aplats compressent déjà très bien
        for r, g, b in row:
            raw += bytes((r, g, b))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           chunk(b"IEND", b""))
    open(path, "wb").write(png)


def compose(grid, k):
    """Grille d'écrans mis à l'échelle, séparés par un filet de fond."""
    tiles = [[scale(read_bmp(os.path.join(SRC, n + ".bmp")), k) for n in row]
             for row in grid]
    th, tw = len(tiles[0][0]), len(tiles[0][0][0])
    gap = HERO_GAP * k // 2
    W = tw * len(tiles[0]) + gap * (len(tiles[0]) - 1)
    H = th * len(tiles) + gap * (len(tiles) - 1)
    out = [[BG] * W for _ in range(H)]
    for r, row in enumerate(tiles):
        for c, t in enumerate(row):
            oy, ox = r * (th + gap), c * (tw + gap)
            for y in range(th):
                out[oy + y][ox:ox + tw] = t[y]
    return out


def main():
    os.makedirs(DST, exist_ok=True)
    made = 0
    for name, k in SHOTS:
        src = os.path.join(SRC, name + ".bmp")
        if not os.path.exists(src):
            print("absent (ignoré) :", name)
            continue
        write_png(scale(read_bmp(src), k), os.path.join(DST, name + ".png"))
        made += 1
    write_png(compose(HERO, 2), os.path.join(DST, "hero.png"))
    print(f"{made} écrans + hero.png → docs/images/")


if __name__ == "__main__":
    main()
