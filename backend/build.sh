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

echo "[1/7] Compiling order..."
"$CC" -Wall -Wextra -o order order.c utils.c -lm
echo "      order compiled successfully"

echo "[2/7] Compiling auth..."
"$CC" -Wall -Wextra -o auth auth.c utils.c -lm
echo "      auth compiled successfully"

echo "[3/7] Compiling inventory..."
"$CC" -Wall -Wextra -o inventory inventory.c utils.c -lm
echo "      inventory compiled successfully"

echo "[4/7] Compiling delivery..."
"$CC" -Wall -Wextra -o delivery delivery.c utils.c -lm
echo "      delivery compiled successfully"

echo "[5/7] Compiling receipt..."
"$CC" -Wall -Wextra -o receipt receipt.c utils.c -lm
echo "      receipt compiled successfully"

echo "[6/7] Compiling users..."
"$CC" -Wall -Wextra -o users users.c utils.c -lm
echo "      users compiled successfully"

echo "[7/7] Compiling mailer..."
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libcurl; then
    CURL_FLAGS="$(pkg-config --cflags --libs libcurl)"
elif command -v curl-config >/dev/null 2>&1; then
    CURL_FLAGS="$(curl-config --cflags --libs)"
else
    CURL_FLAGS="-lcurl"
fi
"$CC" -Wall -Wextra -o mailer mailer.c $CURL_FLAGS
echo "      mailer compiled successfully"

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
