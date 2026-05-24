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

## Application Features

### User Features

| Feature | Description |
|---|---|
| **Register** | New user onboarding with OTP-verified email and SHA-256 hashed credentials |
| **User Dashboard** | Personalised customer home — order summary, quick actions, account overview |
| **Shop** | Browse full vegetable catalogue with live stock levels, pricing, and tags |
| **Shopping Cart** | DLL-backed real-time cart — add, update quantity, remove items, order confirmation and payment |
| **My Orders** | Full customer order history with status tracking and receipt access |
| **Profile** | View and update personal details, change password with OTP verification |

### Administrative Controls

| Feature | Description |
|---|---|
| **Admin Dashboard** | Admin control center — live revenue, order status breakdown, staffing summary |
| **Inventory** | Manage vegetable stock, price, and validity per product; manage promo/free items |
| **Admin Orders** | View all orders newest-first; slot-priority sorted dispatch view via Min-Heap |
| **Users List** | List, filter by status, and search registered customers by ID or name |
| **Analytics** | Revenue breakdown, average order value, slot distribution, inventory health, staff metrics |

---

## Algorithmic Core (DSA)

This project is built around custom data structures implemented from scratch in C. No STL, no standard containers, no database engine.

### Data Structures

#### Singly Linked List (SLL) — Persistence Layer
`O(N) Time | O(N) Space`

Used for: **Users, Products, Orders, Free Items, Delivery Boys, Admin Credentials**

The SLL is the primary in-memory representation of every dataset. On each binary invocation, the relevant `.dat` file is deserialized with `fread` into a dynamically allocated SLL. On mutation, the SLL is re-serialized back with `fwrite`. This gives the system a clean separation between the persistence format (flat binary structs) and the runtime format (linked nodes).

```
.dat file  ──fread──►  SLL (head → node → node → NULL)  ──fwrite──►  .dat file
```

#### Doubly Linked List (DLL) — Shopping Cart
`O(1) Mid-List Deletions`

Used for: **Per-user cart state** (`carts/<user_id>_cart.txt`)

The DLL drives the real-time shopping cart. Each `CartNode` carries both `prev` and `next` pointers. The critical advantage over an SLL: **once a node is located, deletion is O(1)** — `curr->prev->next` and `curr->next->prev` are rewired without any predecessor traversal.

```c
/* O(1) unlink — no back-scan needed because prev is always available */
if (curr->prev) curr->prev->next = curr->next;
else            *head            = curr->next;
if (curr->next) curr->next->prev = curr->prev;
```

In an SLL, even with a direct pointer to the target node, you cannot unlink it without traversing from head to find its predecessor. The `prev` pointer is what makes O(1) deletion architecturally possible here.

| Operation | SLL | DLL (Fresh Picks) |
|---|---|---|
| Find item by `veg_id` | O(N) | O(N) — no index key on cart items |
| Unlink after find | O(N) predecessor scan | **O(1)** via `prev` |
| Update quantity | O(N) | O(N) find + O(1) mutate |
| Get cart total | O(N) | O(N) |

#### Min-Heap (Priority Queue) — Admin Dispatch Engine
`O(log N) Time`

Used for: **Priority-sorted order listing** (`delivery.c → cmd_list_all_orders_sorted`)

A flat-array binary min-heap sorted by `slot_priority` (Morning=1, Afternoon=2, Evening=3). All orders are pushed into the heap from the SLL, then extracted in slot-priority order for dispatch. Morning slots continuously bubble to the root node, guaranteeing that high-urgency deliveries are always extracted first regardless of placement order.

```
Insert:       O(log N)   — heapify-up
Extract-min:  O(log N)   — heapify-down
Build:        O(N log N) — N insertions
```

#### Circular Linked List (CLL) — Logistics Engine
`Infinite Loop Routing`

Used for: **Round-robin delivery boy assignment** (`utils.c → cll_build_from_sll`, `cll_assign_delivery`)

Built at runtime from the `DeliveryBoyNode` SLL, containing only `is_active = 1` entries. A persistent `last_assigned` counter enables the CLL to resume from where it left off across subprocess calls. Wrap-around is automatic — `tail->next = head`. Guarantees mathematically equitable order distribution among all active delivery personnel with O(1) next-assignment.

#### Standard Queue (FIFO) — Order Processing Pipeline
`O(1) Enqueue | O(1) Dequeue`

Used for: **Sequential order ingestion** (`utils.c → queue_*`)

A `front/rear` pointer queue that ensures orders are processed strictly in arrival sequence before slot-priority sorting takes over at the dispatch stage.

---

### Search Algorithms

#### Hash Table — O(1) Lookup / Perfect Hashing

Used for: **All entity lookups by ID** (Users, Orders, Products, Free Items, Admins)

After the SLL is built, a pointer table is constructed once per binary invocation. The numeric suffix of every entity ID is extracted and offset by `ID_BASE (1001)` to produce a zero-based array index. The corresponding slot stores a direct pointer to the live SLL node.

```
ID: "U1042"  →  index = 1042 - 1001 = 41  →  user_table[41] → UserNode*
```

This completely bypasses O(N) SLL linear search for every read, update, and delete. All subsequent operations are O(1) pointer dereferences.

```c
int idx       = get_index_from_id(user_id);   // arithmetic extraction
UserNode *match = user_table[idx];            // O(1) — direct pointer access
```

No hash collisions are possible because IDs are system-generated sequentially — the hash is a pure arithmetic mapping with guaranteed uniqueness.

---

## Engineering Features

### Security & Networking

**SHA-256 Password Hashing** ─ `SHA-256 Cryptography`
Plaintext credentials are cryptographically hashed in Python before reaching the C engine. Flask uses Python's built-in `hashlib` — no OpenSSL dependency in the C layer:

```python
def _hash_password(plaintext: str) -> str:
    return hashlib.sha256(plaintext.encode("utf-8")).hexdigest()  # 64-char hex
```

The C structs store the 64-character hex digest in `char password[MAX_STR_LEN]` (128 bytes). All `strcmp` validations in `auth.c` operate entirely on hashes — the C layer is completely agnostic to this.

**OTP Verification** ─ `Time-To-Live Logic`
Generates secure, time-limited One Time Passwords for safe user registration and account recovery. OTP payloads are staged in server-side session memory with a TTL and cleared immediately on verification or expiry, preventing replay attacks and stale token reuse.

**HTTPS Security** ─ `Self-Signed X.509 SSL`
All intranet traffic is encrypted via TLS using a self-signed X.509 certificate. Ensures secure interactions with external APIs (Razorpay, SMTP) and prevents packet sniffing on the local network.

**Intranet Hosting** ─ `0.0.0.0 Network Binding`
The Flask server binds to `0.0.0.0` and dynamically detects the host's local IPv4 address at startup, broadcasting the LAN-accessible URL for seamless cross-device access on the same network — no manual IP configuration required.

---

### Automated Mailing & Integrations

**Receipt Dispatch** ─ `C-Based SMTP Engine`
On successful checkout, `mailer.c` instantly packages the generated PDF invoice and emails it directly to the customer's inbox — no Python mail library, no third-party SDK. The entire mail pipeline runs inside the C binary via direct SMTP relay.

**OTP Dispatch** ─ `Direct SMTP Relay`
On registration and password recovery, `mailer.c` wraps the generated OTP into a formatted email and relays it directly to the user's inbox. Same C-based SMTP engine, triggered instantly on the relevant operations.

**Payment Gateway** ─ `Razorpay API`
A secure 2-step checkout pipeline. After payment, the server performs HMAC-SHA256 signature verification on the Razorpay callback payload before marking any order as placed. Client-side cart values are never trusted — the signature check prevents cart spoofing and tampered payment amounts.

**Dynamic Receipts** ─ `FPDF Python Library`
Generates high-fidelity PDF invoices on the fly at checkout. Each receipt embeds the brand identity, exact order timestamp, full item breakdown with per-item price snapshots, delivery slot, assigned agent details, and transaction total. Also renderable inline via `html2pdf.js` for dark-theme browser preview before download.

---

## Getting Started

### Prerequisites

```
GCC         (tested with GCC 13+)
Python 3.x  (3.10+ recommended)
OpenSSL     (for HTTPS certificate generation)
```

### Install Python Dependencies

```bash
pip install flask fpdf2 razorpay
```

### Compile All C Binaries

```bash
cd backend/
bash build.sh
```

Produces: `auth`, `order`, `inventory`, `delivery`, `analytics`, `users`, `receipt`, `mailer`, `admin_tools`

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

All `.dat` files are flat binary — no delimiters, no headers. Records are fixed-size C structs written with `fwrite` and read back with `fread`. Sizes verified against `sizeof()` at runtime.

| Struct | Size | File |
|---|---|---|
| `User` | **916 bytes** | `users.dat` |
| `AdminCreds` | **532 bytes** | `admin_creds.dat` |
| `Vegetable` | — | `products.dat` |
| `Order` | — | `orders.dat` |
| `FreeItem` | — | `free_inventory.dat` |
| `DeliveryBoy` | — | `delivery_boys.dat` |

All size constants defined in `models.h` — `MAX_STR_LEN (128)`, `MAX_ID_LEN (20)`, `MAX_ADD_LEN (256)`.

---

## Troubleshooting

**Blank PDFs on receipt download**
CSS variables (`var(--accent)`) are stripped during `html2pdf.js` canvas rendering. Use hardcoded hex colours inside the receipt clone element.

**`[SKIP] users.dat not found` during migration**
Run the migration script from inside `backend/`, or apply the `os.path.abspath(__file__)` path fix so it resolves correctly from any working directory.

**Binary not found / permission denied**
Ensure `build.sh` ran successfully and binaries have execute permissions:
```bash
chmod +x backend/auth backend/order backend/inventory backend/delivery backend/analytics backend/users backend/receipt backend/mailer
```

**HTTPS certificate warning in browser**
Expected for self-signed certs. Click "Advanced → Proceed" on first access. Import `cert.pem` into your OS trust store to suppress permanently.

**404 on API routes**
Verify the `fetch` URLs in HTML templates match the route definitions in `app.py`. All API routes follow the pattern `/api/<resource>/<action>`.

---

*Developed by Team CodeCrafters — SDP-1 Project.*
