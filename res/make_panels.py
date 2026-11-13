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
PART_R = {"knob": 5.1, "small": 4.1, "trim": 3.1, "jack": 4.2, "button": 3.1, "switch3": (2.6, 5.0), "light": 1.1}


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


if __name__ == "__main__":
    warp()
    contour()
