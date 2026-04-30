"""
app.py - Fresh Picks: Main Flask Application (v4 — Binary Storage Edition)
============================================================================

DEVELOPER NOTE: SSL, HTTPS & MULTI-DEVICE SETUP

WHY HTTPS ON COLLEGE WI-FI?
─────────────────────────
    HTTP sends data as plain text. On a shared Wi-Fi network
    (like a college lab or hotspot), anyone on the same network
    can run a packet-sniffer (e.g. Wireshark) and read:
    username=alice&password=Alice@123

    HTTPS encrypts everything using SSL/TLS so it becomes:
    Gx92#@!k%LpQ... (unreadable without our key.pem)

SELF-SIGNED CERTIFICATE WARNING:
───────────────────────────────
    When you open https://<ip>:5000 on any browser, you will see:
    "Your connection is not private"
    NET::ERR_CERT_AUTHORITY_INVALID

    This is EXPECTED. Our cert.pem is "self-signed" — we created
    it ourselves rather than paying a Certificate Authority (CA)
    like DigiCert or Let's Encrypt. Browsers don't trust unknown
    issuers by default, so they warn you.

HOW TO BYPASS (for demo):
    Chrome/Edge: Click "Advanced" -> "Proceed to <IP> (unsafe)"
    Firefox:     Click "Advanced" -> "Accept the Risk"
    (Do this once per device per session.)

ARCHITECTURE CHANGE (v4):
    The C backend now reads/writes binary .dat files instead of
    plain .txt files. All pipe-delimited stdout contracts between
    the C binaries and this Flask app remain IDENTICAL — only the
    internal storage mechanism changed. bridge.py is unchanged.

ROUTES:
  GET  /                        -> Landing page (index.html)
  GET  /login/<role>            -> Unified login page (user or admin)
  GET  /register                -> Register page
  GET  /user_home               -> User dashboard (requires user login)
  GET  /admin_dash              -> Admin dashboard (requires admin login)
  GET  /profile                 -> User profile page
  GET  /security                -> Security/Change Password page
  GET  /shop                    -> Shop / vegetable listing
  GET  /cart                    -> Cart page
  GET  /user_orders             -> User order history page
  GET  /admin_inventory         -> Admin inventory management page
  GET  /admin_orders            -> Admin orders dispatch page
  GET  /logout                  -> Clear session and redirect to home
  POST /api/login               -> Validate login via C auth binary
  POST /api/register            -> Register new user via C auth binary
  POST /api/get_profile         -> Fetch profile data via C auth binary
  POST /api/update_profile      -> Update one profile field
  POST /api/change_password     -> Unified password change (user OR admin)
  GET  /api/list_products       -> List all vegetables via C order binary
  POST /api/update_stock        -> Update vegetable stock (admin only)
  POST /api/update_promo_stock  -> Update freebie stock (admin only)
  POST /api/add_to_cart         -> Add item to cart
  POST /api/view_cart           -> View cart contents
  POST /api/update_cart_qty     -> Update item quantity in cart
  POST /api/remove_item         -> Remove item from cart
  POST /api/checkout            -> Place order
  POST /api/get_user_orders     -> Get order history for logged-in user
  GET  /api/admin_orders        -> Get all orders (admin, heap-sorted)
  POST /api/get_admin_orders    -> Get all orders newest-first (admin)
  POST /api/update_order_status -> Update order status (admin only)
  POST /api/promote_slot_orders -> Batch-promote slot orders (admin only)
  POST /api/cancel_order        -> Cancel an order
  GET  /api/get_active_orders   -> Get active (placed/OFD) orders (admin)
  POST /api/assign_agent        -> Assign delivery agent (admin only)
  GET  /api/download_receipt/<order_id> -> Generate and stream PDF receipt
  POST /api/get_admin_info      -> Return admin session data as JSON

HOW TO RUN:
  1. python generate_certs.py   (once, to create cert.pem + key.pem)
  2. ./txt_to_bin_converter     (once, to migrate .txt files to .dat)
  3. python app.py              (every time to start the server)

Team: CodeCrafters | Project: Fresh Picks | SDP-1
"""

import ssl
import os
import tempfile
import sys
import smtplib
from datetime import datetime
from email.message import EmailMessage

from flask import (
    Flask,
    render_template,
    request,
    jsonify,
    session,
    redirect,
    send_from_directory,
    send_file
)

from bridge import run_c_binary
from generate_receipt import generate_receipt


# ─────────────────────────────────────────────────────────────
# Flask App Setup
# ─────────────────────────────────────────────────────────────
app = Flask(
    __name__,
    template_folder="../templates",
    static_folder="../static"
)

app.secret_key = "fresh_picks_secret_codecrafters_2026"

# ─────────────────────────────────────────────────────────────
# SSL Certificate Paths
# Built relative to this file so the server works regardless of
# which directory you launch it from.
# ─────────────────────────────────────────────────────────────
_HERE     = os.path.dirname(os.path.abspath(__file__))
CERT_FILE = os.path.join(_HERE, "cert.pem")
KEY_FILE  = os.path.join(_HERE, "key.pem")


# =============================================================
# PRIVATE HELPERS
# Small, pure functions used only inside this module.
# =============================================================

def _safe_float(value, default=0.0):
    """
    PURPOSE: Safely convert a value to float without raising on bad input.
    RETURNS: float on success, `default` on any error.
    """
    try:
        return float(value)
    except (ValueError, TypeError):
        return default


def _require_login(role=None):
    """
    PURPOSE: Guard clause helper — returns a redirect if the session is invalid.
             Returns None when the session is valid (caller proceeds normally).
    PARAMS:  role — "user", "admin", or None (any logged-in session).
    RETURNS: A Flask redirect response, or None.
    """
    if role == "user"  and session.get("role") != "user":
        return redirect("/login/user")
    if role == "admin" and session.get("role") != "admin":
        return redirect("/login/admin")
    if role is None and "user_id" not in session:
        return redirect("/")
    return None


def _parse_order_line(line):
    """
    PURPOSE: Parse one pipe-delimited order line into a dict.
             Used by all order-listing API endpoints.
    PARAMS:  line — one stripped line of raw C stdout output.
    RETURNS: dict or None if the line has fewer than 8 fields.

    SCHEMA:  order_id|user_id|total_amount|delivery_slot|delivery_boy_id|
             status|timestamp|items_string[|boy_name|boy_phone]
    """
    parts = line.strip().split("|")
    if len(parts) < 8:
        return None

    return {
        "order_id":        parts[0],
        "user_id":         parts[1],
        "total_amount":    _safe_float(parts[2]),
        "delivery_slot":   parts[3],
        "delivery_boy_id": parts[4],
        "status":          parts[5],
        "timestamp":       parts[6],
        "items_string":    parts[7],
        "boy_name":        parts[8] if len(parts) > 8 else "Unknown",
        "boy_phone":       parts[9] if len(parts) > 9 else "N/A",
    }


def _parse_multiline_orders(raw_output):
    """
    PURPOSE: Parse multi-line C stdout (header line + data lines) into a list
             of order dicts. Skips the first line (SUCCESS| header) and any
             blank lines.
    PARAMS:  raw_output — the full stdout string from run_c_binary().
    RETURNS: list of order dicts.
    """
    orders = []
    lines  = raw_output.strip().split("\n")

    for line in lines[1:]:   # lines[0] is the "SUCCESS|" header
        line = line.strip()
        if not line:
            continue
        order = _parse_order_line(line)
        if order:
            orders.append(order)

    return orders


def _get_receipt_data(order_id):
    """
    Fetch and parse receipt data from the C receipt binary.
    Used by both download and email receipt endpoints.
    """
    result = run_c_binary("receipt", [order_id])

    if result["status"] != "SUCCESS":
        return None, result.get("data", "Could not fetch order data")

    raw = result["raw_output"].strip().split("\n")[0]
    parts = raw.split("|")

    if len(parts) < 14:
        return None, f"Malformed receipt data ({len(parts)} fields)"

    return {
        "order_id":    parts[1],
        "user_id":     parts[2],
        "full_name":   parts[3],
        "user_phone":  parts[4],
        "user_email":  parts[5],
        "address":     parts[6],
        "slot":        parts[7],
        "status":      parts[8],
        "timestamp":   parts[9],
        "boy_name":    parts[10],
        "boy_phone":   parts[11],
        "total":       _safe_float(parts[12]),
        "items_string": "|".join(parts[13:]),
    }, None


def _generate_receipt_pdf(receipt_data):
    """
    Generate receipt PDF and return (path, error).
    Caller is responsible for deleting the temp file when appropriate.
    """
    try:
        with tempfile.NamedTemporaryFile(
            suffix=".pdf",
            delete=False,
            prefix=f"{receipt_data['order_id']}_{receipt_data['user_id']}_"
        ) as tmp:
            tmp_path = tmp.name

        generate_receipt(receipt_data, tmp_path)
        return tmp_path, None
    except Exception as e:
        return None, f"PDF generation failed: {str(e)}"


def _send_receipt_email(receipt_data, pdf_path):
    """
    Send receipt PDF using SMTP credentials from environment variables.

    Required:
      FP_EMAIL_USER
      FP_EMAIL_APP_PASSWORD

    Optional:
      FP_SMTP_HOST defaults to smtp.gmail.com
      FP_SMTP_PORT defaults to 587
    """
    sender = os.environ.get("FP_EMAIL_USER", "").strip()
    password = os.environ.get("FP_EMAIL_APP_PASSWORD", "").strip()
    smtp_host = os.environ.get("FP_SMTP_HOST", "smtp.gmail.com").strip()
    smtp_port = int(os.environ.get("FP_SMTP_PORT", "587"))

    if not sender or not password:
        return "Email sender is not configured. Set FP_EMAIL_USER and FP_EMAIL_APP_PASSWORD."

    recipient = receipt_data.get("user_email", "").strip()
    if not recipient:
        return "Customer email address is missing for this order."

    order_id = receipt_data.get("order_id", "order")
    msg = EmailMessage()
    msg["Subject"] = f"Fresh Picks Receipt - {order_id}"
    msg["From"] = sender
    msg["To"] = recipient
    msg.set_content(
        f"Hi {receipt_data.get('full_name', 'Customer')},\n\n"
        f"Your Fresh Picks receipt for order {order_id} is attached.\n\n"
        "Thank you for shopping with Fresh Picks.\n"
    )

    with open(pdf_path, "rb") as f:
        msg.add_attachment(
            f.read(),
            maintype="application",
            subtype="pdf",
            filename=f"{order_id}_{receipt_data.get('user_id', 'customer')}.pdf"
        )

    try:
        with smtplib.SMTP(smtp_host, smtp_port, timeout=20) as server:
            server.starttls()
            server.login(sender, password)
            server.send_message(msg)
        return None
    except Exception as e:
        return f"Email send failed: {str(e)}"


# =============================================================
# PAGE ROUTES — Serve HTML templates
# All routes apply a guard clause FIRST; if the check fails,
# we return the redirect immediately without proceeding further.
# =============================================================

@app.route("/")
def index():
    # Public landing page — no login required.
    return render_template("index.html")


@app.route("/login/<role>")
def login_page(role):
    # Unified login: handles both /login/user and /login/admin.
    if role not in ["user", "admin"]:
        return redirect("/")

    # Auto-redirect if the correct role is already in session.
    if session.get("role") == role:
        return redirect("/admin_dash" if role == "admin" else "/user_home")

    return render_template("login.html", role=role)


@app.route("/register")
def register():
    return render_template("register.html")


@app.route("/user_home")
def user_home():
    guard = _require_login(role="user")
    if guard: return guard

    return render_template("user_home.html", username=session.get("username"))


@app.route("/admin_dash")
def admin_dash():
    guard = _require_login(role="admin")
    if guard: return guard

    return render_template(
        "admin_dash.html",
        username   = session.get("username"),
        admin_name = session.get("admin_name", "Admin")
    )


@app.route("/profile")
def profile():
    guard = _require_login(role="user")
    if guard: return guard

    return render_template("profile.html")


@app.route("/security")
def security():
    guard = _require_login()
    if guard: return guard

    return render_template("security.html")


@app.route("/logout")
def logout():
    session.clear()
    return redirect("/")


@app.route("/shop")
def shop():
    guard = _require_login(role="user")
    if guard: return guard

    return render_template("shop.html")


@app.route("/cart")
def cart():
    guard = _require_login(role="user")
    if guard: return guard

    return render_template("cart.html")


@app.route("/user_orders")
def user_orders_page():
    guard = _require_login(role="user")
    if guard: return guard

    return render_template("user_orders.html", username=session.get("username"))


@app.route("/admin_inventory")
def admin_inventory():
    guard = _require_login(role="admin")
    if guard: return guard

    # ── Fetch vegetable list ──────────────────────────────────
    vegetables = []
    veg_result = run_c_binary("order", ["list_products"])

    if veg_result["status"] == "SUCCESS":
        for line in veg_result["raw_output"].strip().split("\n")[1:]:
            parts = line.split("|")
            if len(parts) >= 7:
                vegetables.append({
                    "veg_id":          parts[0],
                    "category":        parts[1],
                    "name":            parts[2],
                    "stock_g":         int(parts[3]),
                    "price_per_1000g": float(parts[4]),
                    "tag":             parts[5],
                    "validity_days":   int(parts[6])
                })

    # ── Fetch promotional freebie list ───────────────────────
    free_items   = []
    promo_result = run_c_binary("inventory", ["list_promo"])

    if promo_result["status"] == "SUCCESS":
        for line in promo_result["raw_output"].strip().split("\n")[1:]:
            parts = line.split("|")
            if len(parts) >= 5:
                free_items.append({
                    "vf_id":           parts[0],
                    "name":            parts[1],
                    "stock_g":         int(parts[2]),
                    "min_trigger_amt": float(parts[3]),
                    "free_qty_g":      int(parts[4])
                })

    return render_template(
        "admin_inventory.html",
        vegetables = vegetables,
        free_items = free_items,
        admin_name = session.get("admin_name", "Admin")
    )

"""
app.py — ADDITIONS FOR USER MANAGEMENT MODULE
=============================================
Paste these two blocks into app.py at the indicated positions.

BLOCK 1 → Page route: add after the /admin_orders route (around line 368).
BLOCK 2 → API routes: add before the "START THE SERVER" section at the bottom.

Both blocks follow the exact same guard-clause + bridge-call pattern
already used by /admin_inventory and /api/admin_orders in the existing app.py.

Team: CodeCrafters | Project: Fresh Picks | SDP-1
"""


# =============================================================
# BLOCK 1 — PAGE ROUTE
# Add this after the existing /admin_orders page route.
# =============================================================

@app.route("/admin/users")
def admin_users():
    """
    GET /admin/users  (admin only)

    Calls users.exe list_users and passes the full user list to the
    admin_users.html template as a Jinja variable so the page renders
    server-side. Client-side JS then handles search/filter locally
    without additional API calls, keeping the UI snappy.

    Template context:
      users       — list of user dicts
      total_count — total users in the binary
    """
    guard = _require_login(role="admin")
    if guard:
        return guard

    users      = []
    total_count = 0

    result = run_c_binary("users", ["list_users"])

    if result["status"] == "SUCCESS":
        lines = result["raw_output"].strip().split("\n")

        # lines[0] → "SUCCESS|<count>"
        # lines[1+] → pipe-delimited user records
        if lines:
            header_parts = lines[0].split("|")
            if len(header_parts) >= 2:
                try:
                    total_count = int(header_parts[1])
                except ValueError:
                    total_count = 0

        for line in lines[1:]:
            line = line.strip()
            if not line:
                continue
            parts = line.split("|")
            if len(parts) >= 7:
                users.append({
                    "user_id":   parts[0],
                    "username":  parts[1],
                    "full_name": parts[2],
                    "email":     parts[3],
                    "phone":     parts[4],
                    "address":   parts[5],
                    "status":    parts[6],
                })

    return render_template(
        "admin_users.html",
        users       = users,
        total_count = total_count,
        admin_name  = session.get("admin_name", "Admin"),
    )

@app.route("/admin_orders")
def admin_orders():
    guard = _require_login(role="admin")
    if guard: return guard

    # ── Determine the active delivery slot by current hour ───
    current_hour = datetime.now().hour
    if   7  <= current_hour <= 10: active_slot = "Morning"
    elif 12 <= current_hour <= 14: active_slot = "Afternoon"
    elif 17 <= current_hour <= 19: active_slot = "Evening"
    else:                          active_slot = None

    # ── JIT: auto-promote "Order Placed" → "Out for Delivery" ─
    jit_promoted = 0
    if active_slot:
        # Change from "order" to "delivery"
        jit_result = run_c_binary("delivery", ["batch_promote_slot", active_slot])
        if jit_result["status"] == "SUCCESS":
            try:
                jit_promoted = int(jit_result["data"].strip())
            except ValueError:
                jit_promoted = 0

    return render_template(
        "admin_orders.html",
        username     = session.get("username"),
        admin_name   = session.get("admin_name", "Admin"),
        jit_promoted = jit_promoted,
        jit_slot     = active_slot or ""
    )


@app.route("/product_images/<filename>")
def serve_product_image(filename):
    # Image bridge: serves files from the /images/ root folder.
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return send_from_directory(os.path.join(root_dir, "images"), filename)


# =============================================================
# API ROUTES — Return JSON, called by JavaScript fetch()
#
# RELATIVE PATHS — WHY THIS MATTERS FOR HTTPS:
#   All fetch() calls in HTML/JS use relative paths ("/api/...").
#   A hardcoded "http://..." causes a "Mixed Content" error when
#   the page is served over HTTPS. Relative paths inherit the
#   current scheme+host automatically.
# =============================================================

@app.route("/api/login", methods=["POST"])
def api_login():
    """
    POST /api/login
    Body: { "username": "...", "password": "...", "role": "user|admin" }

    Calls: ./auth login_user <username> <password>
        OR ./auth login_admin <username> <password>

    Session set on success:
      user role:  session["role"], session["username"], session["user_id"]
      admin role: session["role"], session["username"], session["user_id"],
                  session["admin_name"]
    """
    data     = request.get_json() or {}
    username = data.get("username", "").strip()
    password = data.get("password", "").strip()
    role     = data.get("role",     "user").strip()

    if not username or not password:
        return jsonify({"status": "ERROR", "message": "Required fields missing"})

    if role not in ["user", "admin"]:
        return jsonify({"status": "ERROR", "message": "Invalid role"})

    # ── Call the appropriate auth command ─────────────────────
    cmd    = "login_admin" if role == "admin" else "login_user"
    result = run_c_binary("auth", [cmd, username, password])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "ERROR", "message": result["data"]})

    # ── Populate session ──────────────────────────────────────
    session["role"]     = role
    session["username"] = username

    if role == "admin":
        # Admin C output: SUCCESS|admin_id|admin_name
        admin_parts          = result["data"].split("|")
        session["user_id"]   = admin_parts[0] if len(admin_parts) > 0 else "A1001"
        session["admin_name"]= admin_parts[1] if len(admin_parts) > 1 else "Admin"
    else:
        # User C output: SUCCESS|user_id
        session["user_id"] = result["data"]

    redirect_url = "/admin_dash" if role == "admin" else "/user_home"
    return jsonify({"status": "SUCCESS", "role": role, "redirect": redirect_url})


@app.route("/api/register", methods=["POST"])
def api_register():
    """
    POST /api/register
    Body: { "username", "password", "full_name", "email", "phone",
            "door", "street", "area", "pincode" }

    Calls: ./auth register <username> <password> <full_name> <email>
                           <phone> <address>
    Address is assembled as: "door,street,area,pincode"
    """
    data = request.get_json() or {}

    username  = data.get("username",  "")
    password  = data.get("password",  "")
    full_name = data.get("full_name", "")
    email     = data.get("email",     "")
    phone     = data.get("phone",     "")
    door      = data.get("door",      "")
    street    = data.get("street",    "")
    area      = data.get("area",      "")
    pincode   = data.get("pincode",   "")

    required = [username, password, full_name, email, phone, door, street, area, pincode]
    if not all(required):
        return jsonify({"status": "ERROR", "message": "All fields required"})

    address = f"{door},{street},{area},{pincode}"
    result  = run_c_binary("auth", ["register", username, password,
                                    full_name, email, phone, address])

    message = "Registration successful" if result["status"] == "SUCCESS" else result["data"]
    return jsonify({"status": result["status"], "message": message})


@app.route("/api/get_admin_info", methods=["POST"])
def api_get_admin_info():
    """
    POST /api/get_admin_info
    Returns the current admin's session fields as JSON.
    Used by the admin dashboard to personalise the header.
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"})

    return jsonify({
        "status":   "SUCCESS",
        "user_id":  session.get("user_id",    "—"),
        "username": session.get("username",   "—"),
        "name":     session.get("admin_name", "—")
    })


@app.route("/api/get_profile", methods=["POST"])
def api_get_profile():
    """
    POST /api/get_profile
    Calls: ./auth get_profile <user_id>

    C output: SUCCESS|user_id|username|full_name|email|phone|address
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"})

    result = run_c_binary("auth", ["get_profile", session["user_id"]])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "ERROR", "message": result["data"]})

    parts = result["data"].split("|")
    return jsonify({
        "status":    "SUCCESS",
        "user_id":   parts[0] if len(parts) > 0 else "",
        "username":  parts[1] if len(parts) > 1 else "",
        "full_name": parts[2] if len(parts) > 2 else "",
        "email":     parts[3] if len(parts) > 3 else "",
        "phone":     parts[4] if len(parts) > 4 else "",
        "address":   parts[5] if len(parts) > 5 else "",
    })


@app.route("/api/update_profile", methods=["POST"])
def api_update_profile():
    """
    POST /api/update_profile
    Body: { "field": "full_name|email|phone|address", "value": "..." }
    Calls: ./auth update_profile <user_id> <field> <new_value>
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"})

    data      = request.get_json() or {}
    field     = data.get("field", "").strip()
    new_value = data.get("value", "").strip()

    ALLOWED_FIELDS = {"full_name", "email", "phone", "address"}
    if field not in ALLOWED_FIELDS:
        return jsonify({"status": "ERROR", "message": "Invalid field"})
    if not new_value:
        return jsonify({"status": "ERROR", "message": "Value cannot be empty"})

    result = run_c_binary("auth", ["update_profile", session["user_id"],
                                   field, new_value])
    return jsonify({"status": result["status"], "message": result["data"]})


@app.route("/api/change_password", methods=["POST"])
def api_change_password():
    """
    POST /api/change_password
    Body: { "old_password": "...", "new_password": "..." }

    Routes to the correct C command based on session role:
      user  → ./auth change_pass_user  <user_id> <old> <new>
      admin → ./auth change_pass_admin <user_id> <old> <new>
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"})

    data  = request.get_json() or {}
    old_p = data.get("old_password", "").strip()
    new_p = data.get("new_password", "").strip()

    cmd = "change_pass_admin" if session.get("role") == "admin" else "change_pass_user"
    result = run_c_binary("auth", [cmd, session["user_id"], old_p, new_p])

    return jsonify({"status": result["status"], "message": result["data"]})


@app.route("/api/list_products", methods=["GET"])
def api_list_products():
    """
    GET /api/list_products
    Calls: ./order list_products

    C output (multi-line):
      SUCCESS|
      veg_id|category|name|stock_g|price_per_1000g|tag|validity_days
      ...
    """
    result = run_c_binary("order", ["list_products"])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "ERROR", "message": result["data"]})

    products = []
    for line in result["raw_output"].strip().split("\n")[1:]:
        parts = line.strip().split("|")
        if len(parts) >= 7:
            products.append({
                "veg_id":          parts[0],
                "category":        parts[1],
                "name":            parts[2],
                "stock_g":         int(parts[3]),
                "price_per_1000g": float(parts[4]),
                "tag":             parts[5],
                "validity_days":   int(parts[6])
            })

    return jsonify({"status": "SUCCESS", "products": products})


@app.route("/api/update_stock", methods=["POST"])
def api_update_stock():
    """
    POST /api/update_stock  (admin only)
    Body: { "veg_id": "V1001", "stock_g": 75000, "price": 45.50, "validity": 10 }
    Calls: ./inventory update_stock <veg_id> <stock_g> <price> <validity>
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"})

    data     = request.get_json() or {}
    veg_id   = data.get("veg_id",   "").strip()
    stock_g  = int(data.get("stock_g",  0))
    price    = float(data.get("price",  0))
    validity = int(data.get("validity", 1))

    if not veg_id:
        return jsonify({"status": "ERROR", "message": "veg_id required"})

    result = run_c_binary("inventory", [
        "update_stock", veg_id, str(stock_g), str(price), str(validity)
    ])
    status = "SUCCESS" if result["status"] == "SUCCESS" else "ERROR"
    return jsonify({"status": status, "message": result["data"]})


@app.route("/api/update_promo_stock", methods=["POST"])
def api_update_promo_stock():
    """
    POST /api/update_promo_stock  (admin only)
    Body: { "vf_id": "VF1001", "stock_g": 5000 }
    Calls: ./inventory update_promo_stock <vf_id> <stock_g>
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"})

    data    = request.get_json() or {}
    vf_id   = data.get("vf_id",   "").strip()
    stock_g = int(data.get("stock_g", 0))

    if not vf_id:
        return jsonify({"status": "ERROR", "message": "vf_id required"})

    result = run_c_binary("inventory", ["update_promo_stock", vf_id, str(stock_g)])
    status = "SUCCESS" if result["status"] == "SUCCESS" else "ERROR"
    return jsonify({"status": status, "message": result["data"]})


@app.route("/api/add_to_cart", methods=["POST"])
def api_add_to_cart():
    """
    POST /api/add_to_cart
    Body: { "veg_id": "V1001", "qty_g": 500 }
    Calls: ./order add_to_cart <user_id> <veg_id> <qty_g>
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"})

    data   = request.get_json() or {}
    veg_id = data.get("veg_id", "").strip()
    qty_g  = int(data.get("qty_g", 0))

    result = run_c_binary("order", ["add_to_cart", session["user_id"],
                                    veg_id, str(qty_g)])
    return jsonify({"status": result["status"], "message": result["data"]})


@app.route("/api/view_cart", methods=["POST"])
def api_view_cart():
    """
    POST /api/view_cart
    Calls: ./order view_cart <user_id>

    C output (multi-line):
      SUCCESS|<cart_total>
      veg_id|name|qty_g|price_per_1000g|item_total|is_free
      ...
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"})

    result = run_c_binary("order", ["view_cart", session["user_id"]])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "ERROR", "message": result["data"]})

    lines = result["raw_output"].strip().split("\n")
    items = []

    for line in lines[1:]:
        parts = line.strip().split("|")
        if len(parts) >= 6:
            items.append({
                "veg_id":          parts[0],
                "name":            parts[1],
                "qty_g":           int(parts[2]),
                "price_per_1000g": float(parts[3]),
                "item_total":      float(parts[4]),
                "is_free":         int(parts[5])
            })

    cart_total = _safe_float(lines[0].split("|")[1]) if lines else 0.0
    return jsonify({"status": "SUCCESS", "total": cart_total, "items": items})


@app.route("/api/update_cart_qty", methods=["POST"])
def api_update_cart_qty():
    """
    POST /api/update_cart_qty
    Body: { "veg_id": "V1001", "qty_g": 750 }

    Re-uses add_to_cart which performs an update_or_append in C.
    Calls: ./order add_to_cart <user_id> <veg_id> <qty_g>
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"})

    data   = request.get_json() or {}
    veg_id = data.get("veg_id", "").strip()
    qty_g  = int(data.get("qty_g", 0))

    if not veg_id or qty_g <= 0:
        return jsonify({"status": "ERROR", "message": "Invalid veg_id or qty_g"})

    result = run_c_binary("order", ["add_to_cart", session["user_id"],
                                    veg_id, str(qty_g)])
    return jsonify({"status": result["status"], "message": result["data"]})


@app.route("/api/remove_item", methods=["POST"])
def api_remove_item():
    """
    POST /api/remove_item
    Body: { "veg_id": "V1001" }
    Calls: ./order remove_item <user_id> <veg_id>
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"})

    data   = request.get_json() or {}
    veg_id = data.get("veg_id", "").strip()

    result = run_c_binary("order", ["remove_item", session["user_id"], veg_id])
    return jsonify({"status": result["status"], "message": result["data"]})


@app.route("/api/checkout", methods=["POST"])
def api_checkout():
    """
    POST /api/checkout
    Body: { "delivery_slot": "Morning|Afternoon|Evening" }
    Calls: ./order checkout <user_id> <delivery_slot>

    C output (first line):
      SUCCESS|order_id|total|slot|boy_name|boy_phone|items_string
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"})

    data = request.get_json() or {}
    slot = data.get("delivery_slot", "").strip()

    VALID_SLOTS = {"Morning", "Afternoon", "Evening"}
    if slot not in VALID_SLOTS:
        return jsonify({"status": "ERROR", "message": "Invalid slot"})

    result = run_c_binary("order", ["checkout", session["user_id"], slot])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "ERROR", "message": result["data"]})

    parts = result["raw_output"].strip().split("\n")[0].split("|")
    return jsonify({
        "status":   "SUCCESS",
        "order_id": parts[1] if len(parts) > 1 else "",
        "total":    _safe_float(parts[2]) if len(parts) > 2 else 0.0,
        "slot":     parts[3] if len(parts) > 3 else "",
        "boy_name": parts[4] if len(parts) > 4 else "Unknown",
        "boy_phone":parts[5] if len(parts) > 5 else "N/A",
        "items":    parts[6] if len(parts) > 6 else ""
    })


@app.route("/api/get_user_orders", methods=["POST"])
def api_get_user_orders():
    """
    POST /api/get_user_orders
    Calls: ./order get_orders <user_id>

    C output: multi-line, header + one order per line.
    SCHEMA: order_id|user_id|total|slot|boy_id|status|timestamp|items_string
            [|boy_name|boy_phone]
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"})

    result = run_c_binary("order", ["get_orders", session["user_id"]])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "ERROR", "message": result["data"]})

    orders = _parse_multiline_orders(result["raw_output"])
    return jsonify({"status": "SUCCESS", "orders": orders})


@app.route("/api/admin_orders", methods=["GET"])
def api_admin_orders():
    """
    GET /api/admin_orders  (admin only)
    Calls: ./order admin_orders

    Returns heap-sorted (priority by slot) full order list.
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"})

    result = run_c_binary("order", ["admin_orders"])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "ERROR", "message": result["data"]})

    orders = _parse_multiline_orders(result["raw_output"])
    return jsonify({"status": "SUCCESS", "orders": orders})


@app.route("/api/get_admin_orders", methods=["POST"])
def api_get_admin_orders():
    """
    POST /api/get_admin_orders  (admin only)
    Delegates to: ./delivery list_all_orders_sorted

    Returns all orders newest-first, enriched with boy_name + boy_phone.
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin access required"}), 403

    result = run_c_binary("delivery", ["list_all_orders_sorted"])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "SUCCESS", "orders": [],
                        "warning": result.get("data", "")})

    orders = _parse_multiline_orders(result["raw_output"])
    return jsonify({"status": "SUCCESS", "orders": orders})


@app.route("/api/update_order_status", methods=["POST"])
def api_update_order_status():
    """
    POST /api/update_order_status  (admin only)
    Body: { "order_id": "ORD1007", "status": "Out for Delivery" }
    Calls: ./delivery update_status <order_id> <status>

    Valid status values: "Order Placed" | "Out for Delivery" |
                         "Delivered"    | "Cancelled"
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin access required"}), 403

    data       = request.get_json(silent=True) or {}
    order_id   = data.get("order_id",  "").strip()
    new_status = data.get("status",    "").strip()

    VALID_STATUSES = {"Order Placed", "Out for Delivery", "Delivered", "Cancelled"}

    if not order_id:
        return jsonify({"status": "ERROR", "message": "order_id is required"})
    if new_status not in VALID_STATUSES:
        return jsonify({"status": "ERROR", "message": f"Invalid status: {new_status}"})

    result = run_c_binary("delivery", ["update_status", order_id, new_status])
    return jsonify({"status": result["status"], "message": result.get("data", "")})


@app.route("/api/promote_slot_orders", methods=["POST"])
def api_promote_slot_orders():
    """
    POST /api/promote_slot_orders  (admin only)
    Body: { "slot": "Morning|Afternoon|Evening" }
    Calls: ./delivery batch_promote_slot <slot>
    Returns: { "status": "SUCCESS", "promoted": <int> }
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"})

    slot = (request.get_json(silent=True) or {}).get("slot", "").strip()

    if slot not in {"Morning", "Afternoon", "Evening"}:
        return jsonify({"status": "ERROR", "message": "Invalid slot"})

    result = run_c_binary("delivery", ["batch_promote_slot", slot])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "ERROR", "message": result.get("data", "")})

    try:
        promoted = int(result["data"].strip())
    except (ValueError, AttributeError):
        promoted = 0

    return jsonify({"status": "SUCCESS", "promoted": promoted})


@app.route("/api/cancel_order", methods=["POST"])
def api_cancel_order():
    """
    POST /api/cancel_order
    Body: { "order_id": "ORD1007" }

    Business rules enforced in delivery.c:
      - Only "Order Placed" orders may be cancelled.
    Calls: ./delivery cancel_order <order_id>
    Returns: { "status": "SUCCESS"|"ERROR", "message": "..." }
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"}), 401

    data     = request.get_json(silent=True) or {}
    order_id = data.get("order_id", "").strip()

    if not order_id:
        return jsonify({"status": "ERROR", "message": "order_id is required"})

    result = run_c_binary("delivery", ["cancel_order", order_id])
    return jsonify({"status": result["status"], "message": result.get("data", "")})


@app.route("/api/get_active_orders", methods=["GET"])
def api_get_active_orders():
    """
    GET /api/get_active_orders  (admin only)
    Returns only "Order Placed" and "Out for Delivery" orders,
    enriched with boy_name + boy_phone.
    Calls: ./delivery get_active_orders
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"}), 403

    result = run_c_binary("delivery", ["get_active_orders"])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "SUCCESS", "orders": [],
                        "warning": result.get("data", "")})

    orders = _parse_multiline_orders(result["raw_output"])
    return jsonify({"status": "SUCCESS", "orders": orders})


@app.route("/api/assign_agent", methods=["POST"])
def api_assign_agent():
    """
    POST /api/assign_agent  (admin only)
    Body: { "order_id": "ORD1007", "boy_id": "D1003" }
    Calls: ./delivery assign_agent <order_id> <boy_id>
    Returns: { "status": "SUCCESS", "message": "Agent assigned" }
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"}), 403

    data     = request.get_json(silent=True) or {}
    order_id = data.get("order_id", "").strip()
    boy_id   = data.get("boy_id",   "").strip()

    if not order_id or not boy_id:
        return jsonify({"status": "ERROR",
                        "message": "order_id and boy_id are required"})

    result = run_c_binary("delivery", ["assign_agent", order_id, boy_id])
    return jsonify({"status": result["status"], "message": result.get("data", "")})


@app.route("/api/download_receipt/<order_id>", methods=["GET"])
def api_download_receipt(order_id):
    """
    GET /api/download_receipt/<order_id>

    1. Calls the `receipt` C binary with the order_id.
    2. Parses the 13-field pipe-delimited stdout into a data dict.
    3. Generates a PDF via generate_receipt.py.
    4. Streams the PDF back as a file attachment.

    Accessible by both logged-in users AND admins.

    C output FORMAT (13 fields after SUCCESS):
      SUCCESS|order_id|user_id|full_name|user_phone|user_email|
              address|slot|status|timestamp|boy_name|boy_phone|
              total|items_string
    """
    if "user_id" not in session and session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Not logged in"}), 401

    # ── Step 1: Call the receipt binary ──────────────────────
    result = run_c_binary("receipt", [order_id])

    if result["status"] != "SUCCESS":
        return jsonify({
            "status":  "ERROR",
            "message": result.get("data", "Could not fetch order data")
        }), 404

    # ── Step 2: Parse the pipe-delimited output ───────────────
    raw   = result["raw_output"].strip().split("\n")[0]
    parts = raw.split("|")

    # parts[0] = "SUCCESS", parts[1..13] = data fields
    if len(parts) < 14:
        return jsonify({
            "status":  "ERROR",
            "message": f"Malformed receipt data ({len(parts)} fields)"
        }), 500

    receipt_data = {
        "order_id":    parts[1],
        "user_id":     parts[2],
        "full_name":   parts[3],
        "user_phone":  parts[4],
        "user_email":  parts[5],
        "address":     parts[6],
        "slot":        parts[7],
        "status":      parts[8],
        "timestamp":   parts[9],
        "boy_name":    parts[10],
        "boy_phone":   parts[11],
        "total":       _safe_float(parts[12]),
        # items_string may itself contain "|" if item names do —
        # join remaining parts back to preserve them.
        "items_string": "|".join(parts[13:]),
    }

    # ── Step 3: Generate the PDF to a temp file ───────────────
    try:
        with tempfile.NamedTemporaryFile(
            suffix=".pdf",
            delete=False,
            prefix=f"{receipt_data['order_id']}_{receipt_data['user_id']}_"
        ) as tmp:
            tmp_path = tmp.name

        generate_receipt(receipt_data, tmp_path)

    except Exception as e:
        return jsonify({
            "status":  "ERROR",
            "message": f"PDF generation failed: {str(e)}"
        }), 500

    # ── Step 4: Stream the PDF back to the browser ────────────
    filename = f"{receipt_data['order_id']}_{receipt_data['user_id']}.pdf"
    return send_file(
        tmp_path,
        mimetype="application/pdf",
        as_attachment=True,
        download_name=filename
    )

# =============================================================
# BLOCK 2 — API ROUTES
# Add these before the "START THE SERVER" section.
# =============================================================

@app.route("/api/email_receipt/<order_id>", methods=["POST"])
def api_email_receipt(order_id):
    """
    POST /api/email_receipt/<order_id>
    Generate the same PDF receipt and email it to the customer.
    """
    if "user_id" not in session and session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Not logged in"}), 401

    receipt_data, error = _get_receipt_data(order_id)
    if error:
        return jsonify({"status": "ERROR", "message": error}), 404

    if session.get("role") == "user" and receipt_data.get("user_id") != session.get("user_id"):
        return jsonify({"status": "ERROR", "message": "This receipt belongs to another user"}), 403

    tmp_path, error = _generate_receipt_pdf(receipt_data)
    if error:
        return jsonify({"status": "ERROR", "message": error}), 500

    try:
        error = _send_receipt_email(receipt_data, tmp_path)
    finally:
        try:
            os.remove(tmp_path)
        except OSError:
            pass

    if error:
        return jsonify({"status": "ERROR", "message": error}), 500

    return jsonify({
        "status": "SUCCESS",
        "message": f"Receipt emailed to {receipt_data.get('user_email', '')}"
    })


@app.route("/api/admin/list_users", methods=["GET"])
def api_admin_list_users():
    """
    GET /api/admin/list_users?filter=active|inactive  (admin only)

    Query params:
      filter — optional: "active" or "inactive" (omit for all users)

    Calls users.exe list_users [filter] and returns a JSON array.
    Used by the frontend for dynamic re-filtering without a page reload.

    Returns:
      { "status": "SUCCESS", "users": [...], "total": N }
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"}), 403

    status_filter = request.args.get("filter", "").strip().lower()
    args = ["list_users"]
    if status_filter in ("active", "inactive"):
        args.append(status_filter)

    result = run_c_binary("users", args)

    if result["status"] != "SUCCESS":
        return jsonify({
            "status":  "ERROR",
            "message": result.get("data", "Could not load users"),
            "users":   [],
            "total":   0,
        })

    users       = []
    total_count = 0
    lines       = result["raw_output"].strip().split("\n")

    if lines:
        header_parts = lines[0].split("|")
        if len(header_parts) >= 2:
            try:
                total_count = int(header_parts[1])
            except ValueError:
                total_count = 0

    for line in lines[1:]:
        line = line.strip()
        if not line:
            continue
        parts = line.split("|")
        if len(parts) >= 7:
            users.append({
                "user_id":   parts[0],
                "username":  parts[1],
                "full_name": parts[2],
                "email":     parts[3],
                "phone":     parts[4],
                "address":   parts[5],
                "status":    parts[6],
            })

    return jsonify({"status": "SUCCESS", "users": users, "total": total_count})


@app.route("/api/admin/search_users", methods=["POST"])
def api_admin_search_users():
    """
    POST /api/admin/search_users  (admin only)
    Body: { "query": "Ravi" }   OR   { "query": "U1003" }

    Calls users.exe search_users <query> and returns a JSON array.
    The C binary handles both user_id (exact) and full_name (substring) matching.

    Returns:
      { "status": "SUCCESS", "users": [...], "total": N }
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"}), 403

    data  = request.get_json(silent=True) or {}
    query = data.get("query", "").strip()

    if not query:
        return jsonify({"status": "ERROR", "message": "query is required"})

    result = run_c_binary("users", ["search_users", query])

    if result["status"] != "SUCCESS":
        return jsonify({
            "status":  "ERROR",
            "message": result.get("data", "Search failed"),
            "users":   [],
            "total":   0,
        })

    users       = []
    total_count = 0
    lines       = result["raw_output"].strip().split("\n")

    if lines:
        header_parts = lines[0].split("|")
        if len(header_parts) >= 2:
            try:
                total_count = int(header_parts[1])
            except ValueError:
                total_count = 0

    for line in lines[1:]:
        line = line.strip()
        if not line:
            continue
        parts = line.split("|")
        if len(parts) >= 7:
            users.append({
                "user_id":   parts[0],
                "username":  parts[1],
                "full_name": parts[2],
                "email":     parts[3],
                "phone":     parts[4],
                "address":   parts[5],
                "status":    parts[6],
            })

    return jsonify({"status": "SUCCESS", "users": users, "total": total_count})


@app.route("/api/admin/get_user", methods=["POST"])
def api_admin_get_user():
    """
    POST /api/admin/get_user  (admin only)
    Body: { "user_id": "U1003" }

    Fetches the full profile of a single user. Used by the
    "View Details" modal on admin_users.html.

    Calls users.exe get_user <user_id>

    Returns:
      { "status": "SUCCESS", "user": { ...all fields... } }
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"}), 403

    data    = request.get_json(silent=True) or {}
    user_id = data.get("user_id", "").strip()

    if not user_id:
        return jsonify({"status": "ERROR", "message": "user_id is required"})

    result = run_c_binary("users", ["get_user", user_id])

    if result["status"] != "SUCCESS":
        return jsonify({
            "status":  "ERROR",
            "message": result.get("data", "User not found"),
        })

    # Output: SUCCESS|user_id|username|full_name|email|phone|address|status
    raw   = result["raw_output"].strip().split("\n")[0]
    parts = raw.split("|")

    # parts[0] = "SUCCESS", parts[1..7] = user fields
    if len(parts) < 8:
        return jsonify({
            "status":  "ERROR",
            "message": f"Malformed user data ({len(parts)} fields)",
        })

    user = {
        "user_id":   parts[1],
        "username":  parts[2],
        "full_name": parts[3],
        "email":     parts[4],
        "phone":     parts[5],
        "address":   parts[6],
        "status":    parts[7],
    }

    return jsonify({"status": "SUCCESS", "user": user})

# =============================================================
# START THE SERVER — with SSL/HTTPS
# =============================================================

if __name__ == "__main__":

    # ─────────────────────────────────────────────────────────
    # SSL Context Setup
    # ssl.SSLContext wraps our cert.pem + key.pem and instructs
    # Flask/Werkzeug to wrap every TCP connection in TLS.
    # ─────────────────────────────────────────────────────────
    ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)

    try:
        ssl_ctx.load_cert_chain(certfile=CERT_FILE, keyfile=KEY_FILE)
    except FileNotFoundError:
        print()
        print("  ERROR: cert.pem or key.pem not found!")
        print("  Fix:   cd Fresh-Picks/app && python generate_certs.py")
        print()
        exit(1)

    # ─────────────────────────────────────────────────────────
    # Launch Mode: VERSION B (Wi-Fi / Demo Mode) active.
    # Comment HOST = "0.0.0.0" and uncomment HOST = "127.0.0.1"
    # to switch to VERSION A (local testing only).
    # ─────────────────────────────────────────────────────────

    # VERSION A — Local testing (single laptop, invisible to network)
    # HOST = "127.0.0.1"

    # VERSION B — Demo mode (all devices on same Wi-Fi can connect)
    HOST  = "0.0.0.0"
    PORT  = 5000
    DEBUG = True

    import socket

    def get_local_ip():
        """
        Detect the machine's active LAN/Wi-Fi IPv4 address without
        sending any real traffic. Falls back to 127.0.0.1 on failure.
        """
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            return "127.0.0.1"

    LOCAL_IP = get_local_ip()

    print()
    print("=" * 62)
    print("  FreshPicks  |  CodeCrafters  |  SDP-1  |  HTTPS Server")
    print("=" * 62)
    print()

    if HOST == "127.0.0.1":
        print("  Mode        :  VERSION A  (Local only)")
        print(f"  Home         :  https://localhost:{PORT}/")
        print(f"  Admin Portal :  https://localhost:{PORT}/login/admin")
        print(f"  User Portal  :  https://localhost:{PORT}/login/user")
        print()
        print("  Not visible to other devices on this network.")
    else:
        print("  Mode        :  VERSION B  (Wi-Fi / Demo Mode)")
        print()
        print(f"  Home         :  https://{LOCAL_IP}:{PORT}/")
        print(f"  Admin Portal :  https://{LOCAL_IP}:{PORT}/login/admin")
        print(f"  User Portal  :  https://{LOCAL_IP}:{PORT}/login/user")
        print()
        print(f"  Localhost    :  https://localhost:{PORT}/")
        print()
        print("  Share the Wi-Fi URLs above with teammates.")
        print("  Each device: Advanced -> Proceed (accept self-signed cert).")

    print()
    print("  Browser shows 'Not private'? That is expected (self-signed cert).")
    print("=" * 62)
    print()

    app.run(
        host        = HOST,
        port        = PORT,
        debug       = DEBUG,
        ssl_context = ssl_ctx
    )
