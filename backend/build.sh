#!/usr/bin/env bash
set -e

echo "================================================"
echo "  Fresh Picks - Building C Backend Binaries"
echo "================================================"

cd "$(dirname "$0")"

CC="/c/msys64/ucrt64/bin/gcc"
CURL_FLAGS="-I/c/msys64/ucrt64/include -L/c/msys64/ucrt64/lib -lcurl"
CFLAGS="-Wall -Wextra"

COMMON_SRC="utils.c"

echo "Using compiler: $CC"
echo ""

# ── 1. order ─────────────────────────────────────────────
echo "Compiling order..."
"$CC" $CFLAGS -o order order.c $COMMON_SRC -lm
echo "      ✓ order compiled"

# ── 2. auth ──────────────────────────────────────────────
echo "Compiling auth..."
"$CC" $CFLAGS -o auth auth.c $COMMON_SRC -lm
echo "      ✓ auth compiled"

# ── 3. inventory ─────────────────────────────────────────
echo "Compiling inventory..."
"$CC" $CFLAGS -o inventory inventory.c $COMMON_SRC -lm
echo "      ✓ inventory compiled"

# ── 4. delivery ──────────────────────────────────────────
echo "Compiling delivery..."
"$CC" $CFLAGS -o delivery delivery.c $COMMON_SRC -lm
echo "      ✓ delivery compiled"

# ── 5. receipt ───────────────────────────────────────────
echo "Compiling receipt..."
"$CC" $CFLAGS -o receipt receipt.c $COMMON_SRC -lm
echo "      ✓ receipt compiled"

# ── 6. users ─────────────────────────────────────────────
echo "Compiling users..."
"$CC" $CFLAGS -o users users.c $COMMON_SRC -lm
echo "      ✓ users compiled"

# ── 7. analytics ─────────────────────────────────────────
echo "Compiling analytics..."
"$CC" $CFLAGS -o analytics analytics.c $COMMON_SRC -lm
echo "      ✓ analytics compiled"

# ── 8. mailer (with curl) ────────────────────────────────
echo "Compiling mailer..."
"$CC" $CFLAGS -o mailer mailer.c $COMMON_SRC $CURL_FLAGS -lm
echo "      ✓ mailer compiled (libcurl)"

# ── Setup ────────────────────────────────────────────────
echo ""
echo "[Setup] Creating carts/ directory..."
mkdir -p carts
echo "        carts/ directory ready"

echo ""
echo "================================================"
echo "  All binaries compiled. Ready to run Flask."
echo ""
echo "  PowerShell: cd ../app; python app.py"
echo "================================================"


# echo ""
# echo "  Starting Flask Server..."

# # ── Run app.py via PowerShell
# cd ../app
# python app.py
