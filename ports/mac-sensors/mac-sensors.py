#!/usr/bin/python3
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# Copyright 2026 isoriano1968

"""
Mac Sensors: temperatures and fans on a drawing of the machine.

Data sources:
  - the Apple SMC through `smcstat -p` (applesmc(4D)): temperatures, fans;
  - illumos ksensors under /dev/sensors (coretemp, pchtemp, ...), read with
    SENSOR_IOCTL_SCALAR: works on any machine (needs read access).
Fan presets (Auto, Cool, Cooler, Max) through smcfan(8), run with pfexec(1)
and the rights profile "Apple SMC Fan Control"; hidden where smcfan cannot
set the fans (no SMC, or a driver without fan control).
Profiles by SMBIOS product name: MacPro6,1, iMac*, Sun Ultra 24; others get
cards without a drawing.  All drawings are our own; no Apple artwork.
"""

import collections
import fcntl
import getpass
import math
import os
import re
import struct
import subprocess
import sys

import cairo
import gi
gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gdk, GLib, Gio  # noqa: E402

APP_ID = "org.openindiana.MacSensors"
REFRESH_S = 2
HISTORY = 300			# samples: 10 minutes at 2 s

# ------------------------------------------------------------------ colours

BG, PANEL, CARD, TRACK = "#1e1e1e", "#262629", "#2a2a2e", "#45454b"
COOL, NORMAL, WARM, HOT = "#62a0ea", "#f9f06b", "#ffa348", "#f66151"


def rgb(hexstr, a=1.0):
    h = hexstr.lstrip("#")
    return (int(h[0:2], 16) / 255, int(h[2:4], 16) / 255,
            int(h[4:6], 16) / 255, a)


def level_color(value, limit):
    f = value / limit if limit else 0
    if f < 0.45:
        return COOL
    if f < 0.60:
        return NORMAL
    if f < 0.90:
        return WARM
    return HOT


CSS = """
window.macsensors { background: %(bg)s; color: #ffffff; }
.macsensors headerbar { background: #2e2e32; color: #ffffff; }
.title { font-weight: bold; }
.subtitle { font-size: 0.85em; color: #c0bfbc; }
.pill { background: #3a3a3f; border-radius: 999px; padding: 4px 12px;
        font-size: 0.85em; color: #deddda; }
.panel { background: %(panel)s; border-radius: 16px; padding: 16px; }
.card { background: %(card)s; border-radius: 14px; padding: 14px 18px; }
.card.hot { background: #3a2a2b; border: 1px solid %(hot)s; }
.cardtitle { font-weight: bold; }
.dim { color: #c0bfbc; font-size: 0.85em; }
.small { color: #c0bfbc; font-size: 0.8em; }
.big { font-size: 30pt; font-weight: bold; }
.unit { font-size: 13pt; color: #deddda; }
.caps { font-size: 0.85em; font-weight: bold; letter-spacing: 1px; color: #c0bfbc; }
.ok { color: #8ff0a4; font-size: 0.85em; }
.keys { font-size: 0.85em; color: #deddda; }
.over { color: %(hot)s; font-weight: bold; }
.presets button { padding: 2px 10px; }
.presets button:checked { background: #1c71d8; color: #ffffff; }
.warnmsg { color: #ffa348; font-size: 0.8em; }
""" % {"bg": BG, "panel": PANEL, "card": CARD, "hot": HOT}

# ------------------------------------------------------------------ data

SENSOR_IOCTL_SCALAR = ((ord('s') << 24) | (ord('e') << 16) |
                       (ord('n') << 8)) | 0x02
SCALAR_FMT = "=IiIIq"		# unit, gran, prec, pad, value (sys/sensors.h)


def read_ksensors():
    out = {}
    base = "/dev/sensors/temperature"
    if not os.path.isdir(base):
        return out
    for cls in sorted(os.listdir(base)):
        d = os.path.join(base, cls)
        if cls == "smc" or not os.path.isdir(d):
            continue		# SMC values come through smcstat
        for name in sorted(os.listdir(d)):
            buf = bytearray(struct.calcsize(SCALAR_FMT))
            try:
                fd = os.open(os.path.join(d, name), os.O_RDONLY)
                try:
                    fcntl.ioctl(fd, SENSOR_IOCTL_SCALAR, buf)
                finally:
                    os.close(fd)
            except OSError:
                continue
            unit, gran, _p, _x, value = struct.unpack(SCALAR_FMT, buf)
            if unit != 1 or gran == 0:
                continue
            out["%s/%s" % (cls, name)] = value / gran if gran > 0 else value * -gran
    return out


def read_smc():
    try:
        res = subprocess.run(["/usr/bin/smcstat", "-p"], capture_output=True,
                             text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return {}, []
    if res.returncode != 0:
        return {}, []
    temps, fans = {}, []
    for line in res.stdout.splitlines():
        f = line.split(":")
        try:
            if f[0] == "temp" and len(f) == 3:
                v = float(f[2])
                if 5.0 <= v < 130.0:
                    temps[f[1]] = v
            elif f[0] == "fan" and len(f) >= 7:
                fans.append({"id": int(f[1]), "rpm": float(f[2]),
                             "target": float(f[3]), "min": float(f[4]),
                             "max": float(f[5]),
                             "name": ":".join(f[6:]).strip()})
        except ValueError:
            continue
    return temps, fans


SMCFAN = "/usr/sbin/smcfan"
PFEXEC = "/usr/bin/pfexec"
FAN_PROFILE = "Apple SMC Fan Control"
PRESETS = (("auto", "Auto"), ("cool", "Cool"), ("cooler", "Cooler"),
           ("max", "Max"))
SMCFAN_NOTSUP, SMCFAN_PERM = 3, 4	# smcfan(8) exit status


def read_fan_preset():
    """The active fan preset from the `preset:` line of smcfan(8): a name
    of PRESETS or "manual"; None where the fans cannot be set."""
    if not os.access(SMCFAN, os.X_OK):
        return None
    try:
        res = subprocess.run([SMCFAN], capture_output=True, text=True,
                             timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    if res.returncode != 0:
        return None
    for line in res.stdout.splitlines():
        if line.startswith("preset:"):
            return line.split(":", 1)[1].strip()
    return None


def read_model():
    try:
        out = subprocess.run(["/usr/sbin/smbios", "-t", "SMB_TYPE_SYSTEM"],
                             capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return ""
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("Product:"):
            return line.split(":", 1)[1].strip()
    return ""


def read_cpu_name():
    try:
        out = subprocess.run(["/usr/bin/kstat", "-p", "cpu_info:0:cpu_info0:brand"],
                             capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return ""
    name = out.split("\t", 1)[-1].strip()
    name = re.sub(r"\((?i:r|tm)\)|\bCPU\b|@.*", "", name)
    return " ".join(name.replace("Intel", "").split())


# ------------------------------------------------------------------ values

class Snapshot:
    def __init__(self, smc, ks, fans):
        self.smc, self.ks, self.fans = smc, ks, fans

    def key(self, *keys):
        for k in keys:
            if k in self.smc:
                return self.smc[k]
        return None

    def smc_max(self, pattern):
        vals = [v for k, v in self.smc.items() if re.match(pattern, k)]
        return max(vals) if vals else None

    def smc_range(self, pattern):
        vals = [v for k, v in self.smc.items() if re.match(pattern, k)]
        return (min(vals), max(vals)) if vals else None

    def ks_max(self, cls):
        vals = [v for k, v in self.ks.items() if k.startswith(cls + "/")]
        return max(vals) if vals else None


def fmt(v, unit="°C"):
    return "—" if v is None else "%.0f %s" % (v, unit)


# Card definitions: (id, title, subtitle, limit, value(snapshot), detail(snapshot))
def cards_for(profile, cpu_name, keys=()):
    def cores(s):
        r = s.smc_range(r"^TC\dc$")
        return "cores %.0f–%.0f °C" % r if r else ""

    common_cpu = ("cpu", "CPU", cpu_name or "processor", 100,
                  lambda s: s.key("TCXc", "TCXC", "TC0D", "TC0P") or s.ks_max("cpu"),
                  cores)
    if profile == "MacPro6,1":
        return [
            common_cpu,
            ("gpua", "GPU A", "FirePro D500 · display", 95,
             lambda s: s.key("TG0D", "TG0P"), lambda s: "proximity " + fmt(s.key("TG0P"))),
            ("gpub", "GPU B", "FirePro D500 · compute", 95,
             lambda s: s.key("TG1D", "TG1P"), lambda s: "proximity " + fmt(s.key("TG1P"))),
            ("mem", "Memory", "hottest DIMM", 85,
             lambda s: s.smc_max(r"^TM"), lambda s: "proximity " + fmt(s.key("TM0P"))),
            ("pch", "Chipset", "PCH die", 100,
             lambda s: s.key("TPCD") or s.ks_max("pch"),
             lambda s: "Thunderbolt " + fmt(s.key("TI0P"))),
        ]
    if profile == "iMac":
        # The iMacs differ: discrete GPU (TG..) or graphics in the CPU (TCGc,
        # iMac14,1), a drive sensor (TH..) or none, PSU keys Tp0.. or Tp2..
        # (iMac14,1).  Chosen from the keys the SMC has at start.
        cards = [common_cpu]
        if any(k.startswith("TG") for k in keys):
            cards.append(("gpu", "GPU", "graphics", 95,
                          lambda s: s.key("TG0D", "TG0P", "TG0p"),
                          lambda s: "proximity " + fmt(s.key("TG0P", "TG0p"))))
        else:
            cards.append(("gpu", "GPU", "integrated · in the CPU", 100,
                          lambda s: s.key("TCGc", "TCGC"),
                          lambda s: "system agent " + fmt(s.key("TCSc", "TCSC"))))
        cards.append(("lcd", "Display", "LCD proximity", 60,
                      lambda s: s.key("TL0P", "TL0p"),
                      lambda s: "panel " + fmt(s.key("TL1P", "TL1p"))
                      if s.key("TL1P", "TL1p") is not None else ""))
        if any(re.match(r"^TH\d[PFVpa]$", k) for k in keys):
            cards.append(("hdd", "Drive", "HDD / SSD", 60,
                          lambda s: s.key("TH0P", "TH0F", "TH0V", "TH0p", "TH0a"),
                          lambda s: ""))
        else:
            cards.append(("board", "Mainboard", "logic board", 90,
                          lambda s: s.smc_max(r"^Tm\dP$") or s.key("TM0P", "TM0p"),
                          lambda s: "memory " + fmt(s.key("TM0P", "TM0p", "TMXP"))))
        cards.append(("psu", "Power supply", "PSU", 80,
                      lambda s: s.key("Tp0P", "Tp0C", "Tp0T", "Tp2H", "Tp2F"),
                      lambda s: ""))
        return cards
    return [
        ("cpu", "CPU", cpu_name or "processor", 100,
         lambda s: s.ks_max("cpu") or s.key("TCXc", "TC0P"),
         lambda s: "%d sensors" % len([k for k in s.ks if k.startswith("cpu/")])),
        ("pch", "Chipset", "PCH", 100,
         lambda s: s.ks_max("pch") or s.key("TPCD"), lambda s: ""),
    ]


# SMBIOS product name -> name shown in the header
IMAC_NAMES = {
    "iMac12,1": "iMac (21.5-inch, Mid 2011)",
    "iMac12,2": "iMac (27-inch, Mid 2011)",
    "iMac13,1": "iMac (21.5-inch, Late 2012)",
    "iMac13,2": "iMac (27-inch, Late 2012)",
    "iMac14,1": "iMac (21.5-inch, Late 2013)",
    "iMac14,2": "iMac (27-inch, Late 2013)",
}

# hot spots on the drawings: card id -> [(x, y, r)] in drawing units
SPOTS = {
    "MacPro6,1": {"cpu": [(180, 356, 40)], "gpua": [(112, 300, 34)],
                  "gpub": [(248, 300, 34)], "mem": [(92, 456, 30), (268, 456, 30)],
                  "pch": [(180, 460, 26)]},
    "iMac": {"cpu": [(160, 150, 36)], "gpu": [(220, 150, 36)],
             "hdd": [(109, 297, 34)], "board": [(190, 215, 30)],
             "psu": [(272, 300, 34)], "lcd": [(190, 90, 30)]},
}

# ------------------------------------------------------------------ drawing


def lin(x0, y0, x1, y1, stops):
    g = cairo.LinearGradient(x0, y0, x1, y1)
    for off, col, a in stops:
        g.add_color_stop_rgba(off, *rgb(col, a))
    return g


def glow(cr, x, y, r, col):
    g = cairo.RadialGradient(x, y, 0, x, y, r)
    g.add_color_stop_rgba(0, *rgb(col, 0.95))
    g.add_color_stop_rgba(1, *rgb(col, 0))
    cr.set_source(g)
    cr.arc(x, y, r, 0, 2 * math.pi)
    cr.fill()


def ellipse(cr, cx, cy, rx, ry):
    cr.save()
    cr.translate(cx, cy)
    cr.scale(rx, ry)
    cr.arc(0, 0, 1, 0, 2 * math.pi)
    cr.restore()


def rrect(cr, x, y, w, h, r):
    cr.new_sub_path()
    cr.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    cr.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    cr.close_path()


def draw_macpro(cr, spots, angle):
    """Mac Pro (Late 2013), own drawing in a 360x600 box."""
    ellipse(cr, 180, 566, 132, 16)
    cr.set_source_rgba(0, 0, 0, 0.55)
    cr.fill()
    cr.set_source(lin(54, 0, 306, 0, [
        (0, "#0b0b0d", 1), (0.18, "#2c2d31", 1), (0.32, "#5b5d63", 1),
        (0.42, "#25262a", 1), (0.78, "#16171a", 1), (0.92, "#3b3c41", 1),
        (1, "#0b0b0d", 1)]))
    cr.move_to(54, 70)
    cr.line_to(54, 530)
    cr.save()
    cr.translate(180, 530)
    cr.scale(126, 30)
    cr.arc_negative(0, 0, 1, math.pi, 0)
    cr.restore()
    cr.line_to(306, 70)
    cr.close_path()
    cr.fill()
    ellipse(cr, 180, 70, 126, 30)
    cr.set_source_rgba(*rgb("#3d3e43"))
    cr.fill()
    cr.save()
    cr.translate(180, 70)
    cr.scale(1, 26 / 116)
    g = cairo.RadialGradient(0, 0, 0, 0, 0, 116)
    g.add_color_stop_rgba(0, *rgb("#1a1b1e"))
    g.add_color_stop_rgba(0.75, *rgb("#2a2b2f"))
    g.add_color_stop_rgba(1, *rgb("#55575c"))
    cr.arc(0, 0, 116, 0, 2 * math.pi)
    cr.set_source(g)
    cr.fill()
    cr.set_source_rgba(*rgb("#6b6d73", 0.9))
    cr.set_line_width(5)
    for i in range(11):
        a = angle + i * 2 * math.pi / 11
        cr.move_to(0, 0)
        cr.line_to(108 * math.cos(a), 108 * math.sin(a))
    cr.stroke()
    cr.restore()
    ellipse(cr, 180, 70, 22, 5)
    cr.set_source_rgba(*rgb("#1a1b1e"))
    cr.fill_preserve()
    cr.set_source_rgba(*rgb("#7a7c82"))
    cr.set_line_width(1)
    cr.stroke()

    rrect(cr, 70, 120, 220, 380, 10)
    cr.set_source_rgba(*rgb("#101012"))
    cr.fill_preserve()
    cr.set_source_rgba(*rgb("#4a4b50"))
    cr.stroke()
    cr.save()
    rrect(cr, 70, 120, 220, 380, 10)
    cr.clip()
    cr.move_to(180, 150)
    cr.line_to(96, 430)
    cr.line_to(264, 430)
    cr.close_path()
    cr.set_source(lin(0, 150, 0, 430, [(0, "#8e9096", 0.55), (1, "#5e6066", 0.35)]))
    cr.fill_preserve()
    cr.set_source_rgba(*rgb("#a7a9ae"))
    cr.set_line_width(1.2)
    cr.stroke()
    cr.set_source_rgba(*rgb("#c0c2c7", 0.35))
    cr.set_line_width(0.8)
    for y, half in ((250, 30), (290, 42), (330, 54), (370, 66), (405, 76)):
        cr.move_to(180 - half, y)
        cr.line_to(180 + half, y)
    cr.stroke()
    for cx, ang in ((89, -17), (271, 17)):		# GPU boards
        cr.save()
        cr.translate(cx, 314)
        cr.rotate(math.radians(ang))
        rrect(cr, -7, -118, 14, 236, 3)
        cr.set_source_rgba(*rgb("#1d4f2a"))
        cr.fill_preserve()
        cr.set_source_rgba(*rgb("#2f8a46"))
        cr.stroke()
        cr.restore()
    rrect(cr, 150, 440, 60, 40, 4)			# CPU board
    cr.set_source_rgba(*rgb("#1d4f2a"))
    cr.fill_preserve()
    cr.set_source_rgba(*rgb("#2f8a46"))
    cr.stroke()
    for x, y, r, col in spots:
        glow(cr, x, y, r, col)
    cr.restore()
    rrect(cr, 150, 512, 60, 10, 3)
    cr.set_source_rgba(*rgb("#2b2c30"))
    cr.fill()
    cr.set_source_rgba(*rgb(COOL, 0.7))
    cr.set_line_width(1.6)
    for x in (40, 320):
        d = -10 if x == 40 else 10
        cr.move_to(x, 560)
        cr.curve_to(x + d, 400, x + d, 250, x, 110)
        cr.move_to(x - 6, 120)
        cr.line_to(x, 106)
        cr.line_to(x + 6, 120)
    cr.stroke()


def draw_imac(cr, spots, angle):
    """iMac seen from behind, own drawing in a 380x560 box (dark style)."""
    ellipse(cr, 190, 532, 120, 10)
    cr.set_source_rgba(0, 0, 0, 0.45)
    cr.fill()
    alu = lin(0, 40, 380, 520, [(0, "#5c5e63", 1), (0.5, "#3a3b40", 1), (1, "#2a2b2f", 1)])
    cr.move_to(150, 400)
    cr.line_to(230, 400)
    cr.line_to(262, 520)
    cr.line_to(118, 520)
    cr.close_path()
    cr.set_source(alu)
    cr.fill()
    rrect(cr, 20, 40, 340, 370, 18)
    cr.set_source(alu)
    cr.fill()
    rrect(cr, 44, 64, 292, 300, 10)
    cr.set_source_rgba(*rgb("#141416"))
    cr.fill()
    rrect(cr, 120, 96, 140, 150, 6)
    cr.set_source_rgba(*rgb("#1d4f2a"))
    cr.fill()
    for x in (140, 200):
        rrect(cr, x, 130, 40, 40, 4)
        cr.set_source_rgba(*rgb("#3d3846"))
        cr.fill()
    cr.arc(290, 110, 30, 0, 2 * math.pi)
    cr.set_source_rgba(*rgb("#2c2d31"))
    cr.fill()
    cr.set_source_rgba(*rgb("#77767b"))
    cr.set_line_width(4)
    for i in range(6):
        a = angle + i * math.pi / 3
        cr.move_to(290, 110)
        cr.line_to(290 + 26 * math.cos(a), 110 + 26 * math.sin(a))
    cr.stroke()
    rrect(cr, 64, 262, 90, 70, 6)
    cr.set_source_rgba(*rgb("#4a4b50"))
    cr.fill()
    rrect(cr, 226, 262, 92, 76, 6)
    cr.set_source_rgba(*rgb("#3a3b40"))
    cr.fill()
    for x, y, r, col in spots:
        glow(cr, x, y, r, col)


class Machine(Gtk.DrawingArea):
    BOX = {"MacPro6,1": (360, 600, draw_macpro), "iMac": (380, 560, draw_imac)}

    def __init__(self, profile):
        super().__init__()
        self.profile = profile
        self.bw, self.bh, self.fn = self.BOX[profile]
        self.spots = []
        self.rpm = 0
        self.angle = 0.0
        self.set_size_request(360, 560)
        self.set_vexpand(True)
        self.set_draw_func(self.draw)
        GLib.timeout_add(60, self.tick)

    def tick(self):
        if self.rpm:
            # shown at 1/40 of the real speed so the eye can follow it
            self.angle = (self.angle + self.rpm / 60.0 / 40.0 * 0.06 * 2 * math.pi) % (2 * math.pi)
            self.queue_draw()
        return True

    def update(self, spots, rpm):
        self.spots, self.rpm = spots, rpm
        self.queue_draw()

    def draw(self, area, cr, w, h):
        s = min(w / self.bw, h / self.bh)
        cr.translate((w - self.bw * s) / 2, (h - self.bh * s) / 2)
        cr.scale(s, s)
        self.fn(cr, self.spots, self.angle)


class Bar(Gtk.DrawingArea):
    def __init__(self):
        super().__init__()
        self.frac, self.col = 0.0, COOL
        self.set_content_height(6)
        self.set_hexpand(True)
        self.set_draw_func(self.draw)

    def set(self, frac, col):
        self.frac, self.col = max(0.0, min(1.0, frac)), col
        self.queue_draw()

    def draw(self, area, cr, w, h):
        rrect(cr, 0, 0, w, h, h / 2)
        cr.set_source_rgba(*rgb(TRACK))
        cr.fill()
        if self.frac > 0:
            rrect(cr, 0, 0, max(h, w * self.frac), h, h / 2)
            cr.set_source_rgba(*rgb(self.col))
            cr.fill()


class Spark(Gtk.DrawingArea):
    def __init__(self):
        super().__init__()
        self.data, self.col = [], COOL
        self.set_content_height(34)
        self.set_hexpand(True)
        self.set_draw_func(self.draw)

    def set(self, data, col):
        self.data, self.col = list(data), col
        self.queue_draw()

    def draw(self, area, cr, w, h):
        pts = [v for v in self.data if v is not None]
        if len(pts) < 2:
            return
        lo, hi = min(pts) - 1.0, max(pts) + 1.0
        n = len(self.data)
        cr.set_source_rgba(*rgb(self.col))
        cr.set_line_width(1.6)
        first = True
        for i, v in enumerate(self.data):
            if v is None:
                continue
            x = w * i / max(1, n - 1)
            y = h - 2 - (h - 4) * (v - lo) / (hi - lo)
            if first:
                cr.move_to(x, y)
                first = False
            else:
                cr.line_to(x, y)
        cr.stroke()


class Card(Gtk.Box):
    def __init__(self, title, subtitle, limit):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        self.add_css_class("card")
        self.limit = limit
        self.hist = collections.deque(maxlen=HISTORY)
        top = Gtk.Box(spacing=6)
        t = Gtk.Label(label=title, xalign=0, hexpand=True)
        t.add_css_class("cardtitle")
        self.sub = Gtk.Label(label=subtitle, xalign=1)
        self.sub.add_css_class("dim")
        top.append(t)
        top.append(self.sub)
        self.append(top)
        row = Gtk.Box(spacing=6)
        self.big = Gtk.Label(label="—", xalign=0)
        self.big.add_css_class("big")
        unit = Gtk.Label(label="°C", valign=Gtk.Align.END, margin_bottom=8)
        unit.add_css_class("unit")
        self.note = Gtk.Label(label="", xalign=1, hexpand=True, valign=Gtk.Align.END, margin_bottom=8)
        self.note.add_css_class("dim")
        row.append(self.big)
        row.append(unit)
        row.append(self.note)
        self.append(row)
        self.bar = Bar()
        self.append(self.bar)
        foot = Gtk.Box()
        self.detail = Gtk.Label(label="", xalign=0, hexpand=True)
        self.detail.add_css_class("small")
        lim = Gtk.Label(label="limit %d °C" % limit, xalign=1)
        lim.add_css_class("small")
        foot.append(self.detail)
        foot.append(lim)
        self.append(foot)
        self.spark = Spark()
        self.append(self.spark)

    def update(self, value, detail):
        self.hist.append(value)
        if value is None:
            self.big.set_label("—")
            self.bar.set(0, COOL)
            self.detail.set_label(detail)
            self.note.set_label("")
            self.note.remove_css_class("over")
            self.remove_css_class("hot")
            return COOL
        col = level_color(value, self.limit)
        self.big.set_markup('<span foreground="%s">%.0f</span>' % (col, value))
        self.bar.set(value / self.limit, col)
        self.detail.set_label(detail)
        near = value >= 0.9 * self.limit
        if value >= self.limit:
            self.note.set_label("%.0f °C above limit" % (value - self.limit))
            self.note.add_css_class("over")
        else:
            self.note.set_label("%.0f °C below limit" % (self.limit - value)
                                if near else "")
            self.note.remove_css_class("over")
        if near:
            self.add_css_class("hot")
        else:
            self.remove_css_class("hot")
        self.spark.set(self.hist, col)
        return col


class FanCard(Gtk.Box):
    def __init__(self):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.add_css_class("card")
        self.fan = None
        top = Gtk.Box(spacing=16)
        self.append(top)
        self.ring = Gtk.DrawingArea()
        self.ring.set_content_width(96)
        self.ring.set_content_height(96)
        self.ring.set_draw_func(self.draw)
        top.append(self.ring)
        col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4, valign=Gtk.Align.CENTER)
        t = Gtk.Label(label="Fan", xalign=0)
        t.add_css_class("cardtitle")
        self.rpm = Gtk.Label(label="—", xalign=0)
        self.rpm.add_css_class("big")
        self.range = Gtk.Label(label="", xalign=0)
        self.range.add_css_class("dim")
        self.ok = Gtk.Label(label="SMC in control", xalign=0)
        self.ok.add_css_class("ok")
        for w in (t, self.rpm, self.range, self.ok):
            col.append(w)
        top.append(col)

        # Presets: radio-style toggle buttons, the active one from smcfan(8).
        self.preset = None		# active preset, "manual" or None
        self.busy = False		# smcfan running
        self.syncing = False		# buttons set by the program
        self.presets = Gtk.Box(spacing=0, halign=Gtk.Align.START)
        self.presets.add_css_class("linked")
        self.presets.add_css_class("presets")
        self.buttons = {}
        for name, label in PRESETS:
            b = Gtk.ToggleButton(label=label)
            b.set_tooltip_text("pfexec smcfan -p -s %s" % name)
            b.connect("toggled", self.on_toggled, name)
            self.buttons[name] = b
            self.presets.append(b)
        self.msg = Gtk.Label(label="", xalign=0, wrap=True, max_width_chars=40)
        self.msg.add_css_class("warnmsg")
        self.msg.set_selectable(True)
        self.append(self.presets)
        self.append(self.msg)
        self.set_preset(read_fan_preset())

    def set_preset(self, preset):
        """Show the active preset; hide the buttons without fan control."""
        if self.busy:
            return
        self.preset = preset
        self.presets.set_visible(preset is not None)
        if preset is None:
            self.msg.set_visible(bool(self.msg.get_label()))
            self.ok.set_label("SMC in control")
            return
        self.syncing = True
        for name, b in self.buttons.items():
            b.set_active(name == preset)
            b.set_sensitive(True)
        self.syncing = False
        if preset == "auto":
            self.ok.set_label("SMC in control")
        elif preset == "manual":
            self.ok.set_label("SMC in control · minimum set by hand")
        else:
            self.ok.set_label("SMC in control · minimum raised (%s)" %
                              dict(PRESETS).get(preset, preset))
        self.msg.set_visible(bool(self.msg.get_label()))

    def on_toggled(self, button, name):
        if self.syncing or self.busy:
            return
        if name == self.preset:
            self.set_preset(self.preset)	# keep the active one pressed
            return
        self.busy = True
        for b in self.buttons.values():
            b.set_sensitive(False)
        argv = [PFEXEC, SMCFAN, "-p", "-s", name]
        try:
            proc = Gio.Subprocess.new(
                argv, Gio.SubprocessFlags.STDOUT_PIPE |
                Gio.SubprocessFlags.STDERR_PIPE)
        except GLib.Error as e:
            self.done("%s: %s" % (PFEXEC, e.message))
            return
        proc.communicate_utf8_async(None, None, self.on_done, name)

    def on_done(self, proc, res, name):
        try:
            _ok, out, err = proc.communicate_utf8_finish(res)
        except GLib.Error as e:
            self.done("smcfan: %s" % e.message)
            return
        status = proc.get_exit_status() if proc.get_if_exited() else -1
        if status == 0:
            self.done("")
        elif status == SMCFAN_PERM:
            self.done('Fan presets need the rights profile "%s". An '
                      'administrator can grant it with:\n'
                      'usermod -P +"%s" %s' %
                      (FAN_PROFILE, FAN_PROFILE, getpass.getuser()))
        elif status == SMCFAN_NOTSUP:
            self.done("This Mac's SMC driver cannot set the fans.")
        else:
            lines = ((err or "") + (out or "")).strip().splitlines()
            self.done(lines[-1] if lines else
                      "smcfan failed (exit status %d)" % status)

    def done(self, msg):
        self.busy = False
        self.msg.set_label(msg)
        self.set_preset(read_fan_preset())

    def update(self, fans):
        self.fan = fans[0] if fans else None
        if self.fan:
            f = self.fan
            self.rpm.set_markup('%.0f <span size="small" foreground="#c0bfbc">rpm</span>' % f["rpm"])
            self.range.set_label("min %.0f · max %.0f" % (f["min"], f["max"]))
        else:
            self.rpm.set_label("—")
            self.range.set_label("no fan data")
        self.ring.queue_draw()

    def draw(self, area, cr, w, h):
        cx, cy, r = w / 2, h / 2, min(w, h) / 2 - 8
        cr.set_line_width(8)
        cr.arc(cx, cy, r, 0, 2 * math.pi)
        cr.set_source_rgba(*rgb(TRACK))
        cr.stroke()
        if self.fan and self.fan["max"] > self.fan["min"]:
            f = self.fan
            frac = (f["rpm"] - f["min"]) / (f["max"] - f["min"])
            frac = max(0.01, min(1.0, frac))
            cr.set_line_cap(cairo.LINE_CAP_ROUND)
            cr.arc(cx, cy, r, -math.pi / 2, -math.pi / 2 + 2 * math.pi * frac)
            cr.set_source_rgba(*rgb(COOL))
            cr.stroke()
        cr.set_source_rgba(*rgb("#9a9996", 0.85))
        for i in range(4):
            a = i * math.pi / 2
            cr.move_to(cx, cy)
            cr.arc(cx, cy, r * 0.55, a, a + 0.9)
            cr.close_path()
            cr.fill()
        cr.arc(cx, cy, 5, 0, 2 * math.pi)
        cr.set_source_rgba(*rgb("#deddda"))
        cr.fill()


# ------------------------------------------------------------------ window

class Window(Gtk.ApplicationWindow):
    def __init__(self, app, model):
        super().__init__(application=app, title="Mac Sensors")
        self.add_css_class("macsensors")
        self.set_default_size(1280, 820)
        if model == "MacPro6,1":
            self.profile, title = "MacPro6,1", "Mac Pro (Late 2013)"
        elif model.startswith("iMac"):
            self.profile, title = "iMac", IMAC_NAMES.get(model, "iMac")
        else:
            self.profile, title = None, model or "Computer"
        self.model = model

        header = Gtk.HeaderBar()
        tb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, valign=Gtk.Align.CENTER)
        t = Gtk.Label(label="Mac Sensors")
        t.add_css_class("title")
        self.subtitle = Gtk.Label(label="%s · %s" % (title, model))
        self.subtitle.add_css_class("subtitle")
        tb.append(t)
        tb.append(self.subtitle)
        header.set_title_widget(tb)
        self.pill = Gtk.Label(label="Live · every %d s" % REFRESH_S)
        self.pill.add_css_class("pill")
        header.pack_end(self.pill)
        self.set_titlebar(header)

        root = Gtk.Box(spacing=24, margin_top=24, margin_bottom=24,
                       margin_start=24, margin_end=24)
        self.set_child(root)

        self.machine = None
        if self.profile:
            panel = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
            panel.add_css_class("panel")
            panel.set_size_request(420, -1)
            hdr = Gtk.Box()
            cap = Gtk.Label(label="THERMAL CORE" if self.profile == "MacPro6,1"
                            else "BEHIND THE DISPLAY", xalign=0, hexpand=True)
            cap.add_css_class("caps")
            hdr.append(cap)
            cut = Gtk.Label(label="cutaway" if self.profile == "MacPro6,1" else "rear view")
            cut.add_css_class("small")
            hdr.append(cut)
            panel.append(hdr)
            self.machine = Machine(self.profile)
            panel.append(self.machine)
            legend = Gtk.Box(spacing=14, halign=Gtk.Align.CENTER)
            for col, name in ((COOL, "cool"), (NORMAL, "normal"), (WARM, "warm"), (HOT, "near limit")):
                l = Gtk.Label()
                l.set_markup('<span foreground="%s">●</span> %s' % (col, name))
                l.add_css_class("small")
                legend.append(l)
            panel.append(legend)
            root.append(panel)

        right = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16, hexpand=True)
        root.append(right)
        grid = Gtk.Grid(column_spacing=16, row_spacing=16, column_homogeneous=True)
        right.append(grid)
        self.defs = cards_for(self.profile, read_cpu_name(), set(read_smc()[0]))
        self.cards = {}
        for i, (cid, ttl, sub, lim, _v, _d) in enumerate(self.defs):
            c = Card(ttl, sub, lim)
            self.cards[cid] = c
            grid.attach(c, i % 3, i // 3, 1, 1)
        self.fancard = FanCard()
        n = len(self.defs)
        grid.attach(self.fancard, n % 3, n // 3, 1, 1)

        keys = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8, vexpand=True)
        keys.add_css_class("card")
        self.exp = Gtk.Expander(label="All sensor keys")
        self.keygrid = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE,
                                   max_children_per_line=4, min_children_per_line=2,
                                   column_spacing=16, row_spacing=4, homogeneous=True)
        sc = Gtk.ScrolledWindow(vexpand=True)
        sc.set_child(self.keygrid)
        self.exp.set_child(sc)
        keys.append(self.exp)
        right.append(keys)

        self.refresh()
        GLib.timeout_add_seconds(REFRESH_S, self.refresh)

    def refresh(self):
        smc, fans = read_smc()
        ks = read_ksensors()
        s = Snapshot(smc, ks, fans)
        n = len(smc) + len(ks)
        if not n:
            self.pill.set_label("no sensors (applesmc / driver/cpu/sensor?)")
        spots = []
        place = SPOTS.get(self.profile, {})
        for cid, _t, _s, _l, value, detail in self.defs:
            v = value(s)
            col = self.cards[cid].update(v, detail(s) if v is not None else "")
            if v is not None:
                spots += [(x, y, r, col) for x, y, r in place.get(cid, [])]
        self.fancard.update(fans)
        self.fancard.set_preset(read_fan_preset())
        if self.machine:
            self.machine.update(spots, fans[0]["rpm"] if fans else 0)
        self.exp.set_label("All sensor keys (%d)" % n)
        if self.exp.get_expanded() or not self.keygrid.get_first_child():
            child = self.keygrid.get_first_child()
            while child:
                nxt = child.get_next_sibling()
                self.keygrid.remove(child)
                child = nxt
            for k, v in sorted(list(smc.items()) + list(ks.items())):
                l = Gtk.Label(label="%s  %.1f" % (k, v), xalign=0)
                l.add_css_class("keys")
                self.keygrid.append(l)
        return True


class App(Gtk.Application):
    def __init__(self):
        super().__init__(application_id=APP_ID,
                         flags=Gio.ApplicationFlags.DEFAULT_FLAGS)
        self.model = read_model()

    def do_startup(self):
        Gtk.Application.do_startup(self)
        settings = Gtk.Settings.get_default()
        if settings is not None:
            settings.set_property("gtk-application-prefer-dark-theme", True)
        prov = Gtk.CssProvider()
        prov.load_from_data(CSS, -1)
        Gtk.StyleContext.add_provider_for_display(
            Gdk.Display.get_default(), prov, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

    def do_activate(self):
        win = self.props.active_window or Window(self, self.model)
        win.present()


if __name__ == "__main__":
    sys.exit(App().run(sys.argv))
