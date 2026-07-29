"""
Generates icon_on.ico / icon_off.ico from steam_logo_source.png.

The source is the genuine Steam application icon (extracted directly from
Valve's own steam.exe resources, group 101 - the plain icon, not the
notification/voice-chat badge variants), so it already carries real
per-pixel alpha and clean antialiasing; no background-removal hack is
needed here. The white glyph is kept white and everything else (the blue
circle, and the internal antialiased blend between glyph and circle) is
remapped to this app's own state colour, using each pixel's "whiteness"
(how close its RGB is to pure white) as the blend factor - so every edge
Valve antialiased, internal or outer, stays exactly as smooth, just recoloured.
"""

import numpy as np
from PIL import Image, ImageFilter

RED = np.array([222, 62, 58])   # disabled-state accent
BLACK = np.array([0, 0, 0])
WHITE = np.array([255, 255, 255])

SOURCE = "steam_logo_source.png"
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]

# Downsampling the source's thin ring/glyph strokes all the way to the tray's
# actual on-screen sizes (16-32px) softens them into a grey smear. A light
# unsharp mask restores edge contrast there; it's skipped above 32px where
# LANCZOS alone already looks crisp and sharpening would just add haloing.
SHARPEN_UP_TO = 32
UNSHARP = ImageFilter.UnsharpMask(radius=1.0, percent=180, threshold=2)


def load_source():
    img = Image.open(SOURCE).convert("RGBA")
    arr = np.array(img).astype(np.float64)
    rgb, alpha = arr[..., :3], arr[..., 3].astype(np.uint8)

    # Steam's own icon already fills ~94% of its canvas - crop to its content
    # bounding box (plus a hair of pad) instead of assuming that ratio, so
    # this keeps working if the source is ever swapped for a different asset.
    ys, xs = np.where(alpha > 0)
    y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    pad = int(max(y1 - y0, x1 - x0) * 0.02)
    y0, x0 = max(y0 - pad, 0), max(x0 - pad, 0)
    y1, x1 = min(y1 + pad, alpha.shape[0] - 1), min(x1 + pad, alpha.shape[1] - 1)
    return rgb[y0 : y1 + 1, x0 : x1 + 1], alpha[y0 : y1 + 1, x0 : x1 + 1]


def build(rgb, alpha, circle_color):
    # Minimum channel, not mean brightness: the circle is blue, so its blue
    # channel alone would skew a mean-brightness estimate toward "whiter than
    # it looks". min(R,G,B) stays low for any saturated colour regardless of
    # hue, so it's a hue-independent proxy for "how white is this pixel".
    whiteness = (rgb.min(axis=2) / 255.0)[..., None]
    out_rgb = WHITE * whiteness + circle_color * (1 - whiteness)

    # Fully-transparent pixels keep whatever colour they inherited from the
    # source, which varies pixel-to-pixel and is invisible but not free: it
    # still costs palette slots and compresses worse. Flattening it to one
    # constant colour is lossless (alpha is 0 either way) and roughly halves
    # the encoded size once the palette step below runs.
    out_rgb = np.where(alpha[..., None] == 0, WHITE, out_rgb)

    out = np.dstack([out_rgb, alpha]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def make(path, img):
    imgs = []
    for s in SIZES:
        frame = img.resize((s, s), Image.LANCZOS)
        if s <= SHARPEN_UP_TO:
            frame = frame.filter(UNSHARP)
        # The glyph is a flat-colour shape with one antialiased edge, i.e. a
        # true-colour RGBA PNG spends most of its bytes on a gradient that a
        # small indexed palette reproduces losslessly to the eye (verified
        # visually against the unquantized frame - no discernible banding).
        # This is what actually dominates the shipped DLL's size: the .rsrc
        # section holding these icons is ~85% of it, code is a few KB.
        frame = frame.quantize(colors=64, method=Image.FASTOCTREE, dither=Image.Dither.NONE)
        imgs.append(frame)
    imgs[-1].save(
        path,
        format="ICO",
        sizes=[(s, s) for s in SIZES],
        append_images=imgs[:-1],
    )
    print("wrote", path)


rgb, alpha = load_source()
make("icon_on.ico", build(rgb, alpha, BLACK))
make("icon_off.ico", build(rgb, alpha, RED))
