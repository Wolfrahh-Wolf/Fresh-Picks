"""
download_icons.py — FreshPicks: Icon Downloader  (Windows-compatible)
======================================================================
Downloads and prepares all six icons needed by generate_receipt.py.

    static/icons/
        icon_basket.png          bi-basket2-fill    (brand name)
        icon_envelope.png        bi-envelope-fill   (email in contact)
        icon_phone_contact.png   bi-telephone-fill  (phone in contact)
        icon_phone.png           📞 Twemoji 1f4de   (delivery boy phone)
        icon_scooter.png         🛵 Twemoji 1f6f5   (delivery partner box)
        icon_gift.png            🎁 Twemoji 1f381   (FREE badge)
"""

import os
import sys
import io
import urllib.request


# ── Paths ─────────────────────────────────────────────────────
_HERE     = os.path.dirname(os.path.abspath(__file__))
ICONS_DIR = os.path.normpath(os.path.join(_HERE, "..", "static", "icons"))

# ── Brand colours ─────────────────────────────────────────────
ACCENT_BLUE = "#007acc"
GREEN       = "#22993d"

# ── CDN base URLs ─────────────────────────────────────────────
BI_CDN = "https://cdn.jsdelivr.net/npm/bootstrap-icons@1.11.3/icons"
TW_CDN = "https://cdnjs.cloudflare.com/ajax/libs/twemoji/14.0.2/72x72"

# ── Icon definitions ──────────────────────────────────────────
#   (save_as, source_type, source_id, fill_colour)
ICONS = [
    ("icon_basket.png",        "bootstrap", "basket2-fill",   ACCENT_BLUE),
    ("icon_envelope.png",      "bootstrap", "envelope-fill",  ACCENT_BLUE),
    ("icon_phone_contact.png", "bootstrap", "telephone-fill", ACCENT_BLUE),
    ("icon_phone.png",         "twemoji",   "1f4de",          None),
    ("icon_scooter.png",       "twemoji",   "1f6f5",          None),
    ("icon_gift.png",          "twemoji",   "1f381",          None),
]

PNG_SIZE = 72   # output pixel dimensions (square)


# ─────────────────────────────────────────────────────────────
# DEPENDENCY CHECK
# ─────────────────────────────────────────────────────────────
def _check_deps():
    missing = []
    for pkg in ("svglib", "reportlab", "PIL"):
        try:
            __import__(pkg)
        except ImportError:
            missing.append(pkg)

    if missing:
        install_names = {"PIL": "Pillow"}
        pkgs = " ".join(install_names.get(m, m) for m in missing)
        print(f"Missing packages: {pkgs}")
        print(f"\nInstall them with:")
        print(f"    pip install {pkgs}")
        sys.exit(1)


# ─────────────────────────────────────────────────────────────
# DOWNLOAD HELPER
# ─────────────────────────────────────────────────────────────
def _fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "FreshPicks/1.0"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return r.read()


# ─────────────────────────────────────────────────────────────
# SVG COLOUR INJECTION
# Bootstrap Icons use currentColor throughout — a plain string
# replace is reliable and requires no XML parsing.
# ─────────────────────────────────────────────────────────────
def _colour_svg(svg_bytes: bytes, fill: str) -> bytes:
    svg = svg_bytes.decode("utf-8")
    svg = svg.replace("currentColor", fill)
    svg = svg.replace('fill="black"',    f'fill="{fill}"')
    svg = svg.replace('fill="#000"',     f'fill="{fill}"')
    svg = svg.replace('fill="#000000"',  f'fill="{fill}"')
    return svg.encode("utf-8")


# ─────────────────────────────────────────────────────────────
# SVG → PNG  (pure Python via svglib + reportlab + Pillow)
# ─────────────────────────────────────────────────────────────
def _svg_bytes_to_png(svg_bytes: bytes, dest: str, size: int = PNG_SIZE):
    """
    Convert SVG bytes to a PNG file using svglib + reportlab.
    svglib parses the SVG into a ReportLab drawing object;
    renderPM renders it to a raster image; Pillow saves it as PNG.
    """
    import tempfile
    from svglib.svglib import svg2rlg
    from reportlab.graphics import renderPM
    from PIL import Image

    # svglib needs a file path, not bytes — write to a temp file
    with tempfile.NamedTemporaryFile(suffix=".svg", delete=False, mode="wb") as tmp:
        tmp.write(svg_bytes)
        tmp_path = tmp.name

    try:
        drawing = svg2rlg(tmp_path)
        if drawing is None:
            raise RuntimeError("svglib could not parse the SVG")

        # Render to PNG bytes at native SVG size first
        png_bytes = renderPM.drawToString(drawing, fmt="PNG")

        # Resize to target size with Pillow (LANCZOS for quality)
        img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
        img = img.resize((size, size), Image.LANCZOS)
        img.save(dest, "PNG")
    finally:
        os.unlink(tmp_path)


# ─────────────────────────────────────────────────────────────
# TWEMOJI PNG → resize to uniform size
# ─────────────────────────────────────────────────────────────
def _save_twemoji_png(png_bytes: bytes, dest: str, size: int = PNG_SIZE):
    from PIL import Image
    img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    img = img.resize((size, size), Image.LANCZOS)
    img.save(dest, "PNG")


# ─────────────────────────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────────────────────────
def download_icons(dest_dir: str = ICONS_DIR):
    _check_deps()
    os.makedirs(dest_dir, exist_ok=True)
    print(f"Saving icons to: {dest_dir}\n")

    all_ok = True

    for filename, src_type, src_id, colour in ICONS:
        dest = os.path.join(dest_dir, filename)

        if os.path.isfile(dest):
            print(f"  ✓ Already exists — skipping:  {filename}")
            continue

        print(f"  ↓ {filename} ...", end=" ", flush=True)

        try:
            if src_type == "bootstrap":
                url       = f"{BI_CDN}/{src_id}.svg"
                svg_bytes = _fetch(url)
                svg_bytes = _colour_svg(svg_bytes, colour)
                _svg_bytes_to_png(svg_bytes, dest)

            elif src_type == "twemoji":
                url      = f"{TW_CDN}/{src_id}.png"
                png_bytes = _fetch(url)
                _save_twemoji_png(png_bytes, dest)

            size_kb = os.path.getsize(dest) / 1024
            print(f"OK  ({size_kb:.1f} KB)")

        except Exception as exc:
            print(f"FAILED\n    -> {exc}")
            all_ok = False

    print()
    if all_ok:
        print("All icons ready.")
        print("generate_receipt.py will embed them automatically on the next run.")
    else:
        print("Some downloads failed. Check your internet connection and retry.")
        sys.exit(1)


if __name__ == "__main__":
    download_icons()
