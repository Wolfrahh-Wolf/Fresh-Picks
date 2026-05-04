#!/bin/bash
# =============================================================
# build.sh - Fresh Picks: C Backend Compilation Script (v3)
# =============================================================
# SPRINT 3 ADDITIONS:
#   - Compiles receipt.c -> receipt binary (PDF data extractor)
#
# HOW TO RUN:
#   Linux / macOS:   chmod +x build.sh && ./build.sh
#   Windows (Bash):  bash build.sh
#
# Team: CodeCrafters | Project: Fresh Picks | SDP-1
# =============================================================

echo "================================================"
echo "  Fresh Picks - Building C Backend Binaries"
echo "================================================"

# Navigate to the backend directory
cd "$(dirname "$0")"

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
echo "[7/8] Compiling mailer..."
gcc -Wall -Wextra -o mailer mailer.c -lcurl
echo "      ✓ mailer compiled successfully"

# ── Create the carts/ directory if it doesn't exist ─────────────
echo ""
echo "[Setup] Creating carts/ directory..."
mkdir -p carts
echo "        ✓ carts/ directory ready"

echo ""
echo "  All binaries compiled successfully!"

# echo ""
# echo "  Starting Flask Server..."

# # ── Run app.py via PowerShell
# cd ../app
# python app.py

echo ""
echo "================================================"
echo "  All binaries compiled! Ready to run Flask."
echo ""
echo "  Next step: Run app.py"
echo "  Git Bash:    cd ../app && python app.py"
echo "  PowerShell:  cd ../app; python app.py"
echo "  macOS/Linux: cd ../app && python3 app.py"
echo ""
echo "================================================"