"""
generate_receipt.py — Fresh Picks: Server-Side PDF Receipt Engine
"""

import os
import sys
from fpdf import FPDF
from datetime import datetime

# ─────────────────────────────────────────────────────────────
# PATH HELPERS
# ─────────────────────────────────────────────────────────────
_HERE      = os.path.dirname(os.path.abspath(__file__))
_FONT_DIR  = os.path.normpath(os.path.join(_HERE, "..", "static", "fonts"))
_ICON_DIR  = os.path.normpath(os.path.join(_HERE, "..", "static", "icons"))

def _font(name):
    return os.path.join(_FONT_DIR, name)

def _icon(name):
    """Return absolute path to static/icons/<name>.png, or None if missing."""
    p = os.path.join(_ICON_DIR, name)
    return p if os.path.isfile(p) else None


# ─────────────────────────────────────────────────────────────
# AUTO-GENERATE PLACEHOLDER ICONS  (--make-icons mode)
# Produces simple solid-colour squares with a letter so the PDF
# renders immediately.  Replace with real Twemoji/Noto PNGs for
# production.  Run once:  python generate_receipt.py --make-icons
# ─────────────────────────────────────────────────────────────
def make_placeholder_icons():
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        print("Pillow not installed. Run: pip install Pillow")
        return

    os.makedirs(_ICON_DIR, exist_ok=True)

    icons = {
        "icon_basket.png":  ((  0, 122, 204), "B"),   # blue   basket
        "icon_phone.png":   ((  0, 122, 204), "P"),   # blue   phone
        "icon_scooter.png": ((  0, 122, 204), "S"),   # blue   scooter
        "icon_gift.png":    (( 34, 153,  61), "G"),   # green  gift
    }

    for filename, (colour, letter) in icons.items():
        size = 64
        img  = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        # Filled circle background
        draw.ellipse([2, 2, size - 3, size - 3], fill=(*colour, 220))
        # Letter centred
        try:
            font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 28)
        except Exception:
            font = ImageFont.load_default()
        bbox = draw.textbbox((0, 0), letter, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        draw.text(((size - tw) / 2, (size - th) / 2 - 2), letter,
                  fill=(255, 255, 255, 255), font=font)
        dest = os.path.join(_ICON_DIR, filename)
        img.save(dest, "PNG")
        print(f"  Created: {dest}")

    print("\nPlaceholder icons created.")
    print("For production, replace them with real Twemoji/Noto emoji PNGs.")
    print("Download Twemoji PNGs from: https://github.com/twitter/twemoji")
    print("  🧺  1f9fa.png  →  icon_basket.png")
    print("  📞  1f4de.png  →  icon_phone.png")
    print("  🛵  1f6f5.png  →  icon_scooter.png")
    print("  🎁  1f381.png  →  icon_gift.png")


# ─────────────────────────────────────────────────────────────
# BRAND COLOURS
# ─────────────────────────────────────────────────────────────
C_WHITE        = (255, 255, 255)
C_BLACK        = ( 30,  30,  30)
C_ACCENT       = (  0, 122, 204)
C_LABEL        = (120, 120, 120)
C_BORDER       = (200, 200, 200)
C_BORDER_D     = (210, 210, 210)
C_GREEN        = ( 34, 153,  61)
C_GREEN_BG     = (230, 247, 235)
C_HEADER_BG    = (  0, 122, 204)
C_ROW_ALT      = (247, 249, 252)
C_STATUS_DEL   = ( 34, 153,  61)
C_STATUS_PLC   = (204, 120,   0)
C_STATUS_PLC_BG= (255, 248, 230)


# ─────────────────────────────────────────────────────────────
# PARSERS
# ─────────────────────────────────────────────────────────────
def _parse_timestamp(ts):
    try:
        dt = datetime.strptime(ts.strip(), "%Y-%m-%d %H:%M:%S")
        return dt.strftime("%-d %b %Y, %I:%M %p")
    except Exception:
        return ts


def _parse_items(items_string):
    items = []
    if not items_string or not items_string.strip():
        return items
    for part in items_string.split(","):
        segs = [s.strip() for s in part.strip().split(":")]
        if len(segs) < 3 or not segs[0]:
            continue
        veg_id = segs[0]
        if len(segs) >= 4:
            name, qty_g, price_snap = segs[1], int(segs[2]) if segs[2].isdigit() else 0, float(segs[3]) if segs[3] else 0.0
        else:
            name, qty_g, price_snap = veg_id, int(segs[1]) if segs[1].isdigit() else 0, float(segs[2]) if segs[2] else 0.0

        is_free    = (price_snap == 0.0) or veg_id.startswith("VF")
        qty_kg     = qty_g / 1000.0
        qty_str    = f"{qty_kg:.2f} kg" if qty_kg >= 1 else f"{qty_g} g"
        rate_str   = "\u2014" if is_free else f"\u20b9{price_snap:.2f}/kg"
        amount     = (price_snap * qty_g) / 1000.0 if not is_free else 0.0
        amount_str = "FREE" if is_free else f"\u20b9{amount:.2f}"

        items.append(dict(veg_id=veg_id, name=name, qty_g=qty_g,
                          qty_str=qty_str, rate_str=rate_str, amount=amount,
                          amount_str=amount_str, is_free=is_free))
    return items


# ─────────────────────────────────────────────────────────────
# PDF SUBCLASS
# ─────────────────────────────────────────────────────────────
class ReceiptPDF(FPDF):
    def header(self): pass
    def footer(self): pass


# ─────────────────────────────────────────────────────────────
# DRAWING PRIMITIVES
# ─────────────────────────────────────────────────────────────
def _solid_line(pdf, y, left, right, color=C_BORDER, lw=0.3):
    pdf.set_draw_color(*color)
    pdf.set_line_width(lw)
    pdf.line(left, y, right, y)


def _dashed_line(pdf, y, left, right, color=C_BORDER_D, lw=0.2, dash=3.0, gap=2.0):
    pdf.set_draw_color(*color)
    pdf.set_line_width(lw)
    x = left
    while x < right:
        pdf.line(x, y, min(x + dash, right), y)
        x += dash + gap


def _embed_icon(pdf, icon_path, x, y, size_mm=4.5):
    """
    Embed a PNG icon at (x, y) with a square size of size_mm.
    Silent no-op if icon_path is None (file missing).
    """
    if not icon_path:
        return
    try:
        pdf.image(icon_path, x=x, y=y, w=size_mm, h=size_mm)
    except Exception:
        pass   # Never crash the PDF over a missing icon


def _table_header(pdf, y, F, left, col_widths):
    hdr_h = 8
    pdf.set_fill_color(*C_HEADER_BG)
    pdf.set_draw_color(*C_HEADER_BG)
    pdf.set_line_width(0)
    pdf.rect(left, y, sum(col_widths), hdr_h, style="F")
    pdf.set_font(F, "B", 8)
    pdf.set_text_color(*C_WHITE)
    x = left
    for h, cw, al in zip(["#", "ITEM", "RATE", "QTY", "AMOUNT"],
                          col_widths, ["C", "L", "R", "R", "R"]):
        pdf.set_xy(x + (1 if al == "L" else 0), y + 1)
        pdf.cell(cw, 6, h, align=al)
        x += cw
    return y + hdr_h


# ─────────────────────────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────────────────────────
def generate_receipt(data: dict, output_path: str) -> None:

    pdf = ReceiptPDF(orientation="P", unit="mm", format="A4")
    pdf.set_auto_page_break(auto=False)
    pdf.add_page()

    # ── Fonts ─────────────────────────────────────────────────
    pdf.add_font("UF", "",  _font("DejaVuSans.ttf"))
    pdf.add_font("UF", "B", _font("DejaVuSans-Bold.ttf"))
    F = "UF"

    # ── Layout ────────────────────────────────────────────────
    LEFT        = 15
    RIGHT       = 195
    W           = RIGHT - LEFT
    PAGE_BOTTOM = 275
    pdf.set_margins(LEFT, 10, 15)
    COL = [8, 88, 35, 24, 25]   # # | Item | Rate | Qty | Amount = 180mm

    # ── Pre-load icon paths ───────────────────────────────────
    # Bootstrap Icons (SVG→PNG, accent-blue) — exact match to HTML receipt
    ICO_BASKET   = _icon("icon_basket.png")           # bi-basket2-fill   brand name
    ICO_ENVELOPE = _icon("icon_envelope.png")          # bi-envelope-fill  email
    ICO_PHONE_CT = _icon("icon_phone_contact.png")     # bi-telephone-fill contact phone
    # Twemoji PNGs
    ICO_PHONE    = _icon("icon_phone.png")             # 📞  delivery boy phone
    ICO_SCOOTER  = _icon("icon_scooter.png")           # 🛵  delivery partner box
    ICO_GIFT     = _icon("icon_gift.png")              # 🎁  FREE badge in items table

    y = 15

    # ═════════════════════════════════════════════════════════
    # SECTION 1 — HEADER
    # ═════════════════════════════════════════════════════════

    # 🧺 basket icon before brand name
    ICON_SIZE = 5.0   # mm — icon size used throughout
    _embed_icon(pdf, ICO_BASKET, LEFT, y + 1.5, ICON_SIZE)
    brand_x = LEFT + (ICON_SIZE + 1.5 if ICO_BASKET else 0)

    pdf.set_xy(brand_x, y)
    pdf.set_font(F, "B", 18)
    pdf.set_text_color(*C_ACCENT)
    pdf.cell(0, 8, "Fresh Picks", ln=False)

    # "ORDER RECEIPT" — top right
    pdf.set_font(F, "", 7)
    pdf.set_text_color(*C_LABEL)
    pdf.set_xy(LEFT, y)
    pdf.cell(W, 5, "ORDER RECEIPT", align="R", ln=False)

    y += 8

    # Order-ID chip
    cw, ch = 30, 8
    cx, cy = RIGHT - cw, y - 3
    pdf.set_fill_color(*C_WHITE)
    pdf.set_draw_color(*C_ACCENT)
    pdf.set_line_width(0.5)
    pdf.rect(cx, cy, cw, ch, style="D")
    pdf.set_xy(cx, cy + 1)
    pdf.set_font(F, "B", 10)
    pdf.set_text_color(*C_ACCENT)
    pdf.cell(cw, ch - 2, data.get("order_id", ""), align="C")

    # Timestamp
    pdf.set_xy(LEFT, cy + ch + 1)
    pdf.set_font(F, "", 7)
    pdf.set_text_color(*C_LABEL)
    pdf.cell(W, 4, _parse_timestamp(data.get("timestamp", "")), align="R", ln=False)

    # Store sub-lines
    pdf.set_xy(LEFT, y)
    pdf.set_font(F, "", 7.5)
    pdf.set_text_color(*C_LABEL)
    for sl in [
        "123 Grocery Ave, Market City, Chennai \u2013 600045",
        "GSTIN: 22AAAAA0000A1Z5  |  FSSAI: 10020042004971",
        "+91 98765 43210  |  support@freshpicks.in",
    ]:
        pdf.set_x(LEFT)
        pdf.cell(0, 4, sl, ln=True)
        y += 4

    y += 5
    _dashed_line(pdf, y, LEFT, RIGHT)
    y += 6

    # ═════════════════════════════════════════════════════════
    # SECTION 2 — BILL TO / SLOT / STATUS
    # ═════════════════════════════════════════════════════════
    col_w = W / 3

    def _lbl(x, yp, txt):
        pdf.set_xy(x, yp); pdf.set_font(F, "B", 6.5)
        pdf.set_text_color(*C_LABEL); pdf.cell(col_w, 4, txt.upper(), ln=False)

    def _val(x, yp, txt, color=C_BLACK, bold=True, size=10):
        pdf.set_xy(x, yp); pdf.set_font(F, "B" if bold else "", size)
        pdf.set_text_color(*color); pdf.cell(col_w, 5, txt, ln=False)

    _lbl(LEFT,           y, "Bill To")
    _lbl(LEFT + col_w,   y, "Delivery Slot")
    _lbl(LEFT + col_w*2, y, "Status")
    y += 5

    _val(LEFT,         y, data.get("full_name", ""), size=11)
    _val(LEFT + col_w, y, data.get("slot", ""),      size=11)

    # Status badge
    status = data.get("status", "")
    is_del = "deliver" in status.lower()
    sx     = LEFT + col_w * 2
    bw, bh = 38, 7
    pdf.set_fill_color(*(C_GREEN_BG      if is_del else C_STATUS_PLC_BG))
    pdf.set_draw_color(*(C_STATUS_DEL    if is_del else C_STATUS_PLC))
    pdf.set_line_width(0.4)
    pdf.rect(sx, y - 1, bw, bh, style="FD")
    pdf.set_xy(sx, y)
    pdf.set_font(F, "B", 7.5)
    pdf.set_text_color(*(C_STATUS_DEL if is_del else C_STATUS_PLC))
    # ✓ is BMP — DejaVuSans renders it fine
    badge_label = ("\u2713 " + status.upper()) if is_del else status.upper()
    pdf.cell(bw, 5, badge_label, align="C")

    y += 12
    _solid_line(pdf, y, LEFT, RIGHT)
    y += 5

    # ═════════════════════════════════════════════════════════
    # SECTION 3 — CONTACT
    # ═════════════════════════════════════════════════════════
    pdf.set_xy(LEFT, y)
    pdf.set_font(F, "B", 6.5)
    pdf.set_text_color(*C_LABEL)
    pdf.cell(0, 4, "CONTACT", ln=True)
    y += 5

    # ☎ telephone icon (Bootstrap bi-telephone-fill PNG) + phone number
    CT_ICO = 4.0   # contact icon size mm
    _embed_icon(pdf, ICO_PHONE_CT, LEFT, y + 0.5, CT_ICO)
    pdf.set_xy(LEFT + CT_ICO + 1.5, y)
    pdf.set_font(F, "B", 9)
    pdf.set_text_color(*C_ACCENT)
    pdf.cell(0, 5, data.get("user_phone", ""), ln=True)
    y += 5

    # ✉ envelope icon (Bootstrap bi-envelope-fill PNG) + email
    _embed_icon(pdf, ICO_ENVELOPE, LEFT, y + 0.5, CT_ICO)
    pdf.set_xy(LEFT + CT_ICO + 1.5, y)
    pdf.set_font(F, "", 8.5)
    pdf.set_text_color(*C_BLACK)
    pdf.cell(0, 4, data.get("user_email", ""), ln=True)
    y += 5

    # Address — 4 lines
    addr_parts = [p.strip() for p in data.get("address", "").split(",")]
    while len(addr_parts) < 4:
        addr_parts.append("")
    pdf.set_font(F, "", 8)
    pdf.set_text_color(*C_LABEL)
    for i, part in enumerate(addr_parts[:4]):
        pdf.set_xy(LEFT, y)
        pdf.cell(0, 4, part + ("," if i < 3 and part else ""), ln=True)
        y += 4

    y += 3
    _solid_line(pdf, y, LEFT, RIGHT)
    y += 5

    # ═════════════════════════════════════════════════════════
    # SECTION 4 — DELIVERY PARTNER
    # 🛵 scooter PNG icon embedded on the left
    # ═════════════════════════════════════════════════════════
    box_h = 20
    pdf.set_fill_color(*C_WHITE)
    pdf.set_draw_color(*C_BORDER)
    pdf.set_line_width(0.4)
    pdf.rect(LEFT, y, W, box_h, style="FD")

    # "DELIVERY PARTNER" label
    pdf.set_xy(LEFT + 3, y + 2)
    pdf.set_font(F, "B", 6.5)
    pdf.set_text_color(*C_LABEL)
    pdf.cell(0, 4, "DELIVERY PARTNER")

    # 🛵 scooter icon (PNG) — 7 mm square, vertically centred in box
    SCOOTER_SIZE = 7.0
    _embed_icon(pdf, ICO_SCOOTER,
                x=LEFT + 3,
                y=y + (box_h - SCOOTER_SIZE) / 2 + 1,
                size_mm=SCOOTER_SIZE)

    # Name + phone offset right of the icon
    name_x = LEFT + 3 + SCOOTER_SIZE + 2
    pdf.set_xy(name_x, y + 7)
    pdf.set_font(F, "B", 11)
    pdf.set_text_color(*C_BLACK)
    pdf.cell(0, 5, data.get("boy_name", ""))

    # 📞 phone icon (PNG) + number
    PHONE_ICO_SIZE = 3.5
    _embed_icon(pdf, ICO_PHONE,
                x=name_x,
                y=y + 13.5,
                size_mm=PHONE_ICO_SIZE)
    pdf.set_xy(name_x + PHONE_ICO_SIZE + 1.0, y + 13)
    pdf.set_font(F, "", 8.5)
    pdf.set_text_color(*C_ACCENT)
    pdf.cell(0, 4, data.get("boy_phone", ""))

    y += box_h + 6

    # ═════════════════════════════════════════════════════════
    # SECTION 5 — ITEMS TABLE
    # 🎁 gift icon (PNG) replaces the text "FREE" badge
    # ═════════════════════════════════════════════════════════
    items = _parse_items(data.get("items_string", ""))
    y     = _table_header(pdf, y, F, LEFT, COL)

    GIFT_SIZE = 3.8   # mm — gift icon size inside item row

    for row_num, item in enumerate(items, start=1):
        ROW_H = 9

        # Overflow guard
        if y + ROW_H > PAGE_BOTTOM:
            pdf.add_page()
            y = 15
            y = _table_header(pdf, y, F, LEFT, COL)

        pdf.set_line_width(0.2)
        pdf.set_draw_color(*C_BORDER)

        if row_num % 2 == 0:
            pdf.set_fill_color(*C_ROW_ALT)
            pdf.rect(LEFT, y, W, ROW_H, style="F")

        pdf.line(LEFT, y + ROW_H, RIGHT, y + ROW_H)

        # # column
        pdf.set_xy(LEFT, y + (ROW_H - 6) / 2)
        pdf.set_font(F, "", 8)
        pdf.set_text_color(*C_LABEL)
        pdf.cell(COL[0], 6, str(row_num), align="C")

        # Item name — plain, no badge here
        nx = LEFT + COL[0]
        pdf.set_xy(nx + 1, y + (ROW_H - 6) / 2)
        pdf.set_font(F, "B" if not item["is_free"] else "", 8.5)
        pdf.set_text_color(*C_BLACK)
        pdf.cell(COL[1] - 2, 6, item["name"])

        # Rate column — for FREE items: 🎁 icon + "FREE" text centred in the column
        #               for paid items: rate string right-aligned as normal
        x = LEFT + COL[0] + COL[1]
        if item["is_free"]:
            # Total badge width: icon + gap + "FREE" text
            FREE_LABEL   = "FREE"
            pdf.set_font(F, "B", 7)
            free_text_w  = pdf.get_string_width(FREE_LABEL)
            total_badge_w = GIFT_SIZE + 1.0 + free_text_w   # icon + gap + text

            # Centre the whole badge inside COL[2]
            badge_x = x + COL[2] - total_badge_w
            icon_y_pos = y + (ROW_H - GIFT_SIZE) / 2

            if ICO_GIFT:
                _embed_icon(pdf, ICO_GIFT, badge_x, icon_y_pos, GIFT_SIZE)
                pdf.set_xy(badge_x + GIFT_SIZE + 1.0, y + (ROW_H - 5) / 2)
                pdf.set_font(F, "B", 7)
                pdf.set_text_color(*C_GREEN)
                pdf.cell(free_text_w, 5, FREE_LABEL)
            else:
                # Fallback: green pill centred in Rate column
                pill_w, pill_h = 16, 5
                pill_x = x + (COL[2] - pill_w) / 2
                pdf.set_fill_color(*C_GREEN_BG)
                pdf.set_draw_color(*C_GREEN)
                pdf.set_line_width(0.3)
                pdf.rect(pill_x, y + (ROW_H - pill_h) / 2, pill_w, pill_h, style="FD")
                pdf.set_xy(pill_x, y + (ROW_H - pill_h) / 2)
                pdf.set_font(F, "B", 6.5)
                pdf.set_text_color(*C_GREEN)
                pdf.cell(pill_w, pill_h, FREE_LABEL, align="C")
        else:
            pdf.set_xy(x, y + (ROW_H - 6) / 2)
            pdf.set_font(F, "", 8)
            pdf.set_text_color(*C_BLACK)
            pdf.cell(COL[2], 6, item["rate_str"], align="R")

        # Qty
        x += COL[2]
        pdf.set_xy(x, y + (ROW_H - 6) / 2)
        pdf.set_font(F, "", 8)
        pdf.set_text_color(*C_BLACK)
        pdf.cell(COL[3], 6, item["qty_str"], align="R")

        # Amount
        x += COL[3]
        pdf.set_xy(x, y + (ROW_H - 6) / 2)
        pdf.set_font(F, "B", 8.5)
        pdf.set_text_color(*C_GREEN if item["is_free"] else C_ACCENT)
        pdf.cell(COL[4], 6, item["amount_str"], align="R")

        y += ROW_H

    y += 4

    # ═════════════════════════════════════════════════════════
    # SECTION 6 — TOTALS
    # ═════════════════════════════════════════════════════════
    if y + 35 > PAGE_BOTTOM:
        pdf.add_page(); y = 20

    total = float(data.get("total", 0))

    def _trow(label, value, bold=False, vcol=C_BLACK):
        nonlocal y
        pdf.set_xy(LEFT, y)
        pdf.set_font(F, "B" if bold else "", 10 if bold else 9)
        pdf.set_text_color(*C_LABEL); pdf.cell(W - 40, 6, label)
        pdf.set_font(F, "B" if bold else "", 11 if bold else 9)
        pdf.set_text_color(*vcol); pdf.cell(40, 6, value, align="R", ln=True)
        y = pdf.get_y()

    _trow("Subtotal",         f"\u20b9{total:.2f}")
    _trow("Delivery Charges", "FREE", vcol=C_GREEN)

    y += 1
    _solid_line(pdf, y, LEFT, RIGHT, color=C_BORDER, lw=0.4)
    y += 3; pdf.set_y(y)

    pdf.set_xy(LEFT, y); pdf.set_font(F, "B", 12); pdf.set_text_color(*C_BLACK)
    pdf.cell(W - 40, 7, "Total Paid")
    pdf.set_text_color(*C_ACCENT)
    pdf.cell(40, 7, f"\u20b9{total:.2f}", align="R", ln=True)
    y = pdf.get_y() + 6

    # ═════════════════════════════════════════════════════════
    # SECTION 7 — FOOTER
    # ═════════════════════════════════════════════════════════
    if y + 22 > PAGE_BOTTOM:
        pdf.add_page(); y = 20

    _dashed_line(pdf, y, LEFT, RIGHT)
    y += 5

    pdf.set_xy(LEFT, y); pdf.set_font(F, "", 8); pdf.set_text_color(*C_LABEL)
    pdf.multi_cell(W, 4.5,
        "Thank you for shopping with FreshPicks!\n"
        "This is a computer-generated receipt. No signature required.",
        align="C")
    y = pdf.get_y() + 3

    oid  = data.get("order_id", "")
    date = data.get("timestamp", "")[:10]
    pdf.set_xy(LEFT, y); pdf.set_font(F, "", 6.5); pdf.set_text_color(*C_LABEL)
    pdf.cell(W, 4,
        f"Receipt ID: {oid}   Generated: {date}   CodeCrafters - SDP-1",
        align="C")
    y = pdf.get_y() + 5

    _dashed_line(pdf, y, LEFT, RIGHT)

    pdf.output(output_path)


# ─────────────────────────────────────────────────────────────
# CLI  —  python generate_receipt.py --make-icons
# ─────────────────────────────────────────────────────────────
if __name__ == "__main__":
    if "--make-icons" in sys.argv:
        print(f"Creating placeholder icons in: {_ICON_DIR}\n")
        make_placeholder_icons()
    else:
        print("Usage: python generate_receipt.py --make-icons")
        print("       (to generate placeholder icon PNGs for development)")
