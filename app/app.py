"""
app.py - Fresh Picks: Main Flask Application
"""

import ssl
import os
import re
import secrets
import tempfile
import razorpay
import hashlib
from datetime import datetime, timedelta
from config import RAZORPAY_KEY_ID, RAZORPAY_KEY_SECRET, APP_SECRET_KEY

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
from flask_cors import CORS

_rzp_client = razorpay.Client(auth=(RAZORPAY_KEY_ID, RAZORPAY_KEY_SECRET))
OTP_STORE = {}
OTP_TTL_MINUTES = 10

# ─────────────────────────────────────────────────────────────
# Flask App Setup
# ─────────────────────────────────────────────────────────────
app = Flask(
    __name__,
    template_folder="../templates",
    static_folder="../static"
)


# ─────────────────────────────────────────────────────────────
# Secret Key
# ─────────────────────────────────────────────────────────────
app.secret_key = APP_SECRET_KEY

# ─────────────────────────────────────────────────────────────
# SSL Certificate Paths (Relative)
# ─────────────────────────────────────────────────────────────
_HERE     = os.path.dirname(os.path.abspath(__file__))
CERT_FILE = os.path.join(_HERE, "cert.pem")
KEY_FILE  = os.path.join(_HERE, "key.pem")


# =============================================================
# PRIVATE HELPERS FUNCTIONS
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

def _hash_password(plaintext: str) -> str:
    """
    PURPOSE: One-way SHA-256 digest for password storage.
             Returns a 64-char lowercase hex string that fits
             exactly in auth.c's char password[MAX_STR_LEN] (128).
    PARAMS:  plaintext — the raw password string received from the client.
    RETURNS: 64-char hex digest.
    """
    return hashlib.sha256(plaintext.encode("utf-8")).hexdigest()


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


def _parse_veg_line(line):
    """
    PURPOSE: Parse one pipe-delimited vegetable record into a dict.
            Ensures stock, price, and validity are typed correctly for Jinja.
    PARAMS:  line — one stripped line of raw C stdout output from list_products.
    RETURNS: dict or None if the line is malformed.

    SCHEMA:  veg_id|category|name|stock_g|price_per_1000g|tag|validity_days
    """
    parts = line.strip().split("|")
    if len(parts) < 7: return None
    return {
        "veg_id":          parts[0],
        "category":        parts[1],
        "name":            parts[2],
        "stock_g":         int(parts[3]),
        "price_per_1000g": _safe_float(parts[4]),
        "tag":             parts[5],
        "validity_days":   int(parts[6])
    }


def _parse_promo_line(line):
    """
    PURPOSE: Parse one pipe-delimited promotional freebie record into a dict.
            Used for rendering the admin promo management section.
    PARAMS:  line — one stripped line of raw C stdout output from list_promo.
    RETURNS: dict or None if the line has fewer than 5 fields.

    SCHEMA:  vf_id|name|stock_g|min_trigger_amt|free_qty_g
    """
    parts = line.strip().split("|")
    if len(parts) < 5: return None
    return {
        "vf_id":           parts[0],
        "name":            parts[1],
        "stock_g":         int(parts[2]),
        "min_trigger_amt": _safe_float(parts[3]),
        "free_qty_g":      int(parts[4])
    }



def _parse_analytics_output(raw_output):
    """
    PURPOSE: Parse the multi-line stdout from ./analytics get_analytics
             into a typed Python dictionary.
    PARAMS:  raw_output — full stdout string captured from run_c_binary().
    RETURNS: dict with all metric keys cast to int or float, or None if
             the first line is not "SUCCESS".

    SCHEMA (key|value, one per line after SUCCESS):
        total_revenue          float  — sum of all order totals
        total_orders           int    — count of all orders
        avg_order_value        float  — total_revenue / total_orders
        orders_placed          int    — status == "Order Placed"
        orders_out             int    — status == "Out for Delivery"
        orders_delivered       int    — status == "Delivered"
        orders_cancelled       int    — status == "Cancelled"
        slot_morning           int    — delivery_slot == "Morning"
        slot_afternoon         int    — delivery_slot == "Afternoon"
        slot_evening           int    — delivery_slot == "Evening"
        total_stock_kg         float  — sum of all veg stock_g / 1000
        low_stock_items        int    — count of vegs with stock_g < 5000
        total_users            int    — count of registered users
        active_delivery_boys   int    — delivery boys with is_active == 1
        inactive_delivery_boys int    — delivery boys with is_active == 0
    """
    _INT_KEYS = {
        "total_orders", "orders_placed", "orders_out",
        "orders_delivered", "orders_cancelled",
        "slot_morning", "slot_afternoon", "slot_evening",
        "low_stock_items", "total_users",
        "active_delivery_boys", "inactive_delivery_boys",
    }

    lines = raw_output.strip().split("\n")

    if not lines or lines[0].strip() != "SUCCESS|":
        return None

    stats = {}
    for line in lines[1:]:
        line = line.strip()
        if not line or "|" not in line:
            continue
        key, _, raw_val = line.partition("|")
        key = key.strip()
        if key in _INT_KEYS:
            stats[key] = int(raw_val.strip())
        else:
            stats[key] = _safe_float(raw_val.strip())

    return stats if stats else None



def _load_receipt_data(order_id):
    """
    PURPOSE: Fetch one order's receipt payload from the C receipt binary.
    RETURNS: (receipt_data_dict, error_response_tuple_or_None)
    """
    result = run_c_binary("receipt", [order_id])

    if result["status"] != "SUCCESS":
        return None, (
            jsonify({
                "status":  "ERROR",
                "message": result.get("data", "Could not fetch order data")
            }),
            404
        )

    raw   = result["raw_output"].strip().split("\n")[0]
    parts = raw.split("|")

    if len(parts) < 14:
        return None, (
            jsonify({
                "status":  "ERROR",
                "message": f"Malformed receipt data ({len(parts)} fields)"
            }),
            500
        )

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
        # items_string may itself contain "|" if item names do —
        # join remaining parts back to preserve them.
        "items_string": "|".join(parts[13:]),
    }, None


def _generate_receipt_pdf_file(receipt_data):
    """
    PURPOSE: Render a receipt PDF to a temp file and return its absolute path.
    """
    with tempfile.NamedTemporaryFile(
        suffix=".pdf",
        delete=False,
        prefix=f"{receipt_data['order_id']}_{receipt_data['user_id']}_"
    ) as tmp:
        tmp_path = tmp.name

    generate_receipt(receipt_data, tmp_path)
    return tmp_path


def _build_receipt_pdf(order_id):
    """
    PURPOSE: Shared helper for both download and email receipt flows.
    RETURNS: (receipt_data_dict, pdf_path, error_response_tuple_or_None)
    """
    receipt_data, error_response = _load_receipt_data(order_id)
    if error_response:
        return None, None, error_response

    try:
        pdf_path = _generate_receipt_pdf_file(receipt_data)
    except Exception as e:
        return None, None, (
            jsonify({
                "status":  "ERROR",
                "message": f"PDF generation failed: {str(e)}"
            }),
            500
        )

    return receipt_data, pdf_path, None


def _cleanup_temp_file(path):
    """
    PURPOSE: Best-effort removal for generated receipt files.
    """
    if path and os.path.exists(path):
        os.remove(path)


def _purge_expired_otps():
    """
    PURPOSE: Remove expired OTP entries from the in-memory store.
    """
    now_ts = datetime.utcnow().timestamp()
    expired_keys = [
        key for key, value in OTP_STORE.items()
        if value.get("expires_at", 0) <= now_ts
    ]
    for key in expired_keys:
        OTP_STORE.pop(key, None)


def _get_otp_client_token():
    """
    PURPOSE: Stable per-browser token so OTP values stay server-side.
    """
    client_token = session.get("otp_client_token")
    if not client_token:
        client_token = secrets.token_urlsafe(16)
        session["otp_client_token"] = client_token
    return client_token


def _otp_store_key(flow_key):
    """
    PURPOSE: Namespaced key for one OTP flow in the in-memory store.
    """
    return f"{_get_otp_client_token()}:{flow_key}"


def _generate_otp_code():
    """
    PURPOSE: Generate a 4-digit OTP for user-facing flows.
    """
    return f"{secrets.randbelow(10000):04d}"


def _send_otp_email(recipient_email, otp_code, purpose, reference=""):
    """
    PURPOSE: Use the shared C mailer binary to send an OTP email.
    """
    args = ["otp", recipient_email, otp_code, purpose]
    if reference:
        args.append(reference)
    return run_c_binary("mailer", args)


def _is_valid_email(value):
    """
    PURPOSE: Lightweight email sanity check for OTP recipients.
    """
    return bool(value and "@" in value and " " not in value)


def _is_strong_password(value):
    """
    PURPOSE: Server-side password policy for registration and password change.
    """
    return bool(
        value and
        len(value) >= 8 and
        re.search(r"[A-Z]", value) and
        re.search(r"[a-z]", value) and
        re.search(r"[0-9]", value) and
        re.search(r"[^A-Za-z0-9]", value)
    )


def _start_otp_flow(flow_key, recipient_email, purpose, reference="", payload=None):
    """
    PURPOSE: Generate, store, and email an OTP for one flow.
    """
    _purge_expired_otps()

    otp_code = _generate_otp_code()
    OTP_STORE[_otp_store_key(flow_key)] = {
        "otp": otp_code,
        "recipient_email": recipient_email,
        "purpose": purpose,
        "reference": reference,
        "expires_at": (
            datetime.utcnow() + timedelta(minutes=OTP_TTL_MINUTES)
        ).timestamp()
    }
    if payload is not None:
        OTP_STORE[_otp_store_key(flow_key)]["payload"] = payload
    return _send_otp_email(recipient_email, otp_code, purpose, reference)


def _restart_otp_flow(flow_key, otp_state):
    """
    PURPOSE: Re-issue an OTP using the existing flow metadata.
    """
    return _start_otp_flow(
        flow_key,
        otp_state["recipient_email"],
        otp_state["purpose"],
        otp_state.get("reference", ""),
        otp_state.get("payload")
    )


def _get_otp_flow(flow_key):
    """
    PURPOSE: Return a non-expired OTP flow state, if present.
    """
    _purge_expired_otps()
    return OTP_STORE.get(_otp_store_key(flow_key))


def _clear_otp_flow(flow_key):
    """
    PURPOSE: Remove one OTP flow state after success or abandonment.
    """
    OTP_STORE.pop(_otp_store_key(flow_key), None)


def _validate_otp_flow(flow_key, entered_code, reference=""):
    """
    PURPOSE: Check whether the user-entered OTP matches the stored one.
    """
    otp_state = _get_otp_flow(flow_key)
    if not otp_state:
        return False, "OTP expired or not found. Please request a new OTP."

    if reference and otp_state.get("reference") != reference:
        return False, "OTP context mismatch. Please request a new OTP."

    if otp_state.get("otp") != entered_code:
        return False, "Incorrect OTP. Please try again."

    return True, ""


def _parse_user_profile_data(raw_data):
    """
    PURPOSE: Convert get_profile output into a dict for server-side helpers.
    """
    parts = raw_data.split("|")
    return {
        "user_id": parts[0] if len(parts) > 0 else "",
        "username": parts[1] if len(parts) > 1 else "",
        "full_name": parts[2] if len(parts) > 2 else "",
        "email": parts[3] if len(parts) > 3 else "",
        "phone": parts[4] if len(parts) > 4 else "",
        "address": parts[5] if len(parts) > 5 else "",
    }


def _parse_admin_profile_data(raw_data):
    """
    PURPOSE: Convert get_admin_profile output into a dict for OTP helpers.
    """
    parts = raw_data.split("|")
    return {
        "admin_id": parts[0] if len(parts) > 0 else "",
        "username": parts[1] if len(parts) > 1 else "",
        "admin_name": parts[2] if len(parts) > 2 else "",
        "email": parts[3] if len(parts) > 3 else "",
    }


def _load_current_account_email():
    """
    PURPOSE: Fetch the logged-in account's registered email for OTP delivery.
    RETURNS: (email, error_response_tuple_or_None)
    """
    cached_email = session.get("email", "").strip()
    if _is_valid_email(cached_email):
        return cached_email, None

    if "user_id" not in session:
        return None, (jsonify({"status": "ERROR", "message": "Not logged in"}), 401)

    if session.get("role") == "admin":
        result = run_c_binary("auth", ["get_admin_profile", session["user_id"]])
        if result["status"] != "SUCCESS":
            return None, (
                jsonify({
                    "status": "ERROR",
                    "message": result.get("data", "Could not load admin profile")
                }),
                500
            )
        email = _parse_admin_profile_data(result["data"]).get("email", "").strip()
    else:
        result = run_c_binary("auth", ["get_profile", session["user_id"]])
        if result["status"] != "SUCCESS":
            return None, (
                jsonify({
                    "status": "ERROR",
                    "message": result.get("data", "Could not load user profile")
                }),
                500
            )
        email = _parse_user_profile_data(result["data"]).get("email", "").strip()

    if not _is_valid_email(email):
        return None, (
            jsonify({
                "status": "ERROR",
                "message": "No valid email is registered for this account"
            }),
            400
        )

    session["email"] = email
    return email, None


def _password_change_flow_key():
    """
    PURPOSE: Unique OTP key for the current logged-in account's password flow.
    """
    return f"password_change:{session.get('role', 'user')}:{session.get('user_id', '')}"


def _cancel_order_flow_key(order_id):
    """
    PURPOSE: Separate user/admin cancellation OTP state for the same order.
    """
    return f"cancel_order:{session.get('role', 'guest')}:{order_id}"

# =============================================================
# PAGE ROUTES — Serve HTML templates
# =============================================================

""" RAZORPAY """
@app.after_request
def set_security_headers(response):
    # Use wildcard (*) so the policy delegates down into Razorpay's
    # cross-origin iframe. Quoted-origin syntax doesn't propagate to iframes.
    response.headers["Permissions-Policy"] = (
        "accelerometer=*, gyroscope=*, magnetometer=*"
    )
    # Expose Razorpay's internal tracking headers across the XHR boundary.
    response.headers["Access-Control-Expose-Headers"] = (
        "x-rtb-fingerprint-id, request-id"
    )
    return response

@app.route("/")
def index():
    # Public landing page — no login required.
    return render_template("index.html")


@app.route('/presentation')
def presentation():
    return render_template('presentation.html')


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

    return render_template(
        "admin_inventory.html",
        admin_name = session.get("admin_name", "Admin")
    )

@app.route("/admin_users")
def admin_users():
    guard = _require_login(role="admin")
    if guard:
        return guard

    return render_template(
        "admin_users.html",
        admin_name = session.get("admin_name", "Admin"),
    )


@app.route("/admin_orders")
def admin_orders():
    guard = _require_login(role="admin")
    if guard: return guard

    return render_template(
        "admin_orders.html",
        admin_name = session.get("admin_name", "Admin")
    )


@app.route("/admin_analytics")
def admin_analytics():
    guard = _require_login(role="admin")
    if guard:
        return guard

    return render_template(
        "admin_analytics.html",
        admin_name = session.get("admin_name", "Admin")
    )


@app.route("/product_images/<filename>")
def serve_product_image(filename):
    # Image bridge: serves files from the /images/ root folder.
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return send_from_directory(os.path.join(root_dir, "images"), filename)


# =============================================================
# API ROUTES — Return JSON, called by JavaScript fetch()
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
    cmd      = "login_admin" if role == "admin" else "login_user"
    result   = run_c_binary("auth", [cmd, username, _hash_password(password)])

    if result["status"] != "SUCCESS":
        return jsonify({"status": "ERROR", "message": result["data"]})

    # ── Populate session ──────────────────────────────────────
    session["role"]     = role
    session["username"] = username
    session.pop("email", None)
    session.pop("admin_name", None)

    if role == "admin":
        # Admin C output: SUCCESS|admin_id|admin_name|email
        admin_parts          = result["data"].split("|")
        session["user_id"]   = admin_parts[0] if len(admin_parts) > 0 else "A1001"
        session["admin_name"]= admin_parts[1] if len(admin_parts) > 1 else "Admin"
        session["email"]     = admin_parts[2] if len(admin_parts) > 2 else ""
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

    Stages the registration payload server-side and emails an OTP.
    The actual auth register call only happens after OTP verification.
    """
    data = request.get_json() or {}

    username  = data.get("username",  "").strip()
    password  = data.get("password",  "")
    full_name = data.get("full_name", "").strip()
    email     = data.get("email",     "").strip()
    phone     = data.get("phone",     "").strip()
    door      = data.get("door",      "").strip()
    street    = data.get("street",    "").strip()
    area      = data.get("area",      "").strip()
    pincode   = data.get("pincode",   "").strip()

    required = [username, password, full_name, email, phone, door, street, area, pincode]
    if not all(required):
        return jsonify({"status": "ERROR", "message": "All fields required"})
    if not _is_valid_email(email):
        return jsonify({"status": "ERROR", "message": "A valid email is required"})
    if not _is_strong_password(password):
        return jsonify({
            "status": "ERROR",
            "message": "Password must be 8+ chars with uppercase, lowercase, digit, and special character."
        })

    address = f"{door},{street},{area},{pincode}"
    pending_registration = {
        "username": username,
        "password": _hash_password(password),
        "full_name": full_name,
        "email": email,
        "phone": phone,
        "address": address,
    }
    otp_result = _start_otp_flow(
        "register",
        email,
        "register",
        username,
        payload=pending_registration
    )

    response = {
        "status":   "SUCCESS",
        "message":  "Registration OTP created. Complete verification to finish signup.",
        "user_email": email,
        "otp_sent": otp_result["status"] == "SUCCESS",
        "otp_message": (
            "Verification OTP sent to your registered email."
            if otp_result["status"] == "SUCCESS"
            else otp_result.get("data", "Could not send OTP email. Use resend OTP.")
        )
    }
    return jsonify(response)


@app.route("/api/verify_registration_otp", methods=["POST"])
def api_verify_registration_otp():
    """
    POST /api/verify_registration_otp
    Body: { "otp": "1234" }
    Verifies the OTP generated during the registration flow and commits
    the pending registration through the C auth binary.
    """
    data = request.get_json(silent=True) or {}
    otp  = data.get("otp", "").strip()

    if not otp:
        return jsonify({"status": "ERROR", "message": "OTP is required"})

    otp_state = _get_otp_flow("register")
    if not otp_state:
        return jsonify({
            "status": "ERROR",
            "message": "OTP expired or not found. Please register again.",
            "restart_required": True
        }), 400

    is_valid, message = _validate_otp_flow("register", otp)
    if not is_valid:
        return jsonify({"status": "ERROR", "message": message})

    payload = otp_state.get("payload") or {}
    required_fields = ["username", "password", "full_name", "email", "phone", "address"]
    if not all(payload.get(field, "") for field in required_fields):
        _clear_otp_flow("register")
        return jsonify({
            "status": "ERROR",
            "message": "Registration session expired. Please register again.",
            "restart_required": True
        }), 400

    result = run_c_binary("auth", [
        "register",
        payload["username"],
        payload["password"],
        payload["full_name"],
        payload["email"],
        payload["phone"],
        payload["address"],
    ])
    _clear_otp_flow("register")
    if result["status"] != "SUCCESS":
        return jsonify({
            "status": "ERROR",
            "message": result.get("data", "Could not complete registration"),
            "restart_required": True
        }), 400

    return jsonify({"status": "SUCCESS", "message": "Registration completed successfully"})


@app.route("/api/resend_registration_otp", methods=["POST"])
def api_resend_registration_otp():
    """
    POST /api/resend_registration_otp
    Re-issues the pending registration OTP to the same email address.
    """
    otp_state = _get_otp_flow("register")
    if not otp_state:
        return jsonify({
            "status": "ERROR",
            "message": "Registration OTP session expired. Please register again."
        }), 400

    otp_result = _restart_otp_flow("register", otp_state)

    if otp_result["status"] != "SUCCESS":
        return jsonify({
            "status": "ERROR",
            "message": otp_result.get("data", "Could not resend OTP")
        })

    return jsonify({
        "status": "SUCCESS",
        "message": "OTP resent successfully",
        "user_email": otp_state["recipient_email"]
    })


@app.route("/api/get_admin_info", methods=["POST"])
def api_get_admin_info():
    """
    POST /api/get_admin_info
    Returns the current admin's session fields as JSON.
    Used by the admin dashboard to personalise the header.
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"})

    admin_id = session.get("user_id")
    
    result = run_c_binary("auth", ["get_admin_profile", admin_id])

    if result["status"] == "SUCCESS":
        parts = result["data"].split("|")
        return jsonify({
            "status":   "SUCCESS",
            "user_id":  parts[0],
            "username": parts[1],
            "name":     parts[2],
            "email":    parts[3]  
        })
    
    return jsonify(result)


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

    profile = _parse_user_profile_data(result["data"])
    if _is_valid_email(profile["email"]):
        session["email"] = profile["email"]
    return jsonify({
        "status":    "SUCCESS",
        "user_id":   profile["user_id"],
        "username":  profile["username"],
        "full_name": profile["full_name"],
        "email":     profile["email"],
        "phone":     profile["phone"],
        "address":   profile["address"],
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
    if result["status"] == "SUCCESS" and field == "email":
        session["email"] = new_value
    return jsonify({"status": result["status"], "message": result["data"]})


@app.route("/api/send_password_change_otp", methods=["POST"])
def api_send_password_change_otp():
    """
    POST /api/send_password_change_otp
    Emails an OTP to the logged-in user's registered account email.
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"}), 401

    recipient_email, error_response = _load_current_account_email()
    if error_response:
        return error_response

    flow_key = _password_change_flow_key()
    reference = f"{session.get('role', 'user')}:{session.get('user_id', '')}"
    otp_result = _start_otp_flow(
        flow_key,
        recipient_email,
        "password_change",
        reference
    )

    if otp_result["status"] != "SUCCESS":
        return jsonify({
            "status": "ERROR",
            "message": otp_result.get("data", "Could not send password OTP")
        }), 500

    return jsonify({
        "status": "SUCCESS",
        "message": "Password OTP sent successfully",
        "user_email": recipient_email
    })


@app.route("/api/resend_password_change_otp", methods=["POST"])
def api_resend_password_change_otp():
    """
    POST /api/resend_password_change_otp
    Re-issues the pending password-change OTP.
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"}), 401

    flow_key = _password_change_flow_key()
    otp_state = _get_otp_flow(flow_key)
    if not otp_state:
        return jsonify({
            "status": "ERROR",
            "message": "Password OTP session expired. Start the password change flow again."
        }), 400

    otp_result = _restart_otp_flow(flow_key, otp_state)
    if otp_result["status"] != "SUCCESS":
        return jsonify({
            "status": "ERROR",
            "message": otp_result.get("data", "Could not resend password OTP")
        }), 500

    return jsonify({
        "status": "SUCCESS",
        "message": "Password OTP resent successfully",
        "user_email": otp_state["recipient_email"]
    })


@app.route("/api/change_password", methods=["POST"])
def api_change_password():
    """
    POST /api/change_password
    Body: { "old_password": "...", "new_password": "...", "otp": "1234" }

    Verifies the OTP server-side, then routes to the correct C command:
      user  → ./auth change_pass_user  <user_id> <old> <new>
      admin → ./auth change_pass_admin <user_id> <old> <new>
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"})

    data  = request.get_json() or {}
    old_p = data.get("old_password", "").strip()
    new_p = data.get("new_password", "").strip()
    otp   = data.get("otp", "").strip()

    if not old_p or not new_p:
        return jsonify({"status": "ERROR", "message": "Old and new passwords are required"}), 400
    if not _is_strong_password(new_p):
        return jsonify({
            "status": "ERROR",
            "message": "New password must be 8+ chars with uppercase, lowercase, digit, and special character."
        }), 400
    if not otp:
        return jsonify({
            "status": "ERROR",
            "message": "OTP is required",
            "otp_error": True
        }), 400

    flow_key = _password_change_flow_key()
    reference = f"{session.get('role', 'user')}:{session.get('user_id', '')}"
    is_valid, message = _validate_otp_flow(flow_key, otp, reference=reference)
    if not is_valid:
        return jsonify({
            "status": "ERROR",
            "message": message,
            "otp_error": True
        }), 400

    _clear_otp_flow(flow_key)

    cmd = "change_pass_admin" if session.get("role") == "admin" else "change_pass_user"
    result = run_c_binary("auth", [cmd, session["user_id"],
                                   _hash_password(old_p), _hash_password(new_p)])

    return jsonify({"status": result["status"], "message": result["data"]})

@app.route("/api/admin/inventory_data", methods=["GET"])
def api_admin_inventory_data():
    """
    GET /api/admin/inventory_data  (admin only)
    Calls ./order list_products & ./inventory list_promo
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"}), 403

    veg_raw   = run_c_binary("order",     ["list_products"])
    promo_raw = run_c_binary("inventory", ["list_promo"])

    vegetables = []
    if veg_raw["status"] == "SUCCESS":
        lines = veg_raw["raw_output"].strip().split("\n")
        vegetables = [p for line in lines[1:] if (p := _parse_veg_line(line))]
    
    free_items = []
    if promo_raw["status"] == "SUCCESS":
        lines = promo_raw["raw_output"].strip().split("\n")
        free_items = [p for line in lines[1:] if (p := _parse_promo_line(line))]

    return jsonify({
        "status":     "SUCCESS",
        "vegetables": vegetables,
        "free_items": free_items,
    })

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

@app.route("/api/create_razorpay_order", methods=["POST"])
def api_create_razorpay_order():
    """
    POST /api/create_razorpay_order
    Body: { "delivery_slot": "Morning|Afternoon|Evening" }

    STEP 1 OF 2 in the Razorpay payment flow.

    PURPOSE:
      - Validates the slot and session.
      - Fetches the current cart total from the C binary (view_cart).
      - Creates a Razorpay Order on Razorpay's servers.
      - Returns the razorpay_order_id + amount + key_id to the frontend
        so the browser can open the Razorpay checkout modal.
      - Does NOT touch orders.dat. No order is committed yet.

    Returns:
      {
        "status":            "SUCCESS",
        "razorpay_order_id": "order_XXXXXXXXXXXXXXXX",
        "amount":            25000,        ← paise (₹250.00 × 100)
        "currency":          "INR",
        "key_id":            "rzp_test_...",
        "slot":              "Morning"
      }
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"}), 401

    data = request.get_json(silent=True) or {}
    slot = data.get("delivery_slot", "").strip()

    VALID_SLOTS = {"Morning", "Afternoon", "Evening"}
    if slot not in VALID_SLOTS:
        return jsonify({"status": "ERROR", "message": "Invalid delivery slot"})

    # ── Fetch cart total from C binary ───────────────────────────────────────
    # We call view_cart so we know the exact amount to charge.
    # The C binary returns: SUCCESS|<cart_total>\n<item lines...>
    cart_result = run_c_binary("order", ["view_cart", session["user_id"]])

    if cart_result["status"] != "SUCCESS":
        return jsonify({"status": "ERROR", "message": "Could not read cart"})

    lines      = cart_result["raw_output"].strip().split("\n")
    cart_total = _safe_float(lines[0].split("|")[1]) if lines else 0.0

    if cart_total < 100.0:
        return jsonify({
            "status":  "ERROR",
            "message": "Minimum order amount is ₹100"
        })

    # ── Create Razorpay order ─────────────────────────────────────────────────
    # Razorpay amounts are always in the smallest currency unit (paise).
    # ₹250.75 → 25075 paise. We round to avoid float precision issues.
    amount_paise = int(round(cart_total * 100))

    try:
        rzp_order = _rzp_client.order.create({
            "amount":   amount_paise,
            "currency": "INR",
            "receipt":  f"fp_{session['user_id']}_{slot}",
            "notes": {
                "user_id": session["user_id"],
                "slot":    slot,
                "project": "FreshPicks-SDP1"
            }
        })
    except Exception as e:
        return jsonify({
            "status":  "ERROR",
            "message": f"Razorpay order creation failed: {str(e)}"
        })

    # Store slot in session so /api/verify_and_checkout can read it
    # without the frontend having to resend it (tamper-proofing).
    session["pending_slot"]     = slot
    session["pending_rzp_order"] = rzp_order["id"]

    return jsonify({
        "status":            "SUCCESS",
        "razorpay_order_id": rzp_order["id"],
        "amount":            amount_paise,
        "currency":          "INR",
        "key_id":            RAZORPAY_KEY_ID,
        "slot":              slot
    })


@app.route("/api/verify_and_checkout", methods=["POST"])
def api_verify_and_checkout():
    """
    POST /api/verify_and_checkout
    Body:
      {
        "razorpay_order_id":   "order_XXXXXXXXXXXXXXXX",
        "razorpay_payment_id": "pay_XXXXXXXXXXXXXXXX",
        "razorpay_signature":  "<HMAC-SHA256 hex digest>"
      }

    STEP 2 OF 2 in the Razorpay payment flow.

    PURPOSE:
      - Verifies the HMAC-SHA256 signature that Razorpay sends to the
        browser after a successful payment.
      - Signature formula (from Razorpay docs):
            HMAC_SHA256(razorpay_order_id + "|" + razorpay_payment_id,
                        key_secret)
      - If the signature is valid → calls the C binary to commit the order.
      - If the signature is invalid → rejects immediately. Nothing is saved.

    This is the critical security gate. The C binary is only called
    AFTER the signature passes. A tampered or replayed payment cannot
    commit an order.

    Returns (same schema as the old /api/checkout):
      {
        "status":   "SUCCESS",
        "order_id": "ORD1007",
        "total":    250.00,
        "slot":     "Morning",
        "boy_name": "Ramesh",
        "boy_phone":"9876543210",
        "items":    "V1001:Tomato:500:30.00,..."
      }
    """
    if "user_id" not in session:
        return jsonify({"status": "ERROR", "message": "Not logged in"}), 401

    data           = request.get_json(silent=True) or {}
    rzp_order_id   = data.get("razorpay_order_id",   "").strip()
    rzp_payment_id = data.get("razorpay_payment_id", "").strip()
    rzp_signature  = data.get("razorpay_signature",  "").strip()

    # ── Guard: all three fields must be present ───────────────────────────────
    if not rzp_order_id or not rzp_payment_id or not rzp_signature:
        return jsonify({
            "status":  "ERROR",
            "message": "Incomplete payment response"
        })

    # ── Guard: order_id must match what we stored in session ─────────────────
    # This prevents a user from replaying a payment from a different session.
    if rzp_order_id != session.get("pending_rzp_order"):
        return jsonify({
            "status":  "ERROR",
            "message": "Order ID mismatch — possible replay attempt"
        })

    # ── HMAC-SHA256 Signature Verification ───────────────────────────────────
    #
    # Razorpay's verification formula:
    #   expected_signature = HMAC_SHA256(
    #       key    = KEY_SECRET (bytes),
    #       msg    = razorpay_order_id + "|" + razorpay_payment_id (bytes)
    #   ).hexdigest()
    #
    # If expected_signature == razorpay_signature → payment is genuine.
    # If not → someone tampered with the response.
    #

    try:
        _rzp_client.utility.verify_payment_signature({
            "razorpay_order_id":   rzp_order_id,
            "razorpay_payment_id": rzp_payment_id,
            "razorpay_signature":  rzp_signature
        })
    except Exception:
        return jsonify({
            "status":  "ERROR",
            "message": "Payment verification failed — invalid signature"
        })

    # ── Signature is valid — retrieve slot from session ───────────────────────
    slot = session.get("pending_slot", "")
    if slot not in {"Morning", "Afternoon", "Evening"}:
        return jsonify({"status": "ERROR", "message": "Session slot missing"})

    # ── Call the C binary to commit the order ─────────────────────────────────
    # This is the ONLY point where orders.dat is written.
    # Identical call to the old /api/checkout.
    result = run_c_binary("order", ["checkout", session["user_id"], slot])

    if result["status"] != "SUCCESS":
        # Payment went through on Razorpay's side but our backend failed.
        # Log this — in production you'd trigger a refund here.
        # For the demo, return the error and let the user contact support.
        return jsonify({
            "status":  "ERROR",
            "message": f"Payment received but order failed: {result['data']}"
        })

    # ── Clean up pending session keys ────────────────────────────────────────
    session.pop("pending_slot",      None)
    session.pop("pending_rzp_order", None)

    # ── Parse C binary output (identical schema to old /api/checkout) ─────────
    # C stdout: SUCCESS|order_id|total|slot|boy_name|boy_phone|items_string
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


@app.route("/api/send_cancel_order_otp", methods=["POST"])
def api_send_cancel_order_otp():
    """
    POST /api/send_cancel_order_otp
    Body: { "order_id": "ORD1007" }
    Emails a cancellation OTP to the active account tied to the flow.
    """
    if session.get("role") not in {"user", "admin"}:
        return jsonify({"status": "ERROR", "message": "Login required"}), 403

    data     = request.get_json(silent=True) or {}
    order_id = data.get("order_id", "").strip()

    if not order_id:
        return jsonify({"status": "ERROR", "message": "order_id is required"})

    receipt_data, error_response = _load_receipt_data(order_id)
    if error_response:
        return error_response

    if receipt_data["status"] != "Order Placed":
        return jsonify({
            "status": "ERROR",
            "message": "Only Order Placed orders can be cancelled"
        }), 400

    if session.get("role") == "user" and receipt_data["user_id"] != session.get("user_id"):
        return jsonify({"status": "ERROR", "message": "Order does not belong to this user"}), 403

    if session.get("role") == "admin":
        recipient_email, error_response = _load_current_account_email()
        if error_response:
            return error_response
    else:
        recipient_email = receipt_data.get("user_email", "").strip()
        if not _is_valid_email(recipient_email):
            return jsonify({
                "status": "ERROR",
                "message": "No valid email found for this order"
            }), 400

    flow_key   = _cancel_order_flow_key(order_id)
    otp_result = _start_otp_flow(flow_key, recipient_email, "cancel_order", order_id)

    if otp_result["status"] != "SUCCESS":
        return jsonify({
            "status": "ERROR",
            "message": otp_result.get("data", "Could not send cancellation OTP")
        })

    return jsonify({
        "status": "SUCCESS",
        "message": "Cancellation OTP sent successfully",
        "user_email": recipient_email
    })


@app.route("/api/cancel_order_with_otp", methods=["POST"])
def api_cancel_order_with_otp():
    """
    POST /api/cancel_order_with_otp
    Body: { "order_id": "ORD1007", "otp": "1234" }
    Verifies the active user/admin OTP before cancelling the order.
    """
    if session.get("role") not in {"user", "admin"}:
        return jsonify({"status": "ERROR", "message": "Login required"}), 403

    data     = request.get_json(silent=True) or {}
    order_id = data.get("order_id", "").strip()
    otp      = data.get("otp", "").strip()

    if not order_id:
        return jsonify({"status": "ERROR", "message": "order_id is required"})
    if not otp:
        return jsonify({"status": "ERROR", "message": "OTP is required"})

    receipt_data, error_response = _load_receipt_data(order_id)
    if error_response:
        return error_response

    if session.get("role") == "user" and receipt_data["user_id"] != session.get("user_id"):
        return jsonify({"status": "ERROR", "message": "Order does not belong to this user"}), 403

    if receipt_data["status"] != "Order Placed":
        return jsonify({
            "status": "ERROR",
            "message": "Only Order Placed orders can be cancelled"
        }), 400

    flow_key = _cancel_order_flow_key(order_id)
    is_valid, message = _validate_otp_flow(flow_key, otp, reference=order_id)
    if not is_valid:
        return jsonify({"status": "ERROR", "message": message}), 400

    result = run_c_binary("delivery", ["cancel_order", order_id])
    if result["status"] == "SUCCESS":
        _clear_otp_flow(flow_key)

    return jsonify({"status": result["status"], "message": result.get("data", "")})


@app.route("/api/cancel_order", methods=["POST"])
def api_cancel_order():
    """
    POST /api/cancel_order
    Body: { "order_id": "ORD1007" }

    Legacy route kept only to block non-OTP order cancellations.
    """
    return jsonify({
        "status": "ERROR",
        "message": "Cancellation now requires OTP verification. Use /api/cancel_order_with_otp."
    }), 400


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

    receipt_data, tmp_path, error_response = _build_receipt_pdf(order_id)
    if error_response:
        return error_response

    # ── Step 4: Stream the PDF back to the browser ────────────
    filename = f"{receipt_data['order_id']}_{receipt_data['user_id']}.pdf"
    response = send_file(
        tmp_path,
        mimetype="application/pdf",
        as_attachment=True,
        download_name=filename
    )
    response.call_on_close(lambda: _cleanup_temp_file(tmp_path))
    return response

@app.route("/api/email_receipt", methods=["POST"])
def api_email_receipt():
    """
    POST /api/email_receipt  (user login required)

    PURPOSE:
        Generates the same server-side PDF used by /api/download_receipt,
        passes that temp file to the C mailer binary, then deletes it.

    REQUEST:
        JSON:
          • order_id   — e.g. "ORD1007"

    RESPONSE (JSON):
        { "status": "SUCCESS", "message": "Email sent" }
        { "status": "ERROR",   "message": "<reason>" }

    RULE COMPLIANCE:
        • Thin route — no business logic.
        • Uses the same receipt-generation helper as download.
        • run_c_binary() is the only call to the C layer.
    """

    # ── Session guard ────────────────────────────────────────────
    if session.get("role") != "user":
        return jsonify({"status": "ERROR", "message": "Login required"}), 403

    data     = request.get_json(silent=True) or {}
    order_id = data.get("order_id", "").strip()

    if not order_id:
        return jsonify({"status": "ERROR", "message": "order_id is required"})

    receipt_data, tmp_path, error_response = _build_receipt_pdf(order_id)
    if error_response:
        return error_response

    try:
        if receipt_data["user_id"] != session.get("user_id"):
            return jsonify({"status": "ERROR", "message": "Order does not belong to this user"}), 403

        user_email = receipt_data.get("user_email", "").strip()
        if not user_email or "@" not in user_email:
            return jsonify({"status": "ERROR", "message": "No valid email found for this order"}), 400

        result = run_c_binary("mailer", [user_email, tmp_path])
    finally:
        _cleanup_temp_file(tmp_path)

    # ── Return the C binary's verdict verbatim ────────────────────
    if result["status"] == "SUCCESS":
        return jsonify({
            "status": "SUCCESS",
            "message": "Email sent",
            "user_email": user_email
        })
    else:
        return jsonify({"status": "ERROR", "message": result.get("data", "Mailer failed")})


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


@app.route("/api/analytics", methods=["GET"])
def api_analytics():
    """
    GET /api/analytics  (admin only)
    Calls: ./analytics get_analytics
    Returns JSON with all dashboard metrics.

    Success: { "status": "SUCCESS", "stats": { ...all keys... } }
    Error:   { "status": "ERROR",   "message": "..." }
    """
    if session.get("role") != "admin":
        return jsonify({"status": "ERROR", "message": "Admin only"}), 403

    result = run_c_binary("analytics", ["get_analytics"])

    if result["status"] != "SUCCESS":
        return jsonify({
            "status":  "ERROR",
            "message": result.get("data", "Analytics binary failed")
        })

    stats = _parse_analytics_output(result["raw_output"])

    if stats is None:
        return jsonify({
            "status":  "ERROR",
            "message": "Failed to parse analytics output"
        })

    return jsonify({"status": "SUCCESS", "stats": stats})


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
    # Launch Mode: VERSION B (Intranet Hosting) active.
    # Comment HOST = "0.0.0.0" and uncomment HOST = "127.0.0.1"
    # to switch to VERSION A (Localhost).
    # ─────────────────────────────────────────────────────────

    # VERSION A (Localhost)
    # HOST = "127.0.0.1"

    # VERSION B (Intranet Hosting)
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

    CORS(app, supports_credentials=True, origins=[
        "http://localhost:5000",
        "https://localhost:5000",
        f"http://{LOCAL_IP}:5000",
        f"https://{LOCAL_IP}:5000"
    ])
    
    print()
    print("=" * 62)
    print("  FreshPicks  |  CodeCrafters  |  SDP-1  |  HTTPS Server")
    print("=" * 62)
    print()

    if HOST == "127.0.0.1":
        print("  Mode         :  VERSION A (Localhost)")
        print(f"  Home         :  https://localhost:{PORT}/")
        print(f"  Admin Portal :  https://localhost:{PORT}/login/admin")
        print(f"  User Portal  :  https://localhost:{PORT}/login/user")
        print()
        print("  Not visible to other devices on this network.")
    else:
        print( "  Mode         :  VERSION B (Intranet Hosting)")
        print()
        print(f"  Home         :  https://{LOCAL_IP}:{PORT}/")
        print(f"  Presentation :  https://{LOCAL_IP}:{PORT}/presentation")
        print(f"  Admin Portal :  https://{LOCAL_IP}:{PORT}/login/admin")
        print(f"  User Portal  :  https://{LOCAL_IP}:{PORT}/login/user")
        print()

    print()
    print("  Browser shows 'Not private'? That is expected (self-signed cert).")
    print("  Click Advanced -> Proceed (accept self-signed cert).")
    print("=" * 62)
    print()

    app.run(
        host        = HOST,
        port        = PORT,
        debug       = DEBUG,
        ssl_context = ssl_ctx
    )
