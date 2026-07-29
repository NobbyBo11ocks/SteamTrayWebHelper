"""
Generates wizard_banner.bmp / wizard_small.bmp for NoSteamWebHelper.iss.

Both are Steam-navy backgrounds with the same tray icon (icon_on.ico) Inno
Setup would otherwise cover with its generic grey disk-box artwork. Sourced
from the tray icon rather than redrawn, so the installer and the tray stay
visually consistent if the icon is ever regenerated - rerun this after
`python ../src/res/make_icon.py` picks up a new source.
"""

from PIL.IcoImagePlugin import IcoFile
from PIL import Image

ICON = "../src/res/icon_on.ico"
NAVY = (27, 40, 56)  # Steam panel navy (#1b2838), matches the tray menu background


def get_frame(size):
    with open(ICON, "rb") as f:
        ico = IcoFile(f)
        im = ico.getimage((size, size))
        im.load()
        return im.convert("RGBA")


def banner_on(bg, icon_size, box):
    icon = get_frame(icon_size)
    tile = Image.new("RGBA", icon.size, NAVY + (255,))
    tile.alpha_composite(icon)
    x = box[0] + (box[2] - box[0] - icon.width) // 2
    y = box[1] + (box[3] - box[1] - icon.height) // 2
    bg.paste(tile.convert("RGB"), (x, y))


# Welcome/Finished pages: classic Inno large-image size.
banner = Image.new("RGB", (164, 314), NAVY)
banner_on(banner, 128, (0, 0, 164, 314))
banner.save("wizard_banner.bmp")
print("wrote wizard_banner.bmp")

# Corner image shown on every other page: classic Inno small-image size.
small = Image.new("RGB", (55, 58), NAVY)
banner_on(small, 40, (0, 0, 55, 58))
small.save("wizard_small.bmp")
print("wrote wizard_small.bmp")
