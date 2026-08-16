#!/usr/bin/env python3
# Génère les cartes du design system BlueBubbles Cardputer.
# Les maquettes sont dessinées à l'échelle réelle (240 x 135) puis agrandies
# par CSS : ce qui est dessiné ici est ce que l'écran affiche, au pixel près.
import os, pathlib

ROOT = pathlib.Path(__file__).parent
STYLE = (ROOT / "_style.html").read_text()

# ---------------------------------------------------------------- palette ---
# (nom, hex source, hex réellement affiché après quantification RGB565, rôle)
PAL = [
    ("ink900",   "0x0883", "#0A101B", "#081018", "Fond de l'application — nuit bleutée"),
    ("ink800",   "0x10C5", "#111A29", "#101829", "Barre supérieure, barre d'aide"),
    ("ink700",   "0x1928", "#1B2740", "#182441", "Bulle reçue, surface surélevée"),
    ("ink600",   "0x21A9", "#25344F", "#20344A", "Ligne sélectionnée dans la liste"),
    ("ink500",   "0x3A4D", "#3A4B69", "#394868", "Séparateur, bordure sourde"),
    ("slate300", "0x8D17", "#8FA3BC", "#8BA1BD", "Texte secondaire, horodatage"),
    ("slate200", "0xBE3B", "#B9C7D8", "#BDC6DE", "Aperçu de conversation"),
    ("white",    "0xFFFF", "#FFFFFF", "#FFFFFF", "Texte des messages reçus"),
    ("blue500",  "0x2BFD", "#2F7FEE", "#297DEE", "Bulle envoyée — le bleu iMessage"),
    ("blue400",  "0x4D1F", "#4DA3FF", "#4AA1FF", "Accent : titres, sélection, focus"),
    ("blue300",  "0x8E3F", "#8CC6FF", "#8BC6FF", "Méta sur bulle envoyée"),
    ("green400", "0x3631", "#31C48D", "#31C68B", "Serveur OK, envoi confirmé"),
    ("amber400", "0xFD84", "#FFB020", "#FFB220", "Pastille non lu, avertissement"),
    ("red400",   "0xFACB", "#FF5A5F", "#FF595A", "Erreur, batterie faible"),
]
C = {n: shown for n, _, _, shown, _ in PAL}

W, H = 240, 135
BAR, HINT = 16, 13
GLYPH = 6          # largeur d'un caractère ASCII en efontJA_12
LINE = 13          # interligne
BUB_PAD_X, BUB_PAD_Y = 4, 3
BUB_R = 5
BUB_MAXW = 176     # 73 % de l'écran
MONO = 'font-family="ui-monospace,Menlo,monospace"'


def esc(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def txt(x, y, s, fill, size=11, anchor="start", weight="400", op=1.0):
    return (f'<text x="{x}" y="{y}" fill="{fill}" font-size="{size}" '
            f'text-anchor="{anchor}" font-weight="{weight}" opacity="{op}" '
            f'font-family="ui-sans-serif,Helvetica,Arial" '
            f'style="letter-spacing:.2px">{esc(s)}</text>')


def wrap(s, maxpx):
    """Découpe comme le firmware : aux espaces, largeur en pixels."""
    out, line = [], ""
    for word in s.split(" "):
        cand = (line + " " + word).strip()
        if len(cand) * GLYPH <= maxpx:
            line = cand
        else:
            if line:
                out.append(line)
            line = word
    if line:
        out.append(line)
    return out or [""]


def frame(inner, bg=None):
    return (f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg" '
            f'shape-rendering="crispEdges">'
            f'<rect width="{W}" height="{H}" fill="{bg or C["ink900"]}"/>{inner}</svg>')


def topbar(title, right="87%", sync=None):
    s = f'<rect width="{W}" height="{BAR}" fill="{C["ink800"]}"/>'
    s += txt(5, 11, title, C["blue400"], 11, weight="600")
    if sync:
        s += txt(W - 5 - len(right) * 6 - 8, 11, sync, C["slate300"], 9)
    s += txt(W - 5, 11, right, C["slate300"], 10, anchor="end")
    return s


def hintbar(hint):
    y = H - HINT
    s = f'<rect y="{y}" width="{W}" height="{HINT}" fill="{C["ink800"]}"/>'
    s += txt(5, y + 9, hint, C["slate300"], 9)
    return s


def bubble(x, y, lines, sent, tail=True):
    """Une bulle. x = bord gauche (reçue) ou bord DROIT (envoyée)."""
    tw = max(len(l) for l in lines) * GLYPH
    w = tw + BUB_PAD_X * 2
    h = len(lines) * LINE + BUB_PAD_Y * 2
    bx = x if not sent else x - w
    fill = C["blue500"] if sent else C["ink700"]
    fg = "#FFFFFF" if sent else C["white"]
    s = f'<rect x="{bx}" y="{y}" width="{w}" height="{h}" rx="{BUB_R}" fill="{fill}"/>'
    if tail:
        # Ergot de 3 px sur le coin bas extérieur — c'est lui qui fait « bulle ».
        if sent:
            s += (f'<path d="M{bx + w - BUB_R} {y + h} L{bx + w + 3} {y + h} '
                  f'L{bx + w - 1} {y + h - 5} Z" fill="{fill}"/>')
        else:
            s += (f'<path d="M{bx + BUB_R} {y + h} L{bx - 3} {y + h} '
                  f'L{bx + 1} {y + h - 5} Z" fill="{fill}"/>')
    for i, l in enumerate(lines):
        s += txt(bx + BUB_PAD_X, y + BUB_PAD_Y + LINE * i + 10, l, fg, 11)
    return s, h, w


def sep(y, label):
    """Séparateur temporel centré, à la manière d'iMessage."""
    return txt(W // 2, y + 8, label, C["slate300"], 9, anchor="middle")


def page(title, lede, body, card_group, card_name, card_sub, width=760):
    return (f'<!-- @dsCard group="{card_group}" name="{card_name}" '
            f'subtitle="{card_sub}" width="{width}" -->\n'
            f'<meta charset="utf-8">\n<title>{title} — BlueBubbles Cardputer</title>\n'
            f'{STYLE}\n<div class="wrap">\n<h1>{title}</h1>\n'
            f'<p class="lede">{lede}</p>\n{body}\n</div>\n')


def dev(svg, caption, x2=False):
    cls = "dev x2" if x2 else "dev"
    return f'<div><div class="{cls}">{svg}</div><div class="cap">{caption}</div></div>'


# ------------------------------------------------------------ conversation ---
def screen_conversation():
    s = topbar("Julien", "87%")
    y = BAR + 4
    s += sep(y, "hier 18:42"); y += 12
    b, h, _ = bubble(6, y, wrap("Tu rentres à quelle heure ?", BUB_MAXW - 8), False)
    s += b; y += h + 7
    b, h, _ = bubble(W - 6, y, wrap("Vers 19h, je passe prendre le pain", BUB_MAXW - 8), True)
    s += b; y += h + 7
    b, h, _ = bubble(6, y, wrap("Parfait, merci !", BUB_MAXW - 8), False)
    s += b
    s += hintbar("↑↓ defiler   OK ecrire   ` retour")
    return frame(s)


def screen_compose():
    s = topbar("Julien", "87%")
    y = BAR + 4
    b, h, _ = bubble(6, y, wrap("Tu rentres à quelle heure ?", BUB_MAXW - 8), False)
    s += b; y += h + 7
    b, h, _ = bubble(W - 6, y, wrap("Vers 19h, je passe prendre le pain", BUB_MAXW - 8), True)
    s += b
    # Champ de saisie : deux lignes max, curseur clignotant, compteur discret
    cy = H - HINT - 24
    s += f'<rect y="{cy}" width="{W}" height="24" fill="{C["ink800"]}"/>'
    s += f'<rect x="4" y="{cy + 3}" width="{W - 8}" height="18" rx="6" fill="{C["ink700"]}" stroke="{C["blue400"]}" stroke-width="1"/>'
    s += txt(9, cy + 15, "je prends aussi du fromage", C["white"], 11)
    s += f'<rect x="{9 + 26 * GLYPH}" y="{cy + 6}" width="1" height="12" fill="{C["blue400"]}"/>'
    s += hintbar("OK envoyer   ` annuler")
    return frame(s)


def screen_list():
    s = topbar("Conversations", "87%")
    rows = [
        ("Julien", "Parfait, merci !", "18:44", True, False),
        ("Maman", "moi : je t'appelle demain", "17:02", False, True),
        ("École Jean Moulin", "Sortie scolaire vendredi", "hier", False, False),
        ("Thomas", "[pièce jointe]", "hier", False, False),
    ]
    y = BAR
    for i, (name, prev, when, unread, sel) in enumerate(rows):
        rh = 26
        if sel:
            s += f'<rect y="{y}" width="{W}" height="{rh}" fill="{C["ink600"]}"/>'
            s += f'<rect y="{y}" width="2" height="{rh}" fill="{C["blue400"]}"/>'
        nx = 6
        if unread:
            s += f'<circle cx="9" cy="{y + 8}" r="2.5" fill="{C["amber400"]}"/>'
            nx = 15
        s += txt(nx, y + 11, name, C["white"] if unread else C["slate200"], 11,
                 weight="600" if unread else "500")
        s += txt(W - 5, y + 11, when, C["slate300"], 9, anchor="end")
        s += txt(nx, y + 22, prev, C["slate300"], 10)
        if i < len(rows) - 1:
            s += f'<rect y="{y + rh - 1}" x="6" width="{W - 12}" height="1" fill="{C["ink700"]}"/>'
        y += rh
    s += hintbar("↑↓ naviguer   OK ouvrir   r recalibrer")
    return frame(s)


def screen_states():
    out = []
    # Calibration
    s = topbar("Conversations", "87%")
    s += txt(W // 2, 58, "Calibration", C["blue400"], 13, anchor="middle", weight="600")
    s += txt(W // 2, 74, "analyse des conversations récentes", C["slate300"], 9, anchor="middle")
    s += f'<rect x="50" y="86" width="140" height="5" rx="2.5" fill="{C["ink700"]}"/>'
    s += f'<rect x="50" y="86" width="52" height="5" rx="2.5" fill="{C["blue400"]}"/>'
    s += txt(W // 2, 104, "page 9 / 30", C["slate300"], 9, anchor="middle")
    s += hintbar("patientez…")
    out.append(dev(frame(s), "Calibration — progression déterministe, la liste se remplit derrière", True))

    # Premier démarrage
    s = topbar("Configuration requise", "87%")
    s += txt(10, 34, "1", C["blue400"], 11, weight="700")
    s += txt(22, 34, "Réseau WiFi  CardputerBB", C["white"], 11)
    s += txt(22, 46, "mot de passe  bluebubbles", C["slate300"], 10)
    s += txt(10, 66, "2", C["blue400"], 11, weight="700")
    s += txt(22, 66, "http://192.168.4.1", C["white"], 11)
    s += txt(10, 86, "3", C["blue400"], 11, weight="700")
    s += txt(22, 86, "Renseignez WiFi et serveur", C["white"], 11)
    s += txt(22, 98, "l'appareil redémarre ensuite", C["slate300"], 10)
    s += hintbar("portail ouvert")
    out.append(dev(frame(s), "Premier démarrage — trois étapes numérotées, rien d'autre", True))

    # Erreur
    s = topbar("Conversations", "87%")
    y = BAR
    for name, prev in [("Julien", "Parfait, merci !"), ("Maman", "moi : je t'appelle demain")]:
        s += txt(6, y + 11, name, C["slate200"], 11, weight="500")
        s += txt(6, y + 22, prev, C["slate300"], 10)
        y += 26
    ey = H - HINT - 14
    s += f'<rect y="{ey}" width="{W}" height="14" fill="{C["ink800"]}"/>'
    s += f'<rect y="{ey}" width="2" height="14" fill="{C["red400"]}"/>'
    s += txt(7, ey + 10, "Serveur injoignable — nouvelle tentative…", C["red400"], 9)
    out.append(dev(frame(s), "Erreur — bandeau, jamais un écran vide : la liste en cache reste lisible", True))

    # Infos
    s = topbar("Infos", "87%")
    rows = [("Version", "0.2.0"), ("WiFi", "HomeWiFi  −51 dBm"), ("IP", "192.168.1.30"),
            ("Config", "cardputer.local"), ("Synchro", "il y a 8 s"), ("Historique", "10 messages")]
    y = BAR + 4
    for k, v in rows:
        s += txt(6, y + 9, k, C["slate300"], 10)
        s += txt(78, y + 9, v, C["white"], 10)
        y += 13
    s += hintbar("p tester le serveur   ` retour")
    out.append(dev(frame(s), "Infos — diagnostic complet, y compris l'adresse du portail", True))
    return out


# ------------------------------------------------------------------ cartes ---
def write(path, content):
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content)
    print("écrit", path, len(content), "o")


def card_palette():
    tiles = ""
    for name, hexa, src, shown, role in PAL:
        drift = ("" if src == shown else
                 f'<span title="dérive due à la quantification">{src} → {shown}</span>')
        tiles += (f'<div class="tile"><div class="sw" style="background:{shown}"></div>'
                  f'<div class="meta"><div class="nm">{name}</div>'
                  f'<div class="rl">{role}</div><div class="hex"><span>{hexa}</span>'
                  f'{drift or f"<span>{src}</span>"}</div></div></div>')
    body = (f'<div class="warn"><b>Les pastilles montrent la couleur quantifiée RGB565</b>, '
            f'pas la valeur d\'origine — c\'est ce que l\'écran affiche réellement.</div>'
            f'<h2>Encre et bulle — 14 teintes</h2><div class="grid g4">{tiles}</div>'
            f'<h2>Règle d\'emploi</h2><p>Le bleu <code>blue500</code> n\'appartient qu\'aux '
            f'bulles envoyées : c\'est le signal « c\'est moi qui parle ». Les accents '
            f'(<code>amber400</code> non lu, <code>red400</code> erreur, <code>green400</code> '
            f'confirmation) ne s\'affichent jamais à plus d\'un exemplaire à la fois — sur '
            f'240 × 135, deux signaux simultanés n\'en font plus aucun. Les couleurs traversent '
            f'le code en <code>constexpr uint16_t</code> nommées, jamais de littéral inline.</p>')
    return page("Palette", "Une nuit bleutée pour le fond, un seul bleu saturé pour la parole. "
                "Tout le reste est gris : sur un écran de 4 cm, la couleur est un mot, pas un décor.",
                body, "Foundations", "Palette", "14 teintes, quantifiées RGB565")


def card_typography():
    rows = ""
    for f, px, use in [("efontJA_10", "10 px", "Horodatage, aperçu, barre d'aide, méta"),
                       ("efontJA_12", "12 px", "Texte des messages, titres de conversation"),
                       ("efontJA_14", "14 px", "Titre d'écran d'état (calibration)")]:
        rows += f"<tr><td><code>{f}</code></td><td>{px}</td><td>{use}</td></tr>"
    s = f'<rect width="{W}" height="{H}" fill="{C["ink900"]}"/>'
    y = 14
    for label, size, color in [("Message reçu — 12 px", 11, C["white"]),
                               ("Aperçu de conversation — 10 px", 10, C["slate200"]),
                               ("18:44 · horodatage — 10 px", 10, C["slate300"]),
                               ("↑↓ naviguer  OK ouvrir — 10 px", 10, C["slate300"])]:
        s += txt(8, y, label, color, size)
        y += 20
    s += txt(8, y + 4, "Àéèêçùôïœ — les accents passent", C["blue400"], 11)
    y += 24
    s += txt(8, y + 6, "0123456789 ABCDEFGHIJKLM", C["slate200"], 11)
    demo = (f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg" '
            f'shape-rendering="crispEdges">{s}</svg>')
    body = (f'<div class="row">{dev(demo, "Les trois tailles réellement utilisées", True)}</div>'
            f'<h2>Échelle</h2><div class="scroll"><table><tr><th>Fonte</th><th>Taille</th>'
            f'<th>Emploi</th></tr>{rows}</table></div>'
            f'<h2>Pourquoi efont et pas les fontes ASCII de M5GFX</h2>'
            f'<p>Les fontes <code>Font0</code>…<code>Font7</code> de M5GFX ne portent pas les '
            f'accents : « é », « à », « ç » y deviennent des carrés. Toute l\'interface étant en '
            f'français, la famille <code>efontJA</code> est la seule praticable — elle couvre '
            f'l\'ASCII, le latin accentué, et pèse peu.</p>'
            f'<h2>Métriques utiles</h2><p>En <code>efontJA_12</code>, un caractère ASCII occupe '
            f'<b>6 px</b> de large et l\'interligne retenu est de <b>13 px</b> : la zone de '
            f'conversation (103 px) tient donc <b>7 lignes</b>, et une bulle pleine largeur '
            f'(176 px) accepte <b>28 caractères</b> par ligne. Ces deux nombres gouvernent tout '
            f'le découpage de texte du firmware.</p>')
    return page("Typographie", "Une seule famille, trois tailles. Le français impose efont : "
                "les fontes ASCII de M5GFX n'ont pas d'accents.",
                body, "Foundations", "Typographie", "efontJA 10/12/14 — 6 px par caractère")


def card_geometry():
    # Anatomie d'une bulle, agrandie
    aw, ah = 240, 96
    s = f'<rect width="{aw}" height="{ah}" fill="{C["ink900"]}"/>'
    b, h, w = bubble(30, 20, ["Tu rentres à quelle", "heure ?"], False)
    s += b
    s += f'<rect x="30" y="20" width="{w}" height="{h}" fill="none" stroke="{C["amber400"]}" stroke-dasharray="2 2" stroke-width="0.5"/>'
    s += txt(30 + w + 10, 30, f"rayon {BUB_R} px", C["amber400"], 8)
    s += txt(30 + w + 10, 42, f"marge {BUB_PAD_X} x {BUB_PAD_Y} px", C["amber400"], 8)
    s += txt(30 + w + 10, 54, f"interligne {LINE} px", C["amber400"], 8)
    s += txt(30 + w + 10, 66, "ergot 3 px", C["amber400"], 8)
    s += f'<path d="M{30 + 12} {20 + h + 8} L{30} {20 + h + 2}" stroke="{C["amber400"]}" stroke-width="0.5"/>'
    anat = (f'<svg viewBox="0 0 {aw} {ah}" xmlns="http://www.w3.org/2000/svg" '
            f'shape-rendering="crispEdges">{s}</svg>')

    rows = "".join(f"<tr><td><code>{k}</code></td><td>{v}</td><td>{why}</td></tr>" for k, v, why in [
        ("Largeur max", "176 px (73 %)", "Laisse voir le bord opposé : on lit l'alternance sans lire le texte"),
        ("Rayon", "5 px", "Au-delà, la bulle mange ses propres lettres à cette taille"),
        ("Marge interne", "4 px × 3 px", "Le minimum pour que le texte ne touche pas le bord"),
        ("Ergot", "3 px", "Le seul détail qui fait « messagerie » plutôt que « liste »"),
        ("Écart même auteur", "3 px", "Les messages d'un même tour se touchent presque"),
        ("Écart entre auteurs", "7 px", "Le silence entre deux personnes"),
        ("Marge d'écran", "6 px", "Bord gauche des reçues, bord droit des envoyées"),
    ])
    body = (f'<div class="row">{dev(anat, "Anatomie — cotes réelles", True)}</div>'
            f'<h2>Cotes</h2><div class="scroll"><table><tr><th>Élément</th><th>Valeur</th>'
            f'<th>Raison</th></tr>{rows}</table></div>'
            f'<h2>Ce qu\'on ne fait pas</h2><p>Pas d\'horodatage dans chaque bulle : à 6 px par '
            f'caractère, « 18:44 » coûte 30 px de largeur utile par message. Le temps s\'affiche '
            f'en <b>séparateur centré</b> entre deux groupes, uniquement quand l\'écart dépasse '
            f'un quart d\'heure — c\'est la solution d\'iMessage, et la seule qui tienne ici.</p>'
            f'<p>Pas d\'avatar non plus : 20 px de large volés à chaque ligne pour une initiale '
            f'que l\'alignement dit déjà.</p>'
            f'<h2>Zones de l\'écran</h2><p>240 × 135 se découpe en trois bandes fixes : '
            f'<b>barre supérieure 16 px</b> (titre + batterie), <b>contenu 106 px</b>, '
            f'<b>barre d\'aide 13 px</b>. Les trois sont constantes d\'un écran à l\'autre : '
            f'l\'utilisateur sait toujours où regarder.</p>')
    return page("Géométrie des bulles", "La bulle est le seul objet inventé de ce projet. "
                "Tout y est contraint par 240 × 135 : sept lignes, vingt-huit caractères.",
                body, "Foundations", "Géométrie des bulles", "Cotes, ergot, zones d'écran")


def card_conversation():
    body = (f'<div class="row">{dev(screen_conversation(), "Conversation — reçues à gauche, envoyées à droite", True)}'
            f'{dev(screen_compose(), "Composition — le champ pousse la conversation vers le haut", True)}</div>'
            f'<h2>Lecture</h2><p>L\'alignement porte l\'information principale : à gauche l\'autre, '
            f'à droite soi. La couleur la confirme sans la répéter — <code>blue500</code> pour '
            f'l\'envoyé, <code>ink700</code> pour le reçu. Un écran de cette taille ne supporte '
            f'pas deux systèmes de signes concurrents.</p>'
            f'<h2>Composition</h2><p>Le champ de saisie est <b>surélevé et cerné de bleu</b> : '
            f'il prend 24 px et repousse la conversation, plutôt que de la recouvrir. On voit '
            f'toujours à qui l\'on répond. Le curseur est une barre pleine de 1 × 12 px, sans '
            f'clignotement — l\'écran n\'est rafraîchi qu\'à la frappe.</p>')
    return page("Conversation", "Le cœur de l'appareil : lire un fil et y répondre, "
                "sur sept lignes de treize pixels.",
                body, "Écrans", "Conversation", "Bulles, composition")


def card_list():
    body = (f'<div class="row">{dev(screen_list(), "Liste — quatre conversations visibles", True)}</div>'
            f'<h2>Une ligne, deux étages</h2><p>26 px par conversation : le nom sur la première '
            f'ligne, l\'aperçu sur la seconde, l\'heure calée à droite. Quatre tiennent à '
            f'l\'écran — au-delà, on défile. La ligne sélectionnée reçoit un fond '
            f'<code>ink600</code> et un <b>filet bleu de 2 px</b> à gauche : le fond seul serait '
            f'trop discret sur un écran très éclairé.</p>'
            f'<h2>Le non-lu</h2><p>Une pastille ambre de 2,5 px de rayon et un nom en blanc '
            f'plein. Pas de compteur : savoir qu\'il y a du nouveau suffit, le nombre exact ne '
            f'change aucune décision.</p>'
            f'<h2>Aperçus</h2><p>« moi : » préfixe les messages qu\'on a envoyés. Les pièces '
            f'jointes et les messages sans texte deviennent <code>[pièce jointe]</code> — jamais '
            f'de ligne vide, qui laisserait croire à un bug.</p>')
    return page("Liste des conversations", "L'écran d'accueil. Quatre conversations, "
                "deux étages chacune, et le non-lu visible d'un coup d'œil.",
                body, "Écrans", "Liste des conversations", "Lignes, sélection, non-lu")


def card_states():
    body = (f'<div class="grid gdev">{"".join(screen_states())}</div>'
            f'<h2>Principe</h2><p>Un écran d\'état ne remplace jamais du contenu déjà chargé. '
            f'La calibration est le seul plein écran — c\'est le seul moment où il n\'y a '
            f'effectivement rien à montrer. Toutes les erreurs ultérieures sont des '
            f'<b>bandeaux de 14 px</b> en bas : la liste en cache reste lisible pendant que '
            f'l\'appareil se rattrape.</p>'
            f'<h2>Progression honnête</h2><p>La calibration annonce « page 9 / 30 » parce que le '
            f'nombre de pages est connu d\'avance. Une barre qui progresse sans dénominateur est '
            f'un mensonge poli ; ici on peut compter.</p>')
    return page("États", "Démarrage, calibration, erreur, diagnostic — les quatre moments "
                "où l'appareil parle de lui-même.",
                body, "Écrans", "États", "Calibration, premier démarrage, erreur, infos")


if __name__ == "__main__":
    write("foundations/palette.html", card_palette())
    write("foundations/typography.html", card_typography())
    write("foundations/geometry.html", card_geometry())
    write("screens/conversation.html", card_conversation())
    write("screens/liste.html", card_list())
    write("screens/etats.html", card_states())
