"""The app's look (1.2.0): a dark theme over ttk's clam, a header drawn on a canvas, cards for the main choices and
key caps for the co-op keys. Only Tk and fonts every Windows 10/11 PC has (Bahnschrift, Segoe UI, Consolas); other
systems fall back to Tk's defaults. Nothing here decides anything: tools/coop_app.py builds the screens."""
from __future__ import annotations

import os
import random

BG = "#0d0e10"          # the window
PANEL = "#16181b"       # cards, fields
PANEL_HI = "#1e2125"    # a card or button under the mouse
LINE = "#2b2e34"        # borders
TEXT = "#ebe7e1"        # warm off-white
MUTED = "#9a9ea6"
FAINT = "#62666e"
ACCENT = "#b3261e"      # dried-blood red
ACCENT_HI = "#d33a2f"
ACCENT_LO = "#7c1a15"
LOG_BG = "#0a0b0c"
LOG_TEXT = "#b7b2ab"

FONTS = {}   # filled by apply(): display, title, heading, body, small, label, button, mono, key
IMAGES = []  # the indicator images: Tk drops an image nothing in Python refers to


def pick(families, wanted, fallback):
    return next((name for name in wanted if name in families), fallback)


def tracked(text: str) -> str:
    """Letter spacing for small capitals: a thin space between letters, a wider gap between words."""
    return "  ".join(" ".join(word) for word in text.split())


def rgb(colour: str):
    return tuple(int(colour[i:i + 2], 16) for i in (1, 3, 5))


def segment(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return ((px - ax - t * dx) ** 2 + (py - ay - t * dy) ** 2) ** .5


def indicator(window, size: int, margin: int, shape: str, fill: str, border: str, mark: str | None):
    """A check box or radio button indicator, drawn with 4x4 supersampling so its edges are smooth at any scale.
    shape 'box' (a rounded square; mark draws a tick) or 'dot' (a circle; mark draws the centre dot). The image is
    margin pixels wider, filled with the window colour, to space it from its label."""
    import tkinter as tk
    image = tk.PhotoImage(master=window, width=size + margin, height=size)
    back, inside, edge = rgb(BG), rgb(fill), rgb(border)
    ink = rgb(mark) if mark else None
    half, line = size / 2, max(1.0, size / 13)
    rows = []
    for y in range(size):
        row = []
        for x in range(size + margin):
            total = [0, 0, 0]
            for sy in range(4):
                for sx in range(4):
                    px, py = x + (sx + .5) / 4 - half, y + (sy + .5) / 4 - half
                    if shape == "box":
                        r, b = size * .2, half - .5
                        qx, qy = abs(px) - b + r, abs(py) - b + r
                        d = (max(qx, 0) ** 2 + max(qy, 0) ** 2) ** .5 + min(max(qx, qy), 0) - r
                    else:
                        d = (px * px + py * py) ** .5 - (half - .5)
                    colour = back
                    if d < 0:
                        colour = edge if d > -line else inside
                        if ink and shape == "box":
                            u, v = (px + half) / size, (py + half) / size
                            if min(segment(u, v, .26, .52, .43, .69), segment(u, v, .43, .69, .75, .33)) < .085:
                                colour = ink
                        elif ink and (px * px + py * py) ** .5 < size * .2:
                            colour = ink
                    total = [t + c for t, c in zip(total, colour)]
            row.append("#%02x%02x%02x" % tuple(round(t / 16) for t in total))
        rows.append("{" + " ".join(row) + "}")
    image.put(" ".join(rows))
    IMAGES.append(image)
    return image


def indicators(window, style) -> None:
    """Replaces clam's check box (a cross) and radio button with smooth ones in the theme's colours."""
    scale = max(1.0, window.winfo_fpixels("1i") / 96.0)
    size, margin = round(15 * scale), round(9 * scale)
    for shape, name in (("box", "Check"), ("dot", "Radio")):
        def make(fill, border, mark):
            return indicator(window, size, margin, shape, fill, border, mark)
        off, hover = make(PANEL, LINE, None), make(PANEL_HI, "#4a4e56", None)
        on = make(ACCENT, ACCENT, "#ffffff") if shape == "box" else make(PANEL, ACCENT, ACCENT_HI)
        on_hover = make(ACCENT_HI, ACCENT_HI, "#ffffff") if shape == "box" else make(PANEL_HI, ACCENT_HI, ACCENT_HI)
        dead = make(BG, "#24272c", None)
        style.element_create(f"Coop.{name}.indicator", "image", off, ("disabled", dead), ("selected", "active", on_hover),
                             ("selected", on), ("active", hover), sticky="")
        widget = "Checkbutton" if name == "Check" else "Radiobutton"
        style.layout(f"T{widget}", [(f"{widget}.padding", {"sticky": "nswe", "children": [
            (f"Coop.{name}.indicator", {"side": "left", "sticky": ""}),
            (f"{widget}.focus", {"side": "left", "sticky": "w", "children": [(f"{widget}.label", {"sticky": "nswe"})]})]})])


def apply(window) -> None:
    """Styles every ttk widget the app uses. Called once, before the first screen."""
    import tkinter.font as tkfont
    from tkinter import ttk
    families = set(tkfont.families(window))
    display = pick(families, ("Bahnschrift SemiBold Condensed", "Bahnschrift SemiBold", "Segoe UI Semibold"), "TkHeadingFont")
    heading = pick(families, ("Bahnschrift SemiBold", "Segoe UI Semibold"), "TkHeadingFont")
    body = pick(families, ("Segoe UI", "Helvetica"), "TkDefaultFont")
    strong = pick(families, ("Segoe UI Semibold", "Segoe UI"), "TkDefaultFont")
    mono = pick(families, ("Cascadia Mono", "Consolas", "Courier New"), "TkFixedFont")
    FONTS.update(display=(display, 26), title=(heading, 19), heading=(heading, 13), body=(body, 10),
                 small=(body, 9), label=(strong, 8), button=(strong, 10), mono=(mono, 9), key=(mono, 9, "bold"))
    window.configure(background=BG)
    window.option_add("*TCombobox*Listbox.background", PANEL)
    window.option_add("*TCombobox*Listbox.foreground", TEXT)
    window.option_add("*TCombobox*Listbox.selectBackground", ACCENT)
    window.option_add("*TCombobox*Listbox.selectForeground", "#ffffff")
    window.option_add("*TCombobox*Listbox.font", FONTS["body"])
    window.option_add("*TCombobox*Listbox.borderWidth", 0)
    style = ttk.Style(window)
    style.theme_use("clam")
    style.configure(".", background=BG, foreground=TEXT, font=FONTS["body"], bordercolor=LINE, darkcolor=BG,
                    lightcolor=BG, troughcolor=PANEL, focuscolor=ACCENT, selectbackground=ACCENT,
                    selectforeground="#ffffff", fieldbackground=PANEL, insertcolor=TEXT)
    style.configure("TFrame", background=BG)
    style.configure("Card.TFrame", background=PANEL)
    style.configure("TLabel", background=BG, foreground=TEXT, font=FONTS["body"])
    style.configure("Title.TLabel", font=FONTS["title"])
    style.configure("Sub.TLabel", foreground=MUTED)
    style.configure("Hint.TLabel", foreground=MUTED, font=FONTS["small"])
    style.configure("Faint.TLabel", foreground=FAINT, font=FONTS["small"])
    style.configure("Section.TLabel", foreground=FAINT, font=FONTS["label"])
    style.configure("Field.TLabel", foreground=MUTED)
    style.configure("Offer.TLabel", background=PANEL, font=FONTS["heading"])
    style.configure("OfferText.TLabel", background=PANEL, foreground=MUTED)
    # buttons: the main action is red, the rest quiet
    style.configure("TButton", background=PANEL_HI, foreground=TEXT, bordercolor=LINE, lightcolor=PANEL_HI,
                    darkcolor=PANEL_HI, relief="flat", padding=(14, 7), font=FONTS["button"], focusthickness=0)
    style.map("TButton", background=[("disabled", PANEL), ("pressed", LINE), ("active", "#272a2f")],
              foreground=[("disabled", FAINT)], bordercolor=[("focus", ACCENT), ("active", "#3a3e45")],
              lightcolor=[("pressed", LINE), ("active", "#272a2f")], darkcolor=[("pressed", LINE), ("active", "#272a2f")])
    style.configure("Accent.TButton", background=ACCENT, foreground="#ffffff", bordercolor=ACCENT, lightcolor=ACCENT,
                    darkcolor=ACCENT, padding=(22, 9))
    style.map("Accent.TButton", background=[("disabled", "#3a1f1d"), ("pressed", ACCENT_LO), ("active", ACCENT_HI)],
              foreground=[("disabled", "#8d7774")], bordercolor=[("disabled", "#3a1f1d"), ("focus", "#f0d6d3"),
                                                                  ("pressed", ACCENT_LO), ("active", ACCENT_HI)],
              lightcolor=[("disabled", "#3a1f1d"), ("pressed", ACCENT_LO), ("active", ACCENT_HI)],
              darkcolor=[("disabled", "#3a1f1d"), ("pressed", ACCENT_LO), ("active", ACCENT_HI)])
    style.configure("Ghost.TButton", background=BG, foreground=MUTED, bordercolor=BG, lightcolor=BG, darkcolor=BG,
                    padding=(10, 5), font=FONTS["small"])
    style.map("Ghost.TButton", background=[("pressed", PANEL), ("active", PANEL)], foreground=[("active", TEXT)],
              bordercolor=[("focus", LINE), ("active", PANEL)], lightcolor=[("active", PANEL)],
              darkcolor=[("active", PANEL)])
    # fields
    for name in ("TEntry", "TCombobox", "TSpinbox"):
        style.configure(name, fieldbackground=PANEL, foreground=TEXT, bordercolor=LINE, lightcolor=PANEL,
                        darkcolor=PANEL, insertcolor=TEXT, padding=(8, 6), arrowcolor=MUTED, background=PANEL_HI,
                        selectbackground=ACCENT, selectforeground="#ffffff", borderwidth=1, relief="flat")
        style.map(name, bordercolor=[("focus", ACCENT), ("hover", "#3a3e45")],
                  lightcolor=[("focus", PANEL)], fieldbackground=[("readonly", PANEL), ("disabled", BG)],
                  foreground=[("disabled", FAINT)], arrowcolor=[("hover", TEXT)],
                  selectbackground=[("readonly", PANEL)], selectforeground=[("readonly", TEXT)])
    for name in ("TCheckbutton", "TRadiobutton"):
        style.configure(name, background=BG, foreground=TEXT, padding=(0, 3), focuscolor=BG)
        style.map(name, background=[("active", BG)], foreground=[("disabled", FAINT), ("focus", "#ffffff")])
    indicators(window, style)
    style.configure("Horizontal.TProgressbar", troughcolor=PANEL, background=ACCENT, bordercolor=BG, lightcolor=ACCENT,
                    darkcolor=ACCENT, thickness=6)
    style.configure("Vertical.TScrollbar", troughcolor=LOG_BG, background=PANEL_HI, bordercolor=LOG_BG,
                    lightcolor=PANEL_HI, darkcolor=PANEL_HI, arrowcolor=MUTED, gripcount=0)
    style.map("Vertical.TScrollbar", background=[("active", LINE)])
    style.configure("Line.TFrame", background=LINE)


def dark_title_bar(window) -> None:
    """Windows 10 (20H1+) and 11 draw the title bar dark when asked (DWMWA_USE_IMMERSIVE_DARK_MODE)."""
    if os.name != "nt":
        return
    try:
        import ctypes
        window.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(int(window.winfo_id()))   # int(): ctypes must never see a non-number
        on = ctypes.c_int(1)
        for attribute in (20, 19):   # 19 before Windows 10 20H1
            if ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attribute, ctypes.byref(on), ctypes.sizeof(on)) == 0:
                break
    except (AttributeError, OSError, TypeError, ValueError):
        pass


def mix(a: str, b: str, t: float) -> str:
    ar, ag, ab = (int(a[i:i + 2], 16) for i in (1, 3, 5))
    br, bg, bb = (int(b[i:i + 2], 16) for i in (1, 3, 5))
    return f"#{round(ar + (br - ar) * t):02x}{round(ag + (bg - ag) * t):02x}{round(ab + (bb - ab) * t):02x}"


class Header:
    """The band at the top of every screen: a dark red-to-black wash with film grain and the name (the version is
    in the footer)."""
    HEIGHT = 92

    def __init__(self, tk, parent):
        self.canvas = tk.Canvas(parent, height=self.HEIGHT, background=BG, highlightthickness=0, borderwidth=0)
        self.canvas.pack(fill="x", side="top")
        self.width = 0
        self.canvas.bind("<Configure>", self.draw)

    def draw(self, event=None):
        width = event.width if event else self.canvas.winfo_width()
        if width == self.width:
            return
        self.width = width
        c, height = self.canvas, self.HEIGHT
        c.delete("all")
        step = 4
        for x in range(0, width, step):   # the wash: blood at the left fading to the window's black
            t = min(1.0, x / max(1, width * .75))
            c.create_rectangle(x, 0, x + step, height, fill=mix("#2a1112", "#101113", t ** .8), width=0)
        grain = random.Random(1109)   # the same grain on every draw
        for _ in range(width * height // 60):
            x, y = grain.randrange(width), grain.randrange(height)
            shade = grain.choice(("#1d1c1d", "#262223", "#0a0a0b", "#301a1a"))
            c.create_rectangle(x, y, x + 1, y + 1, fill=shade, width=0)
        for x in range(0, width, step):   # the rule under it: red, fading out to the right
            c.create_rectangle(x, height - 2, x + step, height, fill=mix(ACCENT, BG, min(1.0, x / max(1, width * .9))),
                               width=0)
        c.create_text(26, 38, text="CRY OF FEAR", anchor="w", fill=TEXT, font=FONTS.get("display"))
        c.create_text(28, 66, text=tracked("STORY CO-OP"), anchor="w", fill=ACCENT_HI, font=FONTS.get("label"))


class Card:
    """A main-screen choice: a title, one line on what it does and an arrow; the whole card is the button (mouse,
    Tab then Enter or Space)."""

    def __init__(self, tk, parent, title: str, text: str, command):
        self.command = command
        self.frame = tk.Frame(parent, background=PANEL, highlightthickness=1, highlightbackground=LINE,
                              highlightcolor=ACCENT, takefocus=1, cursor="hand2")
        self.frame.pack(fill="x", pady=4)
        self.bar = tk.Frame(self.frame, background=PANEL, width=3)
        self.bar.pack(side="left", fill="y")
        self.arrow = tk.Label(self.frame, text="›", background=PANEL, foreground=FAINT, font=(FONTS["title"][0], 22))
        self.arrow.pack(side="right", padx=(0, 18))
        inner = tk.Frame(self.frame, background=PANEL)
        inner.pack(side="left", fill="both", expand=True, padx=(14, 0), pady=7)   # 1.3.3: a fourth card fits
        self.title = tk.Label(inner, text=title, background=PANEL, foreground=TEXT, font=FONTS["heading"], anchor="w")
        self.title.pack(fill="x")
        self.text = tk.Label(inner, text=text, background=PANEL, foreground=MUTED, font=FONTS["small"], anchor="w")
        self.text.pack(fill="x")
        self.parts = (self.frame, inner, self.title, self.text, self.arrow)
        for part in self.parts:
            part.bind("<Enter>", lambda _: self.lit(True))
            part.bind("<Leave>", lambda _: self.lit(self.frame.focus_get() is self.frame))
            part.bind("<Button-1>", lambda _: self.invoke())
        self.frame.bind("<FocusIn>", lambda _: self.lit(True))
        self.frame.bind("<FocusOut>", lambda _: self.lit(False))
        self.frame.bind("<Return>", lambda _: self.invoke())
        self.frame.bind("<space>", lambda _: self.invoke())

    def lit(self, on: bool):
        colour = PANEL_HI if on else PANEL
        for part in self.parts:
            part.configure(background=colour)
        self.bar.configure(background=ACCENT if on else PANEL)
        self.arrow.configure(foreground=ACCENT_HI if on else FAINT)

    def invoke(self):
        self.command()


def keycaps(tk, parent, keys) -> None:
    """The co-op keys as caps: (key, what it does) pairs, two to a row."""
    grid = tk.Frame(parent, background=BG)
    grid.pack(anchor="w", fill="x")
    for index, (key, text) in enumerate(keys):
        cell = tk.Frame(grid, background=BG)
        cell.grid(row=index // 2, column=index % 2, sticky="w", padx=(0, 26), pady=3)
        tk.Label(cell, text=key, background=PANEL_HI, foreground=TEXT, font=FONTS["key"], padx=7, pady=1,
                 highlightthickness=1, highlightbackground=LINE).pack(side="left")
        tk.Label(cell, text=text, background=BG, foreground=MUTED, font=FONTS["small"]).pack(side="left", padx=(8, 0))


def section(ttk, parent, text: str, top: int = 12):
    ttk.Label(parent, text=tracked(text.upper()), style="Section.TLabel").pack(anchor="w", pady=(top, 4))
