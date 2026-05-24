# 🧺 Fresh Picks

> **Your neighbourhood grocery store, now online.**
> Powered by high-performance C binaries and a Flask API Gateway.

[![Python](https://img.shields.io/badge/Python-3.x-blue?style=flat-square&logo=python)](https://python.org)
[![Flask](https://img.shields.io/badge/Flask-Gateway-lightgrey?style=flat-square&logo=flask)](https://flask.palletsprojects.com)
[![C](https://img.shields.io/badge/Logic_Engine-C-brightgreen?style=flat-square&logo=c)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Bootstrap](https://img.shields.io/badge/UI-Bootstrap_5-purple?style=flat-square&logo=bootstrap)](https://getbootstrap.com)
[![HTTPS](https://img.shields.io/badge/Network-Self--Signed_HTTPS-orange?style=flat-square&logo=openssl)](https://openssl.org)

---

## What is Fresh Picks?

Fresh Picks is a high-performance, intranet-hosted e-commerce platform built for local grocery management. The core architectural decision that separates it from standard web apps: **all business logic and state management runs inside compiled C binaries**, not in Python or a database engine.

Flask acts purely as a routing gateway — it receives HTTP requests, shells out to the appropriate C binary via subprocess IPC, parses the `SUCCESS|...` / `ERROR|...` stdout response, and returns JSON to the frontend. The C layer owns all data structures, all file I/O, and all algorithmic work.

```
Browser  ──►  Flask (app.py)  ──►  C Binary (subprocess IPC)  ──►  .dat files
         ◄──  JSON            ◄──  stdout (pipe-delimited)     ◄──
```

---

## Architecture Overview

```
Fresh-Picks/
├── app/
│   ├── app.py                  # Flask API Gateway — routing only
│   ├── bridge.py               # IPC layer — subprocess runner + stdout parser
│   ├── migrate_password.py     # One-shot SHA-256 migration script
│   └── static/ & templates/    # Frontend (Bootstrap 5 + VS Code dark theme)
│
└── backend/
    ├── models.h                # Global source of truth — all structs & constants
    ├── utils.c                 # Shared DSA implementations (SLL, DLL, Heap, CLL, Tables)
    ├── auth.c                  # Authentication & profile management
    ├── order.c                 # Cart (DLL), checkout pipeline, order management
    ├── inventory.c             # Admin stock & promo item management
    ├── delivery.c              # Delivery status, agent assignment, slot promotion
    ├── analytics.c             # Revenue, inventory, and staff metrics
    ├── users.c                 # Admin user listing and search
    ├── receipt.c               # Order receipt data extractor
    ├── mailer.c                # SMTP engine — OTP and receipt dispatch
    ├── admin_tools.c           # Admin utilities
    ├── build.sh                # Compilation script for all binaries
    └── *.dat                   # Flat binary databases (structs serialized with fwrite)
```

### IPC Protocol

Every C binary prints exactly one of two formats to stdout:

```
SUCCESS|<data>
ERROR|<reason>
```

`bridge.py` splits on `|`, reads the status token, and either returns the data payload or raises an error — no JSON parsing inside C, no ORM, no network sockets.

---

## DSA Core

This project is built around custom data structures implemented from scratch in C. No STL, no standard containers.

### Singly Linked List (SLL) — Persistence Layer

Used for: **Users, Products, Orders, Free Items, Delivery Boys, Admin Credentials**

The SLL is the primary in-memory representation of every dataset. On each binary invocation, the relevant `.dat` file is deserialized with `fread` into a dynamically allocated SLL. On mutation, the SLL is re-serialized back with `fwrite`. This gives the system a clean separation between the persistence format (flat binary structs) and the runtime format (linked nodes).

```
.dat file  ──fread──►  SLL (head → node → node → NULL)  ──fwrite──►  .dat file
```

Complexity: `O(N)` load and save, `O(1)` head insertion.

---

### Pointer Table (Direct Index / Perfect Hash) — O(1) Lookup

Used for: **All entity lookups by ID** (Users, Orders, Products, Free Items)

After the SLL is built, a pointer table is constructed once per binary invocation. The numeric suffix of every entity ID is extracted and offset by `ID_BASE (1001)` to produce a zero-based array index. The corresponding table slot stores a direct pointer to the SLL node.

```
ID: "U1042"  →  index = 1042 - 1001 = 41  →  user_table[41] → UserNode*
```

This eliminates O(N) SLL traversal for every read, update, and delete. All subsequent operations are O(1) pointer dereferences, not linear scans.

```c
int idx = get_index_from_id(user_id);          // extract numeric offset
UserNode *match = user_table[idx];             // O(1) — direct pointer access
```

No hash collisions are possible because IDs are system-generated sequentially — the "hash" is a pure arithmetic mapping.

---

### Doubly Linked List (DLL) — Shopping Cart

Used for: **Per-user cart state** (`carts/<user_id>_cart.txt`)

The DLL drives the real-time shopping cart. Each `CartNode` carries both `prev` and `next` pointers. The critical advantage over an SLL: **once a node is located, deletion is O(1)** — `curr->prev->next` and `curr->next->prev` are rewired without any predecessor traversal.

```c
/* O(1) unlink — no back-scan needed because prev is always available */
if (curr->prev) curr->prev->next = curr->next;
else            *head            = curr->next;
if (curr->next) curr->next->prev = curr->prev;
```

In an SLL, even with a direct pointer to the target node, you cannot unlink it without traversing from head to find its predecessor. The `prev` pointer is what makes O(1) deletion possible here.

| Operation | SLL | DLL (Fresh Picks) |
|---|---|---|
| Find item by `veg_id` | O(N) | O(N) — unavoidable, no index key |
| Unlink after find | O(N) predecessor scan | **O(1)** via `prev` |
| Update quantity | O(N) | O(N) find + O(1) mutate |
| Get cart total | O(N) | O(N) |

The cart file is text-based (one `veg_id|name|qty_g|price|is_free` line per item) and is loaded fresh on each subprocess call, so the DLL is reconstructed per request. This is an intentional tradeoff of the IPC-per-request architecture — the DLL correctness and O(1) unlink guarantee hold regardless.

---

### Min-Heap — Admin Order Dispatch

Used for: **Priority-sorted order listing** (`delivery.c → cmd_list_all_orders_sorted`)

A flat-array binary min-heap sorted by `slot_priority` (Morning=1, Afternoon=2, Evening=3). All "Order Placed" orders are pushed into the heap from the SLL, then extracted in priority order for dispatch.

```
Insert:       O(log N)   — heapify-up
Extract-min:  O(log N)   — heapify-down
Build heap:   O(N log N) — N insertions
```

Guarantees that Morning slots always surface first regardless of the order they were placed in.

---

### Circular Linked List (CLL) — Delivery Assignment

Used for: **Round-robin delivery boy assignment** (`utils.c → cll_build_from_sll`, `cll_assign_delivery`)

Built at runtime from the `DeliveryBoyNode` SLL, containing only `is_active = 1` entries. A persistent `last_assigned` counter on each `DeliveryBoy` struct enables the CLL to resume from where it left off across subprocess calls. Wrap-around is automatic — `tail->next = head`.

Result: equitable, stateless, O(1) next-assignment across all active delivery personnel.

---

### Standard Queue (FIFO) — Order Processing

Used for: **Sequential order processing pipeline** (`utils.c → queue_*`)

A `front/rear` pointer queue with O(1) enqueue and O(1) dequeue. Ensures orders are processed in arrival sequence before slot-priority sorting takes over at the dispatch stage.

---

## Security

### SHA-256 Password Hashing

Plaintext passwords never reach the C binaries. Flask hashes credentials using Python's built-in `hashlib` before passing them as CLI arguments:

```python
def _hash_password(plaintext: str) -> str:
    return hashlib.sha256(plaintext.encode("utf-8")).hexdigest()  # 64-char hex
```

The C structs store the 64-character hex digest in `char password[MAX_STR_LEN]` (128 bytes). All `strcmp` validations in `auth.c` operate on hashes — the C layer is completely agnostic to this change.

### OTP Verification (TTL-based)

Time-limited One Time Passwords are generated in Flask for user registration and password recovery. OTP payloads are staged in server-side session memory with a TTL, then cleared on verification or expiry. `mailer.c` dispatches OTPs directly to the user's inbox via SMTP.

### Self-Signed HTTPS / TLS

All intranet traffic is encrypted via a self-signed X.509 certificate. The Flask server binds to `0.0.0.0` and dynamically detects the host's local IPv4 address at startup, printing the LAN URL for cross-device access on the same network.

### Razorpay HMAC-SHA256 Signature Validation

Checkout uses server-side HMAC-SHA256 signature verification against the Razorpay payment payload. Client-side cart values are never trusted for payment confirmation — the server re-validates the signature before marking an order as placed.

---

## Features

### User Portal
| Feature | Description |
|---|---|
| Register / Login | OTP-verified registration, SHA-256 hashed credentials |
| Shop | Browse full vegetable catalogue with live stock and pricing |
| Cart | DLL-backed real-time cart with quantity editing and item removal |
| Checkout | 5-step pipeline: Cart → Slot → Payment → Confirmation → Receipt |
| My Orders | Full order history with status tracking |
| Profile | View and update personal details, change password |
| PDF Receipt | Dark-theme invoice generated on-the-fly, emailed to inbox |

### Admin Portal
| Feature | Description |
|---|---|
| Dashboard | Live revenue, order status, and staffing summary |
| Inventory | Update stock, price, and validity per product; manage promo items |
| Orders | View all orders newest-first; slot-priority sorted view via Min-Heap |
| Delivery | Update order status, manually assign agents, batch-promote by slot |
| Users | List, filter, and search registered users |
| Analytics | Revenue breakdown, slot distribution, inventory health, staff metrics |

### Automated Mailing (mailer.c — C-Based SMTP Engine)
- OTP emails dispatched instantly on registration and password recovery
- PDF receipt emailed to customer on successful checkout
- Direct SMTP relay — no third-party email SDK

---

## Getting Started

### Prerequisites

```
GCC         (tested with GCC 13+)
Python 3.x  (3.10+ recommended)
OpenSSL     (for HTTPS certificate generation)
pip packages: flask, fpdf2, razorpay, (see requirements below)
```

### Install Python Dependencies

```bash
pip install flask fpdf2 razorpay
```

### Compile C Binaries

All binaries are compiled by a single build script:

```bash
cd backend/
bash build.sh
```

This produces: `auth`, `order`, `inventory`, `delivery`, `analytics`, `users`, `receipt`, `mailer`, `admin_tools`

### Generate SSL Certificates

```bash
cd app/
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes
```

### (First-time only) Migrate Passwords to SHA-256

If you have an existing `users.dat` / `admin_creds.dat` with plaintext passwords, run the one-shot migration script before starting the server:

```bash
cd backend/
python ../app/migrate_password.py
```

### Launch the Server

```bash
cd app/
python app.py
```

The terminal will print the LAN URL:

```
 * Running on https://192.168.x.x:5000
```

Access from any device on the same network at that address.

---

## Binary Struct Layout

All `.dat` files are flat binary files — no delimiters, no headers. Records are fixed-size C structs written with `fwrite` and read with `fread`.

| Struct | Size | File |
|---|---|---|
| `User` | 916 bytes | `users.dat` |
| `AdminCreds` | 532 bytes | `admin_creds.dat` |
| `Vegetable` | — | `products.dat` |
| `Order` | — | `orders.dat` |
| `FreeItem` | — | `free_inventory.dat` |
| `DeliveryBoy` | — | `delivery_boys.dat` |

All constants are defined in `models.h` — `MAX_STR_LEN (128)`, `MAX_ID_LEN (20)`, `MAX_ADD_LEN (256)`.

---

## Troubleshooting

**Blank PDFs on receipt download**
CSS variables (`var(--accent)`) are stripped during `html2pdf.js` canvas rendering. Use hardcoded hex colours inside the receipt clone element.

**`[SKIP] users.dat not found` during migration**
The migration script resolves paths relative to the working directory. Run it from inside `backend/`, or it will auto-resolve via `os.path.abspath(__file__)` if the path fix is applied.

**Binary not found / permission denied**
Ensure `build.sh` ran successfully and binaries have execute permissions: `chmod +x backend/auth backend/order ...`

**HTTPS certificate warning in browser**
Expected for self-signed certs. Click "Advanced → Proceed" on first access. Import `cert.pem` into your OS trust store to suppress permanently.

**404 on API routes**
Verify the `fetch` URLs in HTML templates match the route definitions in `app.py`. All routes follow the pattern `/api/<resource>/<action>`.

---

*Developed by Team CodeCrafters — SDP-1 Project.*
