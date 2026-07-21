import math
from PIL import Image, ImageDraw

BG = (27, 40, 56, 255)        # Steam's dark navy (#1B2838)
GLASS = (240, 244, 248, 255)  # window body
BAR = (102, 192, 244, 255)    # Steam accent blue (#66C0F4) title bar
STOP = (222, 62, 58, 255)     # prohibition red (disabled state)
GO = (86, 197, 118, 255)      # status-dot green (enabled state)

SS = 8  # supersampling factor for anti-aliasing


def build(size, state, detailed):
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    pad = S * 0.03
    d.ellipse([pad, pad, S - pad, S - pad], fill=BG)

    win_w, win_h = S * 0.60, S * 0.48
    wx0 = (S - win_w) / 2
    wy0 = (S - win_h) / 2 - S * 0.02
    wx1 = wx0 + win_w
    wy1 = wy0 + win_h
    radius = win_h * 0.16

    d.rounded_rectangle([wx0, wy0, wx1, wy1], radius=radius, fill=GLASS)

    bar_h = win_h * 0.30
    d.rounded_rectangle([wx0, wy0, wx1, wy0 + bar_h], radius=radius, fill=BAR)
    d.rectangle([wx0, wy0 + bar_h - radius, wx1, wy0 + bar_h], fill=BAR)

    if detailed:
        dot_r = win_w * 0.028
        for i in range(3):
            cx = wx0 + win_w * 0.12 + i * dot_r * 3.2
            cy = wy0 + bar_h / 2
            d.ellipse([cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r], fill=GLASS)

        line_h = win_h * 0.07
        for i, w in enumerate([win_w * 0.62, win_w * 0.42]):
            ly = wy0 + bar_h + win_h * 0.14 + i * (line_h + win_h * 0.10)
            d.rounded_rectangle([wx0 + win_w * 0.08, ly, wx0 + win_w * 0.08 + w, ly + line_h],
                                 radius=line_h / 2, fill=(206, 214, 222, 255))

    if state == "off":
        # Prohibition ring + slash, sized generously so it reads at a glance.
        ring_pad = win_w * 0.16
        rx0, ry0 = wx0 - ring_pad, wy0 - ring_pad
        rx1, ry1 = wx1 + ring_pad, wy1 + ring_pad
        stroke = (rx1 - rx0) * 0.155
        d.ellipse([rx0, ry0, rx1, ry1], outline=STOP, width=round(stroke))

        cx, cy = (rx0 + rx1) / 2, (ry0 + ry1) / 2
        r = (rx1 - rx0) / 2
        ang = math.radians(45)
        dx, dy = math.cos(ang) * r, math.sin(ang) * r
        d.line([(cx - dx, cy - dy), (cx + dx, cy + dy)], fill=STOP, width=round(stroke))
    else:
        # Small "active" status dot, bottom-right, with a ring matching the
        # background so it reads as a badge rather than a smudge.
        dot_r = S * 0.155
        cx, cy = S * 0.775, S * 0.775
        ring = dot_r * 1.28
        d.ellipse([cx - ring, cy - ring, cx + ring, cy + ring], fill=BG)
        d.ellipse([cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r], fill=GO)

    return img.resize((size, size), Image.LANCZOS)


def make(path, state):
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    imgs = [build(s, state, detailed=(s > 24)) for s in sizes]
    imgs[-1].save(
        path,
        format="ICO",
        sizes=[(s, s) for s in sizes],
        append_images=imgs[:-1],
    )
    print("wrote", path)


make("icon_on.ico", "on")
make("icon_off.ico", "off")
