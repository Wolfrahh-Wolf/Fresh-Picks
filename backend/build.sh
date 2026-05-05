#!/bin/bash
# Fresh Picks - C backend build script

set -e

echo "================================================"
echo "  Fresh Picks - Building C Backend Binaries"
echo "================================================"

cd "$(dirname "$0")"

if command -v gcc >/dev/null 2>&1; then
    CC="gcc"
elif [ -x "/c/msys64/ucrt64/bin/gcc.exe" ]; then
    export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"
    CC="/c/msys64/ucrt64/bin/gcc.exe"
elif [ -x "/mingw64/bin/gcc.exe" ]; then
    export PATH="/mingw64/bin:/usr/bin:$PATH"
    CC="/mingw64/bin/gcc.exe"
else
    echo "ERROR: gcc was not found."
    echo "Install MSYS2 GCC, or add C:\\msys64\\ucrt64\\bin to PATH."
    exit 1
fi

echo "Using compiler: $CC"
echo ""

# ── 1. order binary ─────────────────────────────────────────────
echo "[1/8] Compiling order..."
gcc -Wall -Wextra -o order order.c utils.c -lm
echo "      ✓ order compiled successfully"

# ── 2. auth binary ──────────────────────────────────────────────
echo "[2/8] Compiling auth..."
gcc -Wall -Wextra -o auth auth.c utils.c -lm
echo "      ✓ auth compiled successfully"

# ── 3. inventory binary ─────────────────────────────────────────
echo "[3/8] Compiling inventory..."
gcc -Wall -Wextra -o inventory inventory.c utils.c -lm
echo "      ✓ inventory compiled successfully"

# ── 4. delivery binary ──────────────────────────────────────────
echo "[4/8] Compiling delivery..."
gcc -Wall -Wextra -o delivery delivery.c utils.c -lm
echo "      ✓ delivery compiled successfully"

# ── 5. receipt binary ───────────────────────────────────────────
echo "[5/8] Compiling receipt..."
gcc -Wall -Wextra -o receipt receipt.c utils.c -lm
echo "      ✓ receipt compiled successfully"

# ── 6. users binary ───────────────────────────────────────────
echo "[6/8] Compiling users..."
gcc -Wall -Wextra -o users users.c utils.c -lm
echo "      ✓ users compiled successfully"

# ── 7. analytics binary ───────────────────────────────────────────
echo "[7/8] Compiling users..."
gcc -Wall -Wextra -o analytics analytics.c utils.c -lm
echo "      ✓ analytics compiled successfully"

# ── 8. mailer binary ───────────────────────────────────────────
echo "[8/8] Compiling mailer..."
gcc -Wall -Wextra -o mailer mailer.c -lm
echo "      ✓ mailer compiled successfully"

echo ""
echo "[Setup] Creating carts/ directory..."
mkdir -p carts
echo "        carts/ directory ready"

echo ""
echo "  All binaries compiled successfully!"

# echo ""
# echo "  Starting Flask Server..."

# # ── Run app.py via PowerShell
# cd ../app
# python app.py

echo ""
echo "================================================"
echo "  All binaries compiled. Ready to run Flask."
echo ""
echo "  PowerShell: cd ../app; python app.py"
echo "================================================"
