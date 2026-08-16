#!/usr/bin/env python3
# Générateur des glyphes émoji — même système que design/tools/gen.py de
# Geek Casino : palette indexée RGB565 (0 = transparent), glyphes en
# tableaux d'indices, dessinés procéduralement. Émet :
#   include/emoji_art.h   (consommé par le firmware)
#   design/emoji/emojis.html (carte de prévisualisation, échelle x10)
# Ne pas éditer l'en-tête à la main : ce fichier est la source de vérité.
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
PX = 12

# ---------------------------------------------------------------- palette ---
PAL = [
    (None,      "transparent"),
    ("#8A6410", "OUT  contour visage/mains"),
    ("#FFCB28", "FACE jaune émoji"),
    ("#E8A400", "SHAD ombre jaune"),
    ("#201808", "BLK  traits"),
    ("#FFFFFF", "WHT"),
    ("#E83C28", "RED"),
    ("#A01810", "DRED bouche ouverte, ombre rouge"),
    ("#FF86B0", "PNK  joues"),
    ("#FF9518", "ORG"),
    ("#38C060", "GRN"),
    ("#3E8EF8", "BLU  larmes"),
    ("#9C58F0", "PUR"),
    ("#7A4A20", "BRN"),
    ("#96A2B0", "GRY  placeholder"),
    ("#FFD848", "GLD  or, étoiles"),
    ("#3C4450", "DKG  cœur noir, verres"),
]
T, OUT, FACE, SHAD, BLK, WHT, RED, DRED, PNK, ORG, GRN, BLU, PUR, BRN, GRY, GLD, DKG = range(17)


def rgb565(hexs):
    r, g, b = int(hexs[1:3], 16), int(hexs[3:5], 16), int(hexs[5:7], 16)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def back565(v):
    r = ((v >> 11) & 0x1F) * 255 // 31
    g = ((v >> 5) & 0x3F) * 255 // 63
    b = (v & 0x1F) * 255 // 31
    return f"#{r:02X}{g:02X}{b:02X}"


# ------------------------------------------------------------------ dessin ---
def empty():
    return [[0] * PX for _ in range(PX)]


def px(g, x, y, c):
    if 0 <= x < PX and 0 <= y < PX:
        g[y][x] = c


def hl(g, x0, x1, y, c):
    for x in range(x0, x1 + 1):
        px(g, x, y, c)


def vl(g, x, y0, y1, c):
    for y in range(y0, y1 + 1):
        px(g, x, y, c)


def rect(g, x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        hl(g, x0, x1, y, c)


def disc(g, cx, cy, r, c):
    for y in range(PX):
        for x in range(PX):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                g[y][x] = c


def rot180(g):
    return [row[::-1] for row in g[::-1]]


def flip_v(g):
    return g[::-1]


def face(fill=FACE, shade=SHAD, outline=OUT):
    g = empty()
    disc(g, 5.5, 5.5, 5.8, outline)
    disc(g, 5.5, 5.5, 4.9, fill)
    # ombre en bas pour le volume
    for x in range(2, 10):
        for y in (9, 10):
            if g[y][x] == fill and (x - 5.5) ** 2 + (y - 5.5) ** 2 > 3.4 ** 2:
                g[y][x] = shade
    return g


def eyes_dot(g):
    vl(g, 3, 3, 4, BLK)
    vl(g, 8, 3, 4, BLK)


def eyes_happy(g):  # ^ ^
    px(g, 2, 4, BLK); px(g, 3, 3, BLK); px(g, 4, 4, BLK)
    px(g, 7, 4, BLK); px(g, 8, 3, BLK); px(g, 9, 4, BLK)


def eyes_closed(g):
    hl(g, 2, 4, 4, BLK)
    hl(g, 7, 9, 4, BLK)


def mouth_smile(g):
    px(g, 3, 7, BLK); hl(g, 4, 7, 8, BLK); px(g, 8, 7, BLK)


def mouth_grin(g):
    hl(g, 3, 8, 7, BLK)
    hl(g, 3, 8, 9, BLK)
    px(g, 2, 7, BLK); px(g, 9, 7, BLK)
    hl(g, 3, 8, 8, WHT)
    px(g, 3, 8, DRED); px(g, 8, 8, DRED)


def mouth_frown(g):
    hl(g, 4, 7, 7, BLK)
    px(g, 3, 8, BLK); px(g, 8, 8, BLK)


def mouth_flat(g):
    hl(g, 4, 7, 8, BLK)


GLYPHS = {}   # nom -> grille
ORDER = []    # ordre stable


def emit(name, g):
    GLYPHS[name] = g
    ORDER.append(name)


# --- visages ---
g = face(); eyes_happy(g); mouth_grin(g)
px(g, 1, 6, BLU); px(g, 1, 7, BLU); px(g, 10, 6, BLU); px(g, 10, 7, BLU)  # larmes de joie
emit("joy", g)

g = face(); eyes_dot(g); mouth_grin(g)
emit("grin", g)

g = face(); eyes_happy(g); mouth_smile(g)
px(g, 1, 6, PNK); px(g, 2, 6, PNK); px(g, 9, 6, PNK); px(g, 10, 6, PNK)  # joues
emit("smile", g)

g = face(); eyes_dot(g); mouth_grin(g)
px(g, 9, 1, BLU); px(g, 10, 1, BLU); px(g, 10, 2, BLU)  # goutte
emit("sweat", g)

g = face(); vl(g, 3, 3, 4, BLK); hl(g, 7, 9, 4, BLK); mouth_smile(g)
emit("wink", g)

def mini_heart(g, x, y, c):  # cœur 3x3
    px(g, x, y, c); px(g, x + 2, y, c)
    hl(g, x, x + 2, y + 1, c)
    px(g, x + 1, y + 2, c)

g = face(); mini_heart(g, 2, 2, RED); mini_heart(g, 7, 2, RED); mouth_smile(g)
emit("heartEyes", g)

g = face(); vl(g, 3, 3, 4, BLK); hl(g, 7, 9, 4, BLK)
px(g, 4, 7, BLK); px(g, 5, 8, BLK); px(g, 4, 9, BLK)  # bouche en bisou
mini_heart(g, 8, 6, RED)
emit("kiss", g)

g = face()
rect(g, 1, 3, 10, 4, BLK)   # lunettes
px(g, 2, 3, DKG); px(g, 8, 3, DKG)  # reflets
px(g, 0, 3, BLK); px(g, 11, 3, BLK)  # branches
mouth_smile(g)
emit("cool", g)

g = face(); hl(g, 2, 4, 2, BLK); vl(g, 3, 4, 4, BLK); vl(g, 8, 3, 4, BLK)
px(g, 4, 8, BLK); px(g, 5, 8, BLK); px(g, 6, 7, BLK)  # bouche dubitative
rect(g, 6, 9, 8, 10, SHAD); px(g, 6, 9, OUT)  # main au menton
emit("think", g)

g = face()
for (ex) in (2, 7):
    rect(g, ex, 3, ex + 2, 5, BLK)
    px(g, ex, 3, WHT); px(g, ex + 1, 4, WHT)  # gros yeux brillants
hl(g, 2, 4, 1, BLK); hl(g, 7, 9, 1, BLK)  # sourcils hauts
px(g, 5, 8, BLK); px(g, 6, 8, BLK)
px(g, 1, 6, PNK); px(g, 10, 6, PNK)
emit("plead", g)

g = face(); eyes_dot(g); mouth_frown(g)
px(g, 2, 5, BLU); px(g, 2, 6, BLU); px(g, 2, 7, BLU)  # larme
emit("cry", g)

g = face(); eyes_closed(g)
rect(g, 4, 7, 7, 9, DRED); hl(g, 4, 7, 7, BLK); hl(g, 4, 7, 10, BLK)  # bouche grande ouverte — wait outline
vl(g, 2, 5, 9, BLU); vl(g, 9, 5, 9, BLU)  # torrents
emit("sob", g)

g = face(RED, DRED)
px(g, 2, 2, BLK); px(g, 3, 3, BLK); px(g, 9, 2, BLK); px(g, 8, 3, BLK)  # sourcils en V
vl(g, 3, 4, 5, BLK); vl(g, 8, 4, 5, BLK)
mouth_frown(g)
emit("angry", g)

g = face()
for ex in (2, 7):
    rect(g, ex, 3, ex + 2, 5, WHT)
    px(g, ex + 1, 4, BLK)
rect(g, 5, 7, 6, 9, BLK)  # bouche ouverte de stupeur
emit("shock", g)

g = face()
for ex in (2, 7):
    rect(g, ex, 3, ex + 2, 4, WHT)
    px(g, ex + 1, 3, BLK)  # pupilles en l'air
mouth_flat(g)
emit("eyeroll", g)

g = face(); eyes_closed(g); px(g, 5, 8, BLK)
hl(g, 8, 10, 0, BLU); px(g, 9, 1, BLU); hl(g, 8, 10, 2, BLU)  # Z
emit("sleep", g)

emit("upside", rot180(GLYPHS["smile"]))

g = face(); eyes_happy(g); mouth_smile(g)
hl(g, 3, 8, 0, GLD); px(g, 2, 0, GLD); px(g, 9, 0, GLD)  # auréole
emit("halo", g)

# --- cœurs ---
HEART_ROWS = [
    (2, [(2, 4), (7, 9)]),
    (3, [(1, 5), (6, 10)]),
    (4, [(1, 10)]),
    (5, [(1, 10)]),
    (6, [(2, 9)]),
    (7, [(3, 8)]),
    (8, [(4, 7)]),
    (9, [(5, 6)]),
]

def heart(c, shade=None):
    g = empty()
    for y, spans in HEART_ROWS:
        for x0, x1 in spans:
            hl(g, x0, x1, y, c)
    if shade:
        px(g, 2, 3, shade); px(g, 2, 4, shade); px(g, 3, 3, shade)  # reflet
    return g

emit("heartRed", heart(RED, DRED))
emit("heartOrange", heart(ORG))
emit("heartYellow", heart(GLD))
emit("heartGreen", heart(GRN))
emit("heartBlue", heart(BLU))
emit("heartPurple", heart(PUR))
emit("heartBlack", heart(DKG))
g = heart(WHT)
for y, spans in HEART_ROWS:  # contour gris pour la lisibilité sur fond sombre
    for x0, x1 in spans:
        px(g, x0, y, GRY); px(g, x1, y, GRY)
emit("heartWhite", g)
g = heart(RED, DRED)
for x, y in [(5, 2), (6, 3), (5, 4), (6, 5), (5, 6), (6, 7), (5, 8)]:
    px(g, x, y, T)  # fissure
emit("heartBroken", g)

# --- mains ---
g = empty()
rect(g, 3, 5, 9, 10, OUT); rect(g, 4, 6, 8, 9, FACE)   # poing
rect(g, 2, 1, 4, 5, OUT); vl(g, 3, 2, 5, FACE)         # pouce levé
hl(g, 5, 8, 7, SHAD)
emit("thumbsUp", g)
emit("thumbsDown", flip_v(GLYPHS["thumbsUp"]))

g = empty()  # mains jointes : deux paumes l'une contre l'autre
ROWS = [(1,5,6),(2,5,6),(3,4,7),(4,4,7),(5,4,7),(6,3,8),(7,3,8),(8,3,8),(9,4,7),(10,5,6)]
for y,x0,x1 in ROWS: hl(g, x0, x1, y, OUT)
for y,x0,x1 in ROWS[1:-1]: hl(g, x0+1, x1-1, y, FACE)
vl(g, 5, 2, 9, SHAD)  # la ligne entre les deux mains
px(g, 1, 1, GLD); px(g, 10, 1, GLD)
emit("pray", g)

g = empty()  # ok : cercle pouce-index net + trois doigts levés
disc(g, 3.5, 8, 3.2, OUT)
disc(g, 3.5, 8, 2.2, FACE)
rect(g, 3, 7, 4, 9, T)  # le trou du cercle
vl(g, 6, 2, 6, FACE); vl(g, 8, 1, 6, FACE); vl(g, 10, 2, 7, FACE)
vl(g, 7, 3, 6, OUT); vl(g, 9, 2, 6, OUT)
px(g, 6, 7, FACE); px(g, 7, 7, FACE); px(g, 8, 7, FACE)  # jonction paume
emit("okHand", g)

g = empty()  # biceps : bras fléchi, poing à gauche
disc(g, 8, 3.8, 3.3, OUT); disc(g, 8, 3.8, 2.3, FACE)   # le biceps qui gonfle
rect(g, 0, 6, 9, 10, OUT); rect(g, 1, 7, 8, 9, FACE)    # l'avant-bras
disc(g, 1.8, 8, 1.9, OUT); disc(g, 1.8, 8, 1.1, FACE)   # le poing
px(g, 7, 3, WHT)  # reflet
hl(g, 3, 6, 9, SHAD)
emit("muscle", g)

g = empty()  # applaudissements : deux mains inclinées + éclats
rect(g, 2, 4, 4, 9, OUT); rect(g, 3, 5, 3, 8, FACE)
rect(g, 7, 4, 9, 9, OUT); rect(g, 8, 5, 8, 8, FACE)
px(g, 5, 2, GLD); px(g, 6, 2, GLD)
px(g, 1, 1, GLD); px(g, 10, 1, GLD); px(g, 5, 0, GLD)
emit("clap", g)

# --- divers ---
g = empty()  # flamme
disc(g, 5.5, 7.5, 3.9, RED)
disc(g, 5.5, 8.2, 2.6, ORG)
disc(g, 5.5, 9, 1.4, GLD)
vl(g, 5, 1, 3, RED); px(g, 6, 2, RED); px(g, 4, 3, RED); px(g, 7, 3, RED)
px(g, 3, 4, RED); px(g, 8, 4, RED)
emit("fire", g)

def spark(g, cx, cy, r, c):
    hl(g, cx - r, cx + r, cy, c)
    vl(g, cx, cy - r, cy + r, c)

g = empty()
spark(g, 3, 3, 2, GLD); spark(g, 8, 7, 2, GLD); spark(g, 9, 2, 1, WHT); spark(g, 2, 9, 1, WHT)
emit("sparkles", g)

g = empty()  # cône de fête : pointe en bas à gauche, ouverture en haut-droite
for i in range(7):
    y = 10 - i
    x0 = max(0, 6 - i)
    hl(g, x0, 6, y, ORG if i % 2 else DRED)  # rayures du cône
# redresse la diagonale : efface le coin au-dessus de la pointe
for y in range(4, 10):
    for x in range(0, 12):
        if x < 6 - (10 - y) - 0: px(g, x, y, T)
px(g, 0, 11, DRED); px(g, 1, 11, DRED)  # pointe
# confettis qui jaillissent
px(g, 8, 2, GRN); px(g, 10, 3, BLU); px(g, 7, 1, RED)
px(g, 9, 5, PUR); px(g, 6, 0, GLD); px(g, 11, 1, RED); px(g, 10, 6, GLD)
emit("party", g)

STAR = [
    (1, [(5, 6)]), (2, [(5, 6)]), (3, [(4, 7)]),
    (4, [(0, 11)]), (5, [(1, 10)]), (6, [(3, 8)]),
    (7, [(2, 9)]), (8, [(2, 4), (7, 9)]), (9, [(1, 2), (9, 10)]),
]
g = empty()
for y, spans in STAR:
    for x0, x1 in spans:
        hl(g, x0, x1, y, GLD)
emit("star", g)

g = empty()  # crotte souriante
disc(g, 5.5, 8.5, 3.4, BRN)
disc(g, 5.5, 5.5, 2.4, BRN)
disc(g, 5.5, 3, 1.3, BRN)
px(g, 4, 7, WHT); px(g, 7, 7, WHT); px(g, 4, 8, BLK); px(g, 7, 8, BLK)
hl(g, 5, 6, 9, BLK)
emit("poop", g)

g = empty()  # crâne
disc(g, 5.5, 5, 4.4, WHT)
rect(g, 4, 9, 7, 10, WHT)
rect(g, 3, 4, 4, 5, BLK); rect(g, 7, 4, 8, 5, BLK)
px(g, 5, 7, BLK); px(g, 6, 7, BLK)
vl(g, 5, 9, 10, GRY); vl(g, 7, 9, 10, GRY)
emit("skull", g)

g = empty()  # rose
disc(g, 5.5, 3.5, 2.9, RED)
px(g, 5, 3, DRED); px(g, 6, 4, DRED); px(g, 4, 4, DRED)
vl(g, 5, 7, 11, GRN)
px(g, 3, 8, GRN); px(g, 4, 8, GRN); px(g, 7, 9, GRN); px(g, 8, 9, GRN)
emit("rose", g)

g = empty()  # 100 souligné
vl(g, 1, 2, 7, RED); px(g, 0, 3, RED)
for x0 in (5, 9):
    vl(g, x0 - 1, 3, 6, RED); vl(g, x0 + 1, 3, 6, RED)
    px(g, x0, 2, RED); px(g, x0, 7, RED)
hl(g, 0, 10, 9, RED)
hl(g, 1, 11, 10, RED)
emit("hundred", g)

g = empty()  # inconnu : petit carré arrondi gris + « ? »
rect(g, 1, 1, 10, 10, DKG)
for x, y in [(1, 1), (10, 1), (1, 10), (10, 10)]:
    px(g, x, y, T)
hl(g, 4, 7, 3, GRY); px(g, 8, 4, GRY); px(g, 7, 5, GRY); px(g, 6, 6, GRY)
px(g, 6, 8, GRY)
emit("unknown", g)

# ------------------------------------------------- codepoints -> glyphes ---
MAP = {
    0x2728: "sparkles", 0x2763: "heartRed", 0x2764: "heartRed", 0x2B50: "star",
    0x263A: "smile",
    0x1F31F: "star", 0x1F339: "rose", 0x1F382: "party", 0x1F389: "party",
    0x1F38A: "party", 0x1F44C: "okHand", 0x1F44D: "thumbsUp", 0x1F44E: "thumbsDown",
    0x1F44F: "clap", 0x1F480: "skull", 0x1F490: "rose",
    0x1F493: "heartRed", 0x1F494: "heartBroken", 0x1F495: "heartRed",
    0x1F496: "heartRed", 0x1F497: "heartRed", 0x1F499: "heartBlue",
    0x1F49A: "heartGreen", 0x1F49B: "heartYellow", 0x1F49C: "heartPurple",
    0x1F49D: "heartRed", 0x1F49E: "heartRed", 0x1F4AA: "muscle",
    0x1F4A9: "poop", 0x1F4AF: "hundred",
    0x1F525: "fire", 0x1F5A4: "heartBlack",
    0x1F600: "grin", 0x1F601: "grin", 0x1F602: "joy", 0x1F603: "grin",
    0x1F604: "grin", 0x1F605: "sweat", 0x1F606: "grin", 0x1F607: "halo",
    0x1F609: "wink", 0x1F60A: "smile", 0x1F60D: "heartEyes", 0x1F60E: "cool",
    0x1F60F: "wink", 0x1F614: "plead", 0x1F618: "kiss", 0x1F61A: "kiss",
    0x1F620: "angry", 0x1F621: "angry", 0x1F622: "cry", 0x1F62A: "sleep",
    0x1F62D: "sob", 0x1F62E: "shock", 0x1F631: "shock", 0x1F633: "shock",
    0x1F634: "sleep", 0x1F642: "smile", 0x1F643: "upside", 0x1F644: "eyeroll",
    0x1F64F: "pray", 0x1F90D: "heartWhite", 0x1F914: "think", 0x1F923: "joy",
    0x1F929: "heartEyes", 0x1F970: "heartEyes", 0x1F972: "cry", 0x1F97A: "plead",
}

# ------------------------------------------------------------------ sortie ---
def gen_header():
    lines = []
    lines.append("// GÉNÉRÉ par design/tools/gen_emoji.py — ne pas éditer à la main.")
    lines.append("// Même système que les glyphes de Geek Casino : palette indexée")
    lines.append("// RGB565 (0 = transparent), glyphes en indices de palette.")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append(f"constexpr int kEmojiPx = {PX};")
    lines.append(f"constexpr int kEmojiCount = {len(ORDER)};")
    lines.append("")
    lines.append(f"constexpr uint16_t kEmojiPalette[{len(PAL)}] = {{")
    for hexs, com in PAL:
        v = 0 if hexs is None else rgb565(hexs)
        lines.append(f"    0x{v:04X},  // {com}")
    lines.append("};")
    lines.append("")
    lines.append(f"constexpr uint8_t kEmojiArt[kEmojiCount][kEmojiPx * kEmojiPx] = {{")
    for name in ORDER:
        g = GLYPHS[name]
        lines.append(f"    {{  // {ORDER.index(name)} {name}")
        for row in g:
            lines.append("        " + ",".join(str(v) for v in row) + ",")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("// Codepoint -> index de glyphe, trié pour recherche dichotomique.")
    lines.append("struct EmojiMapEntry { uint32_t cp; uint8_t glyph; };")
    entries = sorted((cp, ORDER.index(n)) for cp, n in MAP.items())
    lines.append(f"constexpr EmojiMapEntry kEmojiMap[{len(entries)}] = {{")
    for cp, gi in entries:
        lines.append(f"    {{0x{cp:05X}, {gi}}},  // {ORDER[gi]}")
    lines.append("};")
    lines.append(f"constexpr int kEmojiMapCount = {len(entries)};")
    lines.append(f"constexpr uint8_t kEmojiUnknown = {ORDER.index('unknown')};")
    lines.append("")
    (ROOT / "include" / "emoji_art.h").write_text("\n".join(lines))
    print("emoji_art.h :", len(ORDER), "glyphes,", len(entries), "codepoints,",
          len(ORDER) * PX * PX, "octets d'art")


def gen_preview():
    style = (ROOT / "design" / "_style.html").read_text()
    cells = []
    for name in ORDER:
        g = GLYPHS[name]
        rects = []
        for y in range(PX):
            for x in range(PX):
                v = g[y][x]
                if v:
                    hexs = back565(rgb565(PAL[v][0]))
                    rects.append(f'<rect x="{x}" y="{y}" width="1" height="1" fill="{hexs}"/>')
        cps = sorted(cp for cp, n in MAP.items() if n == name)
        label = "".join(chr(c) for c in cps[:3]) or "—"
        cells.append(
            f'<div class="tile" style="padding:10px;text-align:center">'
            f'<div style="background:#182441;border-radius:8px;padding:8px;display:inline-block">'
            f'<svg viewBox="0 0 {PX} {PX}" width="96" height="96" shape-rendering="crispEdges">{"".join(rects)}</svg>'
            f'</div><div class="nm" style="margin-top:6px">{name}</div>'
            f'<div class="rl">{label}</div></div>'
        )
    body = (
        '<div class="warn"><b>Rendu au pixel près</b> : chaque vignette est le glyphe '
        f'{PX} × {PX} exact que dessine le firmware, sur le fond des bulles reçues.</div>'
        f'<h2>{len(ORDER)} glyphes</h2><div class="grid g4">{"".join(cells)}</div>'
        "<h2>Système</h2><p>Même mécanique que les symboles de Geek Casino : un "
        "générateur Python est la source de vérité, l'en-tête C++ et cette carte en "
        "sortent ensemble. Palette indexée RGB565, 0 = transparent, "
        f"{len(ORDER) * PX * PX} octets d'art en flash. Les variantes fusionnent "
        "(😀😃😄 → un glyphe, tous les cœurs partagent une forme) ; les sélecteurs "
        "de variation, tons de peau et séquences ZWJ sont absorbés ; tout émoji "
        "inconnu devient le glyphe « ? » plutôt qu'un carré vide.</p>"
    )
    page = (
        '<!-- @dsCard group="Fondations" name="Émojis" '
        f'subtitle="{len(ORDER)} glyphes pixel, générés" width="760" -->\n'
        "<meta charset=\"utf-8\">\n<title>Émojis — BlueBubbles Cardputer</title>\n"
        + style + '\n<div class="wrap">\n<h1>Émojis</h1>\n'
        "<p class=\"lede\">Les émojis les plus courants, dessinés au pixel dans la "
        "hauteur de ligne du texte — la police efont n'en contient aucun.</p>\n"
        + body + "\n</div>\n"
    )
    out = ROOT / "design" / "emoji"
    out.mkdir(exist_ok=True)
    (out / "emojis.html").write_text(page)
    print("emojis.html :", len(page), "octets")


gen_header()
gen_preview()
