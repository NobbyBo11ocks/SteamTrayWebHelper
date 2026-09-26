"""
Generates icon_on.ico / icon_off.ico from steam_logo_source.png.

The source is the genuine Steam application icon (extracted directly from
Valve's own steam.exe resources, group 101 - the plain icon, not the
notification/voice-chat badge variants), so it already carries real
per-pixel alpha and clean antialiasing. The white glyph is kept white and
everything else (the blue circle, and the internal antialiased blend between
glyph and circle) is remapped to this app's own state colour, using each
pixel's "whiteness" (how close its RGB is to pure white) as the blend
factor - so every edge Valve antialiased, internal or outer, stays exactly
as smooth, just recoloured. The result is Steam's own round icon in black
(enabled) or red (disabled), identical in shape and size to the real thing.

Frames are kept as full 32-bit RGBA: Pillow stores each as a PNG stream
inside the .ico, which Windows Vista+ and Inno Setup's SetupIconFile both
read. Up to 1.2.7 this script quantized each frame to a 64-colour palette to
shrink the file. That kept the round shape - the palette PNG carries
per-colour transparency, which Windows honours - but cut the shading of the
antialiased edges to the palette's few levels (46 distinct colours in the
16px red icon, against 189 in full colour). Full colour costs about 2 KB per
icon.

To inspect an icon, read each frame with IcoFile.getimage(), as CI does.
Image.open() on an .ico keeps a palette frame's pixels but drops its
transparency, so a quantized icon read that way looks like an opaque square
when it is not.

Only tray sizes are emitted. A notification-area icon is requested at
GetSystemMetrics(SM_CXSMICON), which is 16px at 100% scaling and rises with
DPI to 48px at 300%; 64px covers still higher scaling. Larger frames (128,
256) are what a file-manager "extra large icons" view would use, which a tray
helper's DLL is never shown in, so carrying them would just be dead weight in
the shipped binary - the .rsrc section is most of the DLL's size.
"""

import numpy as np
from PIL import Image, ImageFilter

RED = np.array([222, 62, 58])   # disabled-state accent (matches steam.styles error red)
BLACK = np.array([0, 0, 0])     # enabled-state colour
WHITE = np.array([255, 255, 255])

SOURCE = "steam_logo_source.png"
SIZES = [16, 20, 24, 32, 40, 48, 64]

# Downsampling the source's thin ring/glyph strokes to the tray's actual
# on-screen sizes (16-32px) softens them into a grey smear. A light unsharp
# mask restores edge contrast there; it's skipped above 32px where LANCZOS
# alone already looks crisp and sharpening would just add haloing.
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
    # still compresses worse. Flattening it to one constant colour is lossless
    # (alpha is 0 either way) and shrinks the encoded frame. The alpha channel
    # itself is carried through untouched, which is what keeps the icon round.
    out_rgb = np.where(alpha[..., None] == 0, WHITE, out_rgb)

    out = np.dstack([out_rgb, alpha]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def make(path, img):
    frames = []
    for s in SIZES:
        frame = img.resize((s, s), Image.LANCZOS)
        if s <= SHARPEN_UP_TO:
            frame = frame.filter(UNSHARP)
        frames.append(frame)
    frames[-1].save(
        path,
        format="ICO",
        sizes=[(s, s) for s in SIZES],
        append_images=frames[:-1],
    )
    print("wrote", path)


rgb, alpha = load_source()
make("icon_on.ico", build(rgb, alpha, BLACK))
make("icon_off.ico", build(rgb, alpha, RED))
