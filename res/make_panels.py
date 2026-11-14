#!/usr/bin/env python3
"""Draws the panel SVGs in res/.

VCV Rack can't draw SVG <text>, so every label is turned into outline paths
with fontTools. All positions are in millimetres and must match the positions
used in each module's widget code in src/modules/.

    pip install fonttools
    python3 res/make_panels.py
"""
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

HP = 5.08
H = 128.5

# Colours: keep in step with src/common/Theme.hpp
PANEL, TEXT, DIM, SCREEN, LINE = "#1d1d1f", "#d8d8d8", "#7a7a80", "#0b0b0d", "#34343a"
AMBER, TEAL, VIOLET = "#e8a33c", "#3cc8b4", "#9b7be8"  # time, sound, system

# Haer's label standard: text heights in mm. Widen the panel rather than shrink these.
CTRL_LABEL = 3.2
JACK_LABEL = 2.9
TITLE = 4.6

_fonts = {}


def _font(font):
    if font not in _fonts:
        f = TTFont(font)
        _fonts[font] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    return _fonts[font]


def text_width(s, size, font=FONT_BOLD, spacing=0.06):
    gs, cmap, upm = _font(font)
    track = spacing * upm
    return (sum(gs[cmap[ord(c)]].width + track for c in s) - track) * size / upm


def text(s, x, y, size, font=FONT_BOLD, fill=TEXT, anchor="middle", spacing=0.06):
    """An SVG path for string s. (x, y) is the baseline point in mm."""
    gs, cmap, upm = _font(font)
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = text_width(s, size, font, spacing)
    x0 = {"middle": x - width / 2, "end": x - width}.get(anchor, x)
    pen = SVGPathPen(gs, ntos=lambda v: f"{v:.3f}".rstrip("0").rstrip("."))
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


# Sizes of the standard Rack parts, in mm (radius), for the overlap check.
PART_R = {"knob": 5.1, "small": 4.1, "trim": 3.1, "jack": 4.2, "button": 3.1, "switch3": (2.6, 5.0), "switch2": (2.6, 4.0),
          "light": 1.1}


class Panel:
    """One module panel. Positions given to place() are also written to a C++
    header so the widget code and the artwork can't drift apart."""

    def __init__(self, slug, hp, title, accent, blurb=None):
        self.slug, self.hp, self.w, self.accent = slug, hp, hp * HP, accent
        self.parts = [f'<rect width="{self.w:.2f}" height="{H}" fill="{PANEL}"/>']
        self.boxes = []   # (name, x0, y0, x1, y1) of labels and parts, for the overlap check
        self.layout = {}  # name -> (x, y) or list of (x, y)
        self.kinds = []   # (kind, positions), for the preview picture
        self.parts.append(f'<rect width="{self.w:.2f}" height="1.6" fill="{accent}"/>')
        if hp >= 12:
            # title on the left, what it does next to it, maker on the right
            self.add(text(title, 9.5, 10.2, TITLE, fill=accent, anchor="start", spacing=0.14))
            tw = text_width(title, TITLE, spacing=0.14)
            if blurb:
                self.add(text(blurb, 9.5 + tw + 3.5, 10.0, 2.6, font=FONT_MED, fill=DIM, anchor="start", spacing=0.03))
            self.add(text("Haer", self.w - 9.5, 10.0, 2.9, fill=DIM, anchor="end", spacing=0.1))
        else:
            self.add(text(title, self.w / 2, 9.6, TITLE, fill=accent, spacing=0.14))
            self.add(text("Haer", self.w / 2, H - 4.2, 2.9, fill=DIM, spacing=0.1))

    def add(self, s):
        self.parts.append(s)

    def _box(self, name, x0, y0, x1, y1):
        self.boxes.append((name, x0, y0, x1, y1))

    def label(self, s, x, y, jack=False, fill=TEXT, anchor="middle", size=None):
        size = size or (JACK_LABEL if jack else CTRL_LABEL)
        w = text_width(s, size)
        x0 = {"middle": x - w / 2, "end": x - w}.get(anchor, x)
        self._box("'" + s + "'", x0, y - size * 0.74, x0 + w, y)
        self.add(text(s, x, y, size, fill=fill, anchor=anchor))

    def note(self, s, x, y, size=2.4, fill=DIM, anchor="middle"):
        w = text_width(s, size, FONT_MED, 0.03)
        x0 = {"middle": x - w / 2, "end": x - w}.get(anchor, x)
        self._box("'" + s + "'", x0, y - size * 0.74, x0 + w, y)
        self.add(text(s, x, y, size, font=FONT_MED, fill=fill, anchor=anchor, spacing=0.03))

    def place(self, name, kind, pos):
        """Record where a control, jack or light goes. pos is (x, y) or a list of them."""
        self.layout[name] = pos
        self.kinds.append((kind, pos if isinstance(pos, list) else [pos]))
        r = PART_R[kind]
        rx, ry = r if isinstance(r, tuple) else (r, r)
        for i, (x, y) in enumerate(pos if isinstance(pos, list) else [pos]):
            self._box(f"{name}[{i}]", x - rx, y - ry, x + rx, y + ry)

    def area(self, name, x, y, w, h):
        """Record a display rectangle."""
        self.layout[name] = [(x, y), (w, h)]
        self._box(name, x, y, x + w, y + h)

    def line(self, x1, y1, x2, y2, color=LINE, w=0.3):
        self.add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{color}" stroke-width="{w}"/>')

    def screen(self, name, x, y, w, h):
        self.add(f'<rect x="{x - 0.7:.2f}" y="{y - 0.7:.2f}" width="{w + 1.4:.2f}" height="{h + 1.4:.2f}" rx="1.6" fill="#101012"/>')
        self.add(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="1" fill="{SCREEN}"/>')
        self.area(name, x, y, w, h)

    def outbox(self, x, y, w, h):
        """A lighter box behind output jacks, so outputs read at a glance."""
        self.add(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="1.6" fill="#303036"/>')

    def check(self):
        bad = 0
        for i in range(len(self.boxes)):
            for j in range(i + 1, len(self.boxes)):
                a, b = self.boxes[i], self.boxes[j]
                if a[1] < b[3] - 0.15 and b[1] < a[3] - 0.15 and a[2] < b[4] - 0.15 and b[2] < a[4] - 0.15:
                    print(f"  OVERLAP in {self.slug}: {a[0]} and {b[0]}")
                    bad += 1
        for name, x0, y0, x1, y1 in self.boxes:
            if x0 < 1.0 or x1 > self.w - 1.0 or y1 > H - 2.0:
                print(f"  OFF PANEL in {self.slug}: {name}")
                bad += 1
        return bad

    def preview(self, folder):
        """A rough picture of the panel with stand-in knobs and jacks, for checking the layout by eye."""
        extra = []
        for kind, positions in self.kinds:
            for x, y in positions:
                r = PART_R[kind]
                if kind in ("knob", "small", "trim"):
                    extra.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#0c0c0c" stroke="#444" stroke-width="0.3"/>'
                                 f'<line x1="{x}" y1="{y}" x2="{x}" y2="{y - r + 0.6}" stroke="#eee" stroke-width="0.6"/>')
                elif kind == "jack":
                    extra.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#b9b9b9"/><circle cx="{x}" cy="{y}" r="1.9" fill="#111"/>')
                elif kind == "button":
                    extra.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#555" stroke="#222" stroke-width="0.5"/>')
                elif kind == "switch3":
                    extra.append(f'<rect x="{x - 2.3}" y="{y - 4.8}" width="4.6" height="9.6" rx="0.8" fill="#333"/>'
                                 f'<rect x="{x - 1.6}" y="{y - 4.2}" width="3.2" height="3.2" fill="#ccc"/>')
                elif kind == "switch2":
                    extra.append(f'<rect x="{x - 2.3}" y="{y - 3.8}" width="4.6" height="7.6" rx="0.8" fill="#333"/>'
                                 f'<rect x="{x - 1.6}" y="{y - 3.2}" width="3.2" height="3.2" fill="#ccc"/>')
                elif kind == "light":
                    extra.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#5a4a20"/>')
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w * 8:.0f}" height="{H * 8:.0f}" '
               f'viewBox="0 0 {self.w:.2f} {H}">\n' + "\n".join(self.parts + extra) + "\n</svg>\n")
        with open(os.path.join(folder, self.slug + "-preview.svg"), "w") as f:
            f.write(svg)

    def save(self):
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w:.2f}mm" height="{H}mm" '
               f'viewBox="0 0 {self.w:.2f} {H}">\n' + "\n".join(self.parts) + "\n</svg>\n")
        path = os.path.join(HERE, self.slug + ".svg")
        with open(path, "w") as f:
            f.write(svg)
        bad = self.check()
        if os.environ.get("PREVIEW_DIR"):
            self.preview(os.environ["PREVIEW_DIR"])
        if self.layout:
            out = ["// Written by res/make_panels.py. Do not edit: change the script and run it again.",
                   "// Positions are in millimetres from the panel's top left corner.",
                   "#pragma once", "", f"namespace layout_{self.slug.lower()} {{", "",
                   f"inline constexpr float HP = {self.hp};"]
            for name, pos in self.layout.items():
                if isinstance(pos, list):
                    items = ", ".join(f"{{{x:.2f}f, {y:.2f}f}}" for x, y in pos)
                    out.append(f"inline constexpr float {name}[{len(pos)}][2] = {{{items}}};")
                else:
                    out.append(f"inline constexpr float {name}[2] = {{{pos[0]:.2f}f, {pos[1]:.2f}f}};")
            out += ["", "}", ""]
            with open(os.path.join(HERE, "..", "src", "modules", self.slug + "Layout.hpp"), "w") as f:
                f.write("\n".join(out))
        print("wrote", path, f"({self.hp} HP)", "OK" if not bad else f"with {bad} problems")


# Row heights shared by the wide modules, so they line up next to each other.
ROW_A, ROW_B, JACKS_1, JACKS_2 = 72.0, 89.0, 104.5, 119.5
KNOB_LABEL, JACK_LABEL_DY = 7.5, 6.0


def knob_row(p, y, items, kind="knob"):
    for name, label, x in items:
        p.place(name, kind, (x, y))
        p.label(label, x, y - KNOB_LABEL)


def jack_row(p, y, items, fill=TEXT):
    for name, label, x in items:
        p.place(name, "jack", (x, y))
        if label:
            p.label(label, x, y - JACK_LABEL_DY, jack=True, fill=fill)


def slot_lights(p, x0, y):
    p.place("SLOT_LIGHTS", "light", [(x0 + i * 4.0, y) for i in range(8)])
    p.label("SLOTS", x0 + 14.0, y - KNOB_LABEL)
    for i in (0, 7):
        p.note(str(i + 1), x0 + i * 4.0, y + 4.6)


def warp():
    p = Panel("Warp", 24, "WARP", AMBER, "curved master clock")
    p.screen("DISPLAY", 5.0, 15.0, 50.8, 40.0)

    # per-output settings: one row per phase output
    cols = (("RATIO", 76.0), ("OFFSET", 94.0), ("FOLLOW", 112.0))
    rows = [26.0, 35.0, 44.0, 53.0]
    for label, x in cols:
        p.label(label, x, 19.0)
    for i, y in enumerate(rows):
        p.label(str(i + 1), 62.5, y + 1.2, fill=AMBER)
        p.line(66.0, y, 70.0, y)
    p.place("RATIO", "small", [(76.0, y) for y in rows])
    p.place("OFFSET", "trim", [(94.0, y) for y in rows])
    p.place("FOLLOW", "trim", [(112.0, y) for y in rows])

    xs = [10.0 + i * 16.99 for i in range(7)]
    knob_row(p, ROW_A, [("BPM", "BPM", xs[0]), ("BEATS", "BEATS", xs[1]), ("DRIFT", "DRIFT", xs[2]),
                        ("PERIOD", "PERIOD", xs[3]), ("SHAPE", "SHAPE", xs[4]), ("DEPTH", "DEPTH", xs[5]),
                        ("SEED", "SEED", xs[6])])
    p.line(xs[2] - 5, ROW_A - 12.2, xs[3] + 5, ROW_A - 12.2, AMBER, 0.35)   # tempo drift pair
    p.line(xs[4] - 5, ROW_A - 12.2, xs[5] + 5, ROW_A - 12.2, AMBER, 0.35)   # warp pair

    cx = [10.0, 24.56, 39.12, 53.68]
    knob_row(p, ROW_B, [("RUN", "RUN", cx[0]), ("RESET", "RESET", cx[1]), ("KEEP", "KEEP", cx[2])], kind="button")
    knob_row(p, ROW_B, [("RECALL", "RECALL", cx[3])], kind="small")
    slot_lights(p, 68.0, ROW_B)
    # the four jacks under those controls trigger them
    for x in cx:
        p.line(x, ROW_B + 5.0, x, JACKS_1 - 4.6)
    jack_row(p, JACKS_1, [("RUN_IN", None, cx[0]), ("RESET_IN", None, cx[1]), ("KEEP_IN", None, cx[2]),
                          ("RECALL_IN", None, cx[3]), ("TEMPO_IN", "TEMPO", 70.0), ("SHAPE_IN", "SHAPE", 84.5),
                          ("DEPTH_IN", "DEPTH", 99.0), ("SEED_IN", "SEED", 113.5)])
    p.outbox(3.0, JACKS_2 - 9.2, p.w - 6.0, 14.6)
    ox = [10.0 + i * 14.56 for i in range(8)]
    jack_row(p, JACKS_2, [("PHASE_OUT_1", "1", ox[0]), ("PHASE_OUT_2", "2", ox[1]), ("PHASE_OUT_3", "3", ox[2]),
                          ("PHASE_OUT_4", "4", ox[3]), ("POLY_OUT", "POLY", ox[4]), ("BAR_OUT", "BAR", ox[5]),
                          ("BEAT_OUT", "BEAT", ox[6]), ("TEMPO_OUT", "TEMPO", ox[7])], fill=AMBER)
    p.save()


def contour():
    p = Panel("Contour", 28, "CONTOUR", AMBER, "rhythm where a curve crosses levels")
    p.screen("DISPLAY", 5.0, 15.0, 70.0, 40.0)

    # the four harmonics of the HARM curve
    hx = [99.0, 111.0, 123.0, 135.0]
    hy = [27.0, 38.0, 49.0]
    for i, x in enumerate(hx):
        p.label(str(i + 1), x, 19.5, fill=AMBER)
    for label, y in zip(("AMP", "RATIO", "PHASE"), hy):
        p.label(label, 79.5, y + 1.2, anchor="start")
    p.place("AMP", "small", [(x, hy[0]) for x in hx])
    p.place("RATIO", "small", [(x, hy[1]) for x in hx])
    p.place("THETA", "small", [(x, hy[2]) for x in hx])

    p.place("MODE", "switch3", (8.5, ROW_A))
    p.label("MODE", 13.0, ROW_A - KNOB_LABEL)
    for s, dy, c in (("HARM", -3.3, DIM), ("MOIRE", 0.9, AMBER), ("EXT", 5.1, DIM)):
        p.note(s, 13.0, ROW_A + dy, anchor="start", fill=c)
    knob_row(p, ROW_A, [("RATE", "RATE", 31.0), ("MORPH", "MORPH", 47.5), ("THRESH", "THRESH", 64.0),
                        ("BUNCH", "BUNCH", 80.5), ("HYST", "HYST", 97.0), ("CHANS", "CHANS", 112.0)])
    p.place("DIR", "switch3", (124.5, ROW_A))
    p.label("DIR", 129.0, ROW_A - KNOB_LABEL)
    for s, dy in (("UP", -3.3), ("BOTH", 0.9), ("DOWN", 5.1)):
        p.note(s, 129.0, ROW_A + dy, anchor="start")

    # the three MOIRE controls are lettered in amber
    for name, x in (("SPREAD", 14.0), ("COINC", 31.0), ("WINDOW", 48.0)):
        p.place(name, "knob", (x, ROW_B))
        p.label(name, x, ROW_B - KNOB_LABEL, fill=AMBER)
    knob_row(p, ROW_B, [("SEED", "SEED", 65.0)])
    knob_row(p, ROW_B, [("KEEP", "KEEP", 82.0)], kind="button")
    knob_row(p, ROW_B, [("RECALL", "RECALL", 96.0)], kind="small")
    slot_lights(p, 109.0, ROW_B)

    ix = [10.0 + i * 15.28 for i in range(9)]
    jack_row(p, JACKS_1, [("PHASE_IN", "PHASE", ix[0]), ("EXT_IN", "EXT", ix[1]), ("RATE_IN", "RATE", ix[2]),
                          ("MORPH_IN", "MORPH", ix[3]), ("DENS_IN", "DENS", ix[4]), ("SPREAD_IN", "SPREAD", ix[5]),
                          ("SEED_IN", "SEED", ix[6]), ("KEEP_IN", "KEEP", ix[7]), ("RECALL_IN", "RECALL", ix[8])])
    p.outbox(3.0, JACKS_2 - 9.2, p.w - 6.0, 14.6)
    ox = [12.0 + i * 19.7 for i in range(7)]
    jack_row(p, JACKS_2, [("TRIG_OUT", "TRIG", ox[0]), ("VEL_OUT", "VEL", ox[1]), ("CURVE_OUT", "CURVE", ox[2]),
                          ("SLOPE_OUT", "SLOPE", ox[3]), ("COINC_OUT", "COINC", ox[4]), ("COUNT_OUT", "COUNT", ox[5]),
                          ("PHASORS_OUT", "PHASORS", ox[6])], fill=AMBER)
    p.save()


def mutate():
    p = Panel("Mutate", 38, "MUTATE", AMBER, "step drums that rewrite themselves")
    p.screen("DISPLAY", 5.0, 15.0, p.w - 10.0, 36.0)

    ya, yb = 68.0, 86.0
    xa = [11.5 + i * 15.45 for i in range(12)]
    knob_row(p, ya, [("STEPS", "STEPS", xa[0]), ("DENSITY", "DENS", xa[1]), ("SEED", "SEED", xa[2])])
    knob_row(p, ya, [("GENERATE", "GEN", xa[3])], kind="button")
    knob_row(p, ya, [("EVERY", "EVERY", xa[4]), ("AMOUNT", "AMT", xa[5]), ("DRIFT", "DRIFT", xa[6])])
    knob_row(p, ya, [("HOME", "HOME", xa[7]), ("HOME_SET", "SET", xa[8])], kind="button")
    knob_row(p, ya, [("CURVE", "CURVE", xa[9]), ("DECAY", "DECAY", xa[10]), ("SWING", "SWING", xa[11])])
    # three groups: make a pattern, mutate it, ratchet and swing feel
    for a, b in ((0, 3), (4, 8), (9, 11)):
        p.line(xa[a] - 5, ya - 12.4, xa[b] + 5, ya - 12.4, AMBER, 0.35)

    # how often each mutation rule is picked
    rules = ["ROTATE", "NUDGE", "RATCH", "THIN", "MIRROR", "COPY", "LOCK"]
    xr = [11.5 + i * 15.45 for i in range(7)]
    p.place("RULE", "trim", [(x, yb) for x in xr])
    for name, x in zip(rules, xr):
        p.label(name, x, yb - KNOB_LABEL, fill=AMBER)
    knob_row(p, yb, [("KEEP", "KEEP", 127.0)], kind="button")
    knob_row(p, yb, [("RECALL", "RECALL", 142.5)], kind="small")
    slot_lights(p, 156.0, yb)

    ix = [9.5 + i * 14.5 for i in range(13)]
    names = [("PHASE_IN", "PHASE"), ("CLOCK_IN", "CLOCK"), ("RESET_IN", "RESET"), ("GENERATE_IN", "GEN"),
             ("MUTATE_IN", "MUTATE"), ("HOME_IN", "HOME"), ("DENSITY_IN", "DENS"), ("AMOUNT_IN", "AMT"),
             ("DRIFT_IN", "DRIFT"), ("MICRO_IN", "MICRO"), ("SEED_IN", "SEED"), ("KEEP_IN", "KEEP"),
             ("RECALL_IN", "RECALL")]
    jack_row(p, JACKS_1, [(n, l, x) for (n, l), x in zip(names, ix)])
    p.outbox(3.0, JACKS_2 - 9.2, p.w - 6.0, 14.6)
    p.place("GATE_OUT", "jack", [(ix[i], JACKS_2) for i in range(8)])
    for i in range(8):
        p.label(str(i + 1), ix[i], JACKS_2 - JACK_LABEL_DY, jack=True, fill=AMBER)
    jack_row(p, JACKS_2, [("GATES_OUT", "GATES", ix[8]), ("VEL_OUT", "VEL", ix[9]), ("LOCK_A_OUT", "LOCK A", ix[10]),
                          ("LOCK_B_OUT", "LOCK B", ix[11]), ("ACCENT_OUT", "ACCENT", ix[12])], fill=AMBER)
    p.save()


def strike():
    p = Panel("Strike", 20, "STRIKE", TEAL, "modal percussion")
    xs = [12.0 + i * 19.4 for i in range(5)]
    y1, y2, y3 = 30.0, 54.0, 78.0

    # what hits it
    knob_row(p, y1, [("EXCITER", "EXCITER", xs[0]), ("HARD", "HARD", xs[1]), ("BURST", "BURST", xs[2]),
                     ("RATE", "RATE", xs[3])])
    p.line(xs[0] - 5, y1 - 12.4, xs[3] + 5, y1 - 12.4, TEAL, 0.35)
    p.note("CLICK  MALLET  NOISE  EXT", xs[0] - 5.0, y1 + 8.6, anchor="start")
    p.place("MODE", "switch2", (82.0, y1))
    p.label("MODE", 87.5, y1 - KNOB_LABEL)
    p.note("MODAL", 86.0, y1 - 1.6, anchor="start")
    p.note("FORMANT", 86.0, y1 + 3.0, anchor="start", fill=TEAL)

    # what rings
    knob_row(p, y2, [("PITCH", "PITCH", xs[0]), ("MATERIAL", "MATERIAL", xs[1]), ("DECAY", "DECAY", xs[2]),
                     ("DAMP", "DAMP", xs[3]), ("BRIGHT", "BRIGHT", xs[4])])
    knob_row(p, y3, [("INHARM", "INHARM", xs[0]), ("POSITION", "POS", xs[1]), ("SPREAD", "SPREAD", xs[2])])
    # what MATERIAL means in each mode
    lx = 63.0
    p.note("MATERIAL, LEFT TO RIGHT", lx, y3 - 6.0, anchor="start", fill=TEXT)
    p.note("WOOD  RUBBER  METAL", lx, y3 - 2.0, anchor="start")
    p.note("GLASS  DRUM SKIN", lx, y3 + 1.6, anchor="start")
    p.note("FORMANT:  A  E  I  O  U", lx, y3 + 5.8, anchor="start", fill=TEAL)

    ix = [10.0 + i * 13.6 for i in range(7)]
    jack_row(p, JACKS_1, [("TRIG_IN", "TRIG", ix[0]), ("VEL_IN", "VEL", ix[1]), ("VOCT_IN", "V/OCT", ix[2]),
                          ("MATERIAL_IN", "MATER", ix[3]), ("DECAY_IN", "DECAY", ix[4]), ("POSITION_IN", "POS", ix[5]),
                          ("DAMP_IN", "DAMP", ix[6])])
    jack_row(p, JACKS_2, [("BRIGHT_IN", "BRIGHT", ix[0]), ("EXCITE_IN", "EXCITE", ix[1]), ("CHOKE_IN", "CHOKE", ix[2])])
    p.outbox(ix[4] - 7.5, JACKS_2 - 9.2, p.w - 3.0 - (ix[4] - 7.5), 14.6)
    jack_row(p, JACKS_2, [("L_OUT", "L", ix[4]), ("R_OUT", "R", ix[5]), ("POLY_OUT", "POLY", ix[6])], fill=TEAL)
    p.save()


def fmx():
    p = Panel("Fmx", 24, "FMX", TEAL, "FM voice shaped by phase")
    p.screen("DISPLAY", 5.0, 15.0, 40.0, 40.0)

    # the four operators
    hx = [72.0, 86.0, 100.0, 114.0]
    hy = [25.5, 36.0, 46.5, 57.0]
    for i, x in enumerate(hx):
        p.label(str(i + 1), x, 19.0, fill=TEAL)
    for label, y in zip(("RATIO", "FINE", "LEVEL", "ENV"), hy):
        p.label(label, 49.5, y + 1.2, anchor="start")
    p.place("RATIO", "small", [(x, hy[0]) for x in hx])
    p.place("FINE", "small", [(x, hy[1]) for x in hx])
    p.place("LEVEL", "small", [(x, hy[2]) for x in hx])
    p.place("ENV", "small", [(x, hy[3]) for x in hx])

    ya = 77.0
    xs = [11.0 + i * 16.65 for i in range(7)]
    knob_row(p, ya, [("ALGO", "ALGO", xs[0]), ("INDEX", "INDEX", xs[1]), ("FEEDBACK", "FB", xs[2]),
                     ("TIME", "TIME", xs[3]), ("AMP_SHAPE", "AMP", xs[4]), ("INDEX_SHAPE", "TONE", xs[5]),
                     ("SPREAD", "SPREAD", xs[6])])
    # TIME, AMP and TONE are the envelope: how long, and how fast loudness and brightness fall
    p.line(xs[3] - 5, ya - 12.4, xs[5] + 5, ya - 12.4, TEAL, 0.35)

    ix = [10.0 + i * 11.32 for i in range(10)]
    jack_row(p, JACKS_1, [("VOCT_IN", "V/OCT", ix[0]), ("TRIG_IN", "TRIG", ix[1]), ("PHASE_IN", "PHASE", ix[2]),
                          ("VEL_IN", "VEL", ix[3]), ("INDEX_IN", "INDEX", ix[4]), ("RATIO2_IN", "R2", ix[5]),
                          ("RATIO3_IN", "R3", ix[6]), ("RATIO4_IN", "R4", ix[7]), ("FEEDBACK_IN", "FB", ix[8]),
                          ("ALGO_IN", "ALGO", ix[9])])
    p.outbox(3.0, JACKS_2 - 9.2, p.w - 6.0, 14.6)
    ox = [11.0 + i * 16.65 for i in range(7)]
    jack_row(p, JACKS_2, [("L_OUT", "L", ox[0]), ("R_OUT", "R", ox[1]), ("POLY_OUT", "POLY", ox[2])], fill=TEAL)
    p.place("OP_OUT", "jack", [(ox[3 + i], JACKS_2) for i in range(4)])
    for i in range(4):
        p.label("OP " + str(i + 1), ox[3 + i], JACKS_2 - JACK_LABEL_DY, jack=True, fill=TEAL)
    p.save()


def chain():
    p = Panel("Chain", 34, "CHAIN", AMBER, "four melody voices with harmony rules")
    p.screen("DISPLAY", 5.0, 15.0, 80.0, 40.0)

    # one column per voice
    hx = [118.0, 132.0, 146.0, 160.0]
    hy = [27.0, 38.0, 49.0]
    for i, x in enumerate(hx):
        p.label(str(i + 1), x, 19.5, fill=AMBER)
    for label, y in zip(("CHAR", "RANGE", "CENTER"), hy):
        p.label(label, 90.0, y + 1.2, anchor="start")
    p.place("CHARACTER", "small", [(x, hy[0]) for x in hx])
    p.place("RANGE", "trim", [(x, hy[1]) for x in hx])
    p.place("CENTER", "trim", [(x, hy[2]) for x in hx])

    knob_row(p, ROW_A, [("SCALE", "SCALE", 12.0), ("ROOT", "ROOT", 30.0)])
    p.place("ORDER", "switch2", (46.0, ROW_A))
    p.label("ORDER", 50.0, ROW_A - KNOB_LABEL)
    p.note("2 NOTES BACK", 50.0, ROW_A - 1.6, anchor="start")
    p.note("1 NOTE BACK", 50.0, ROW_A + 3.0, anchor="start")
    knob_row(p, ROW_A, [("GLIDE", "GLIDE", 80.0), ("SEED", "SEED", 97.0)])
    knob_row(p, ROW_A, [("KEEP", "KEEP", 113.0)], kind="button")
    knob_row(p, ROW_A, [("RECALL", "RECALL", 127.0)], kind="small")
    slot_lights(p, 139.0, ROW_A)

    # the rules: six strengths (amber), and the two numbers that go with MAX LEAP and HOME
    rx = [12.0 + i * 18.0 for i in range(8)]
    rules = [("RULE_SCALE", "IN KEY"), ("RULE_PARALLEL", "NO PAR"), ("RULE_INTERVAL", "INTVL"),
             ("RULE_CONTRARY", "CONTRA"), ("RULE_LEAP", "MAX LEAP")]
    for (name, label), x in zip(rules, rx):
        p.place(name, "knob", (x, ROW_B))
        p.label(label, x, ROW_B - KNOB_LABEL, fill=AMBER)
    knob_row(p, ROW_B, [("LEAP", "SEMIS", rx[5])])
    p.place("RULE_HOME", "knob", (rx[6], ROW_B))
    p.label("HOME", rx[6], ROW_B - KNOB_LABEL, fill=AMBER)
    knob_row(p, ROW_B, [("HOME_STEPS", "STEPS", rx[7])])
    p.line(rx[4] + 5.6, ROW_B, rx[5] - 5.6, ROW_B)
    p.line(rx[6] + 5.6, ROW_B, rx[7] - 5.6, ROW_B)
    p.note("RULES", 153.0, ROW_B - 1.0, anchor="start", fill=AMBER)
    p.note("0 = OFF", 153.0, ROW_B + 2.6, anchor="start")

    ix = [10.0 + i * 12.73 for i in range(13)]
    p.place("STEP_IN", "jack", [(ix[i], JACKS_1) for i in range(4)])
    for i in range(4):
        p.label("ST " + str(i + 1), ix[i], JACKS_1 - JACK_LABEL_DY, jack=True)
    jack_row(p, JACKS_1, [("SCALE_IN", "SCALE", ix[4]), ("ROOT_IN", "ROOT", ix[5]), ("LEARN_IN", "LEARN", ix[6]),
                          ("LEARN_PITCH_IN", "PITCH", ix[7]), ("CHARACTER_IN", "CHAR", ix[8]),
                          ("RESET_IN", "RESET", ix[9]), ("SEED_IN", "SEED", ix[10]), ("KEEP_IN", "KEEP", ix[11]),
                          ("RECALL_IN", "RECL", ix[12])])
    p.line(ix[6] + 4.6, JACKS_1, ix[7] - 4.6, JACKS_1)   # LEARN listens to the PITCH next to it
    p.outbox(3.0, JACKS_2 - 9.2, p.w - 6.0, 14.6)
    ox = [11.0 + i * 16.75 for i in range(10)]
    p.place("PITCH_OUT", "jack", [(ox[i], JACKS_2) for i in range(4)])
    p.place("GATE_OUT", "jack", [(ox[5 + i], JACKS_2) for i in range(4)])
    for i in range(4):
        p.label("PITCH " + str(i + 1), ox[i], JACKS_2 - JACK_LABEL_DY, jack=True, fill=AMBER)
        p.label("GATE " + str(i + 1), ox[5 + i], JACKS_2 - JACK_LABEL_DY, jack=True, fill=AMBER)
    jack_row(p, JACKS_2, [("POLY_OUT", "POLY", ox[4]), ("VIOLATION_OUT", "VIOL", ox[9])], fill=AMBER)
    p.save()


def splice():
    p = Panel("Splice", 26, "SPLICE", TEAL, "cut-up and smear buffer")
    p.screen("DISPLAY", 5.0, 15.0, p.w - 10.0, 34.0)

    # the tape and the slice
    xs = [11.0 + i * 15.72 for i in range(8)]
    p.place("REC", "switch3", (xs[0] - 3.0, ROW_A))
    p.label("REC", xs[0] - 3.0, ROW_A - KNOB_LABEL)
    p.note("ALWAYS", xs[0] + 0.6, ROW_A - 3.0, anchor="start")
    p.note("GATED", xs[0] + 0.6, ROW_A + 0.8, anchor="start")
    p.note("OFF", xs[0] + 0.6, ROW_A + 4.6, anchor="start")
    knob_row(p, ROW_A, [("DIV", "DIV", xs[1] + 3.0), ("TIME", "TIME", xs[2] + 2.0), ("HOLD", "HOLD", xs[3] + 1.0)])
    # what each repeat does
    for name, label, x in [("ACCEL", "ACCEL", xs[4]), ("PITCH", "PITCH", xs[5]), ("REVERSE", "REV", xs[6]),
                           ("DECAY", "DECAY", xs[7])]:
        p.place(name, "knob", (x, ROW_A))
        p.label(label, x, ROW_A - KNOB_LABEL, fill=TEAL)
    p.line(xs[4] - 5, ROW_A - 12.4, xs[7] + 5, ROW_A - 12.4, TEAL, 0.35)
    p.note("EACH REPEAT", xs[4] - 5, ROW_A - 13.6, anchor="start", fill=TEAL)
    # grains
    knob_row(p, ROW_B, [("SMEAR", "SMEAR", xs[0]), ("GRAIN", "GRAIN", xs[1]), ("DENSITY", "DENS", xs[2]),
                        ("JITTER", "JITTER", xs[3]), ("WIDTH", "WIDTH", xs[4])])
    knob_row(p, ROW_B, [("SEED", "SEED", xs[7])])
    p.note("DIV: WITH A PHASE CABLE", xs[5] - 7.0, ROW_B - 2.0, anchor="start")
    p.note("TIME: WITHOUT ONE", xs[5] - 7.0, ROW_B + 1.8, anchor="start")

    ix = [10.0 + i * 12.45 for i in range(10)]
    jack_row(p, JACKS_1, [("L_IN", "IN L", ix[0]), ("R_IN", "IN R", ix[1]), ("PHASE_IN", "PHASE", ix[2]),
                          ("STUTTER_IN", "STUT", ix[3]), ("FREEZE_IN", "FREEZE", ix[4]), ("SCRUB_IN", "SCRUB", ix[5]),
                          ("ACCEL_IN", "ACCEL", ix[6]), ("PITCH_IN", "PITCH", ix[7]), ("SMEAR_IN", "SMEAR", ix[8]),
                          ("DIV_IN", "DIV", ix[9])])
    jack_row(p, JACKS_2, [("SEED_IN", "SEED", ix[0])])
    p.outbox(ix[6] - 7.5, JACKS_2 - 9.2, p.w - 3.0 - (ix[6] - 7.5), 14.6)
    jack_row(p, JACKS_2, [("L_OUT", "L", ix[6]), ("R_OUT", "R", ix[7]), ("SLICE_OUT", "SLICE", ix[8]),
                          ("REPEAT_OUT", "RPT", ix[9])], fill=TEAL)
    p.save()


def depth():
    p = Panel("Depth", 24, "DEPTH", TEAL, "four sources in one room")
    # one column per source
    cx = [30.0, 45.0, 60.0, 75.0]
    rows = [("DIST", "knob", 31.0, "DIST"), ("PAN", "small", 48.0, "PAN"), ("DIST_IN", "jack", 62.5, "DIST CV"),
            ("L_IN", "jack", 76.0, "IN L"), ("R_IN", "jack", 89.5, "IN R")]
    for i, x in enumerate(cx):
        p.label(str(i + 1), x, 21.5, fill=TEAL)
    for name, kind, y, label in rows:
        p.place(name, kind, [(x, y) for x in cx])
        p.label(label, 5.0, y + 1.2, anchor="start", jack=kind == "jack")
    p.line(84.5, 17.0, 84.5, 94.0)

    # the room
    gx = [96.0, 112.5]
    gy = [31.0, 52.0, 73.0]
    knob_row(p, gy[0], [("SIZE", "SIZE", gx[0]), ("DECAY", "DECAY", gx[1])])
    knob_row(p, gy[1], [("DAMP", "DAMP", gx[0]), ("MOD", "MOD", gx[1])])
    p.place("INSTABILITY", "knob", (gx[0], gy[2]))
    p.label("INSTAB", gx[0], gy[2] - KNOB_LABEL, fill=TEAL)
    knob_row(p, gy[2], [("MIX", "MIX", gx[1])])

    ix = [11.0 + i * 14.5 for i in range(9)]
    jack_row(p, JACKS_1, [("SIZE_IN", "SIZE", ix[0]), ("DECAY_IN", "DECAY", ix[1]), ("INSTABILITY_IN", "INSTAB", ix[2]),
                          ("MOD_IN", "MOD", ix[3]), ("FREEZE_IN", "FREEZE", ix[4])])
    p.outbox(ix[4] - 7.5, JACKS_2 - 9.2, p.w - 3.0 - (ix[4] - 7.5), 14.6)
    jack_row(p, JACKS_2, [("L_OUT", "OUT L", ix[4]), ("R_OUT", "OUT R", ix[5]), ("WET_L_OUT", "WET L", ix[6]),
                          ("WET_R_OUT", "WET R", ix[7])], fill=TEAL)
    p.save()


def vault():
    p = Panel("Vault", 40, "VAULT", VIOLET, "keep the good bars, play them later")
    p.screen("DISPLAY", 5.0, 15.0, 80.0, 44.0)

    xs = [13.0 + i * 16.5 for i in range(5)]
    knob_row(p, ROW_A, [("TEMPO", "TEMPO", xs[0]), ("BARS", "BARS", xs[1])])
    knob_row(p, ROW_A, [("KEEP", "KEEP", xs[2])], kind="button")
    knob_row(p, ROW_A, [("SEED", "SEED", xs[4])])
    # these four act on one playback lane
    for name, label, x in [("LANE", "LANE", xs[0]), ("CLIP", "CLIP", xs[1]), ("THIN", "THIN", xs[2]),
                           ("TRANSPOSE", "TRANS", xs[3])]:
        p.place(name, "knob", (x, ROW_B))
        p.label(label, x, ROW_B - KNOB_LABEL, fill=VIOLET)
    p.line(xs[0] + 5.6, ROW_B, xs[1] - 5.6, ROW_B, VIOLET)
    p.note("FOR THAT", xs[4] - 7.0, ROW_B - 2.0, anchor="start", fill=VIOLET)
    p.note("LANE", xs[4] - 7.0, ROW_B + 1.8, anchor="start", fill=VIOLET)

    ix = [11.0 + i * 13.6 for i in range(6)]
    jack_row(p, JACKS_1, [("PHASE_IN", "PHASE", ix[0]), ("KEEP_IN", "KEEP", ix[1]), ("CLIP_IN", "CLIP", ix[2]),
                          ("THIN_IN", "THIN", ix[3]), ("TRANSPOSE_IN", "TRANS", ix[4]), ("STOP_IN", "STOP", ix[5])])

    # one column per lane: record inputs, launch, play outputs
    cx = [108.0 + i * 12.2 for i in range(8)]
    lx = 89.0
    for i, x in enumerate(cx):
        p.label(str(i + 1), x, 21.5, fill=VIOLET)
    p.outbox(lx - 2.0, JACKS_1 - 6.2, p.w - 3.0 - (lx - 2.0), 26.6)
    p.outbox(ix[5] - 7.5, JACKS_2 - 9.2, 15.0, 14.6)
    jack_row(p, JACKS_2, [("END_OUT", "END", ix[5])], fill=VIOLET)
    rows = [("GATE_IN", "jack", 31.0, "GATE", TEXT), ("CV_IN", "jack", 45.0, "CV", TEXT),
            ("LAUNCH", "button", 57.5, "LAUNCH", TEXT), ("LAUNCH_IN", "jack", 68.5, "", TEXT),
            ("PLAY_LIGHT", "light", 82.0, "PLAYING", TEXT),
            ("GATE_OUT", "jack", JACKS_1 + 1.0, "GATE", VIOLET), ("CV_OUT", "jack", JACKS_2, "CV", VIOLET)]
    for name, kind, y, label, fill in rows:
        p.place(name, kind, [(x, y) for x in cx])
        if label:
            p.label(label, lx, y + 1.1, anchor="start", jack=True, fill=fill)
    p.note("RECORD IN", lx, 24.0, anchor="start")
    p.note("TRIGGER", lx, 69.4, anchor="start")
    p.note("PLAY OUT", lx, JACKS_1 - 8.0, anchor="start", fill=VIOLET)
    p.line(lx - 2.5, 17.0, lx - 2.5, 94.0)
    p.save()


if __name__ == "__main__":
    warp()
    contour()
    mutate()
    strike()
    fmx()
    chain()
    splice()
    depth()
    vault()
