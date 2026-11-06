#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Turn the branding PNGs into the formats the NSIS installer can actually use.

WHY THIS EXISTS
    NSIS reads neither PNG nor anything else modern. Checked against the Modern
    UI 2 reference:

        MUI_ICON / MUI_UNICON                 icon_file  (.ico)
        MUI_WELCOMEFINISHPAGE_BITMAP          bmp_file, 164x314 recommended
        MUI_HEADERIMAGE_BITMAP                bmp_file, 150x57  recommended

    So `logotip.png` has to become a pair of BMPs at fixed sizes and
    `logotip2.png` a multi-resolution .ico. This script does that.

    Run it ONCE, when the artwork changes, and COMMIT the results. The installer
    build must not depend on Python or Pillow -- build_installer.bat only needs
    CMake and makensis, and it should stay that way.

USAGE
    python installer/make_assets.py                     # defaults, from repo root
    python installer/make_assets.py --bg "#1E2430"      # dark panel behind the logo
    python installer/make_assets.py --logo art/big.png --icon art/mark.png

NOTES ON THE OUTPUT
    The BMPs are 24-bit and uncompressed. That is deliberate: NSIS is content
    with plain BGR, while 32-bit BMPs carrying an alpha channel render as a black
    box on some Windows versions. Transparency in the source is therefore
    composited onto --bg instead of being carried through.

    The logo is fitted, never stretched. 164x314 is a tall narrow strip and
    almost no logo shares that shape; stretching one to fit looks broken, so the
    image is scaled to fit and centred on a --bg panel.
"""

import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTDIR = os.path.join(ROOT, "installer", "assets")

WELCOME_SIZE = (164, 314)      # MUI_WELCOMEFINISHPAGE_BITMAP
HEADER_SIZE = (150, 57)        # MUI_HEADERIMAGE_BITMAP
ICON_SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
              (128, 128), (256, 256)]


def _die(msg):
    sys.stderr.write("error: %s\n" % msg)
    sys.exit(1)


def _load(path):
    from PIL import Image
    if not os.path.isfile(path):
        _die("no such file: %s" % path)
    return Image.open(path).convert("RGBA")


def _drop_background(img, tol=20):
    """Make the surrounding background transparent, by flood fill from the corners.

    NOT a global "white becomes transparent" threshold, which is the obvious
    thing and the wrong thing here: the compass has a white dial face and white
    arrow tips, and a global rule would punch holes straight through them. Flood
    filling from the four corners only reaches the background region that
    actually surrounds the artwork.

    Matters most for the .ico -- an opaque near-white source would install as a
    white tile in Explorer rather than a compass -- but it is also what makes
    --bg mean anything for the BMPs: without it a dark panel would show the
    logo's own white square sitting on top of it.
    """
    import numpy as np
    from PIL import Image, ImageDraw

    work = img.convert("RGB")
    arr = np.array(work)
    sentinel = None
    for cand in ((255, 0, 255), (0, 255, 255), (255, 255, 0), (7, 3, 11)):
        if not (arr == np.array(cand, dtype=arr.dtype)).all(axis=2).any():
            sentinel = cand
            break
    if sentinel is None:                       # pragma: no cover
        return img                             # give up rather than corrupt it

    w, h = work.size
    for xy in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        ImageDraw.floodfill(work, xy, sentinel, thresh=tol)

    bg = (np.array(work) == np.array(sentinel, dtype=arr.dtype)).all(axis=2)
    out = np.array(img.convert("RGBA"))
    out[..., 3] = np.where(bg, 0, out[..., 3])
    return Image.fromarray(out, "RGBA")


def _fit_onto(img, size, bg):
    """Scale to fit inside `size` and centre on a solid panel. Never stretches."""
    from PIL import Image
    panel = Image.new("RGBA", size, bg)
    src = img.copy()
    src.thumbnail(size, Image.LANCZOS)
    panel.alpha_composite(src, ((size[0] - src.width) // 2,
                                (size[1] - src.height) // 2))
    # Flatten to RGB: a 24-bit BMP is what NSIS handles without surprises.
    return panel.convert("RGB")


def _parse_bg(text):
    text = text.lstrip("#")
    if len(text) != 6:
        _die("--bg wants RRGGBB, got %r" % text)
    try:
        return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4)) + (255,)
    except ValueError:
        _die("--bg is not hex: %r" % text)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--logo", default=os.path.join(ROOT, "logotip.png"),
                    help="artwork for the installer pages (default: logotip.png)")
    ap.add_argument("--icon", default=os.path.join(ROOT, "logotip2.png"),
                    help="artwork for the .exe icon (default: logotip2.png)")
    ap.add_argument("--header", default=None,
                    help="artwork for the 150x57 page header. Defaults to --icon, "
                         "not --logo: a square logo with a tagline scales to 57 px "
                         "tall there, where the wordmark is a smear. The bare mark "
                         "still reads.")
    ap.add_argument("--keep-bg", action="store_true",
                    help="do not flood-fill the background away (use if the "
                         "artwork is already transparent, or deliberately framed)")
    ap.add_argument("--out", default=OUTDIR, help="output directory")
    ap.add_argument("--bg", default="FFFFFF",
                    help="panel colour behind the logo, RRGGBB (default: white)")
    args = ap.parse_args()

    try:
        import PIL  # noqa: F401
    except ImportError:
        _die("Pillow is needed:  pip install pillow")

    bg = _parse_bg(args.bg)
    os.makedirs(args.out, exist_ok=True)

    def prep(path):
        img = _load(path)
        return img if args.keep_bg else _drop_background(img)

    logo = prep(args.logo)
    mark = prep(args.icon)
    head = mark if args.header is None else prep(args.header)

    welcome = os.path.join(args.out, "welcome.bmp")
    header = os.path.join(args.out, "header.bmp")
    _fit_onto(logo, WELCOME_SIZE, bg).save(welcome, "BMP")
    _fit_onto(head, HEADER_SIZE, bg).save(header, "BMP")
    print("wrote %s  (%dx%d, 24-bit)" % ((welcome,) + WELCOME_SIZE))
    print("wrote %s  (%dx%d, 24-bit)" % ((header,) + HEADER_SIZE))

    ico = os.path.join(args.out, "AxisFlow.ico")
    # Square it off first: Pillow's ICO writer would letterbox a non-square
    # source, and a squashed icon is obvious at 16x16.
    side = max(mark.size)
    from PIL import Image
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.alpha_composite(mark, ((side - mark.width) // 2,
                                  (side - mark.height) // 2))
    # Alpha IS kept here, unlike the BMPs: .ico supports it and Windows expects
    # icons to be transparent outside the artwork.
    square.save(ico, "ICO", sizes=ICON_SIZES)
    print("wrote %s  (%s)" % (ico, ", ".join("%dx%d" % s for s in ICON_SIZES)))

    print("\nCommit these -- build_installer.bat must not need Python.")


if __name__ == "__main__":
    main()
