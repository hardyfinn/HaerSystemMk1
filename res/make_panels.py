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


def text(s, x, y, size, font=FONT_BOLD, fill=TEXT, anchor="middle", spacing=0.06):
    """An SVG path for string s. (x, y) is the baseline point in mm."""
    if font not in _fonts:
        f = TTFont(font)
        _fonts[font] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    gs, cmap, upm = _fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = {"middle": x - width * scale / 2, "end": x - width * scale}.get(anchor, x)
    pen = SVGPathPen(gs, ntos=lambda v: f"{v:.3f}".rstrip("0").rstrip("."))
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


class Panel:
    def __init__(self, slug, hp, title, accent):
        self.slug, self.w, self.accent = slug, hp * HP, accent
        self.parts = [f'<rect width="{self.w:.2f}" height="{H}" fill="{PANEL}"/>']
        # accent bar and title along the top, maker's name along the bottom
        self.parts.append(f'<rect width="{self.w:.2f}" height="1.6" fill="{accent}"/>')
        self.add(text(title, self.w / 2, 9.6, TITLE, fill=accent, spacing=0.14))
        self.add(text("Haer", self.w / 2, H - 4.2, 2.9, fill=DIM, spacing=0.1))

    def add(self, s):
        self.parts.append(s)

    def label(self, s, x, y, jack=False, fill=TEXT, anchor="middle"):
        self.add(text(s, x, y, JACK_LABEL if jack else CTRL_LABEL, fill=fill, anchor=anchor))

    def note(self, s, x, y, size=2.4, fill=DIM, anchor="middle"):
        self.add(text(s, x, y, size, font=FONT_MED, fill=fill, anchor=anchor, spacing=0.03))

    def line(self, x1, y1, x2, y2, color=LINE, w=0.3):
        self.add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{color}" stroke-width="{w}"/>')

    def screen(self, x, y, w, h):
        self.add(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="1" fill="{SCREEN}"/>')

    def outbox(self, x, y, w, h):
        """A lighter box behind output jacks, so outputs read at a glance."""
        self.add(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="1.2" fill="#2b2b30"/>')

    def save(self):
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w:.2f}mm" height="{H}mm" '
               f'viewBox="0 0 {self.w:.2f} {H}">\n' + "\n".join(self.parts) + "\n</svg>\n")
        path = os.path.join(HERE, self.slug + ".svg")
        with open(path, "w") as f:
            f.write(svg)
        print("wrote", path)


def blank():
    p = Panel("Blank", 6, "CODEX", VIOLET)
    # three stripes, one per layer colour
    for i, (c, name) in enumerate(((AMBER, "TIME"), (TEAL, "SOUND"), (VIOLET, "SYSTEM"))):
        y = 44 + i * 16
        p.add(f'<rect x="5" y="{y}" width="{p.w - 10:.2f}" height="1.2" rx="0.6" fill="{c}"/>')
        p.label(name, p.w / 2, y + 6.4, fill=c)
    p.note("milestone 0", p.w / 2, 104)
    p.note("placeholder", p.w / 2, 108)
    p.save()


if __name__ == "__main__":
    blank()
