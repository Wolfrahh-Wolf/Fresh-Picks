"""
migrate_passwords.py
One-shot migration: plaintext → SHA-256 hex in users.dat and admin_creds.dat.
Constants verified against models.h (Fresh Picks v4).
"""

import hashlib
import struct
import os

# ── Mirrored exactly from models.h ──────────────────────────────────────────
MAX_ID_LEN  = 20    # was incorrectly 16 in the previous version — NOW FIXED
MAX_STR_LEN = 128
MAX_ADD_LEN = 256

# User  : user_id | username | password | full_name | email | phone | address
USER_FMT  = f"{MAX_ID_LEN}s{MAX_STR_LEN}s{MAX_STR_LEN}s{MAX_STR_LEN}s{MAX_STR_LEN}s{MAX_STR_LEN}s{MAX_ADD_LEN}s"
USER_SIZE = struct.calcsize(USER_FMT)

# Admin : admin_id | username | password | admin_name | email
ADMIN_FMT  = f"{MAX_ID_LEN}s{MAX_STR_LEN}s{MAX_STR_LEN}s{MAX_STR_LEN}s{MAX_STR_LEN}s"
ADMIN_SIZE = struct.calcsize(ADMIN_FMT)

# Password field index (0-based) in each unpacked tuple
USER_PASS_IDX  = 2   # user_id=0, username=1, password=2
ADMIN_PASS_IDX = 2   # admin_id=0, username=1, password=2


def _sha256(plaintext_bytes: bytes) -> bytes:
    """
    Decode a null-padded C char[] field, hash the plaintext,
    and return a null-padded bytes object sized to MAX_STR_LEN.
    """
    plaintext = plaintext_bytes.rstrip(b"\x00").decode("utf-8", errors="replace")
    digest    = hashlib.sha256(plaintext.encode("utf-8")).hexdigest()  # 64 hex chars
    return digest.encode("utf-8").ljust(MAX_STR_LEN, b"\x00")[:MAX_STR_LEN]


def migrate(dat_path: str, fmt: str, record_size: int, pass_idx: int) -> int:
    """
    Read all fixed-size records, replace the password field with its
    SHA-256 hex digest, write back atomically. Returns record count migrated.
    """
    if not os.path.exists(dat_path):
        print(f"  [SKIP] {dat_path} not found.")
        return 0

    with open(dat_path, "rb") as fh:
        raw = fh.read()

    if len(raw) % record_size != 0:
        raise ValueError(
            f"{dat_path}: file size {len(raw)} is not a multiple of "
            f"record size {record_size}. Aborting — recheck MAX_* constants."
        )

    n_records = len(raw) // record_size
    out = bytearray()

    for i in range(n_records):
        chunk  = raw[i * record_size : (i + 1) * record_size]
        fields = list(struct.unpack(fmt, chunk))

        password_field = fields[pass_idx]

        # Idempotency guard: if the first 64 bytes are all lowercase hex
        # and non-null, this record was already migrated — skip it.
        candidate = password_field[:64]
        if (candidate[0] != 0 and
                all(chr(c) in "0123456789abcdef" for c in candidate)):
            print(f"  [SKIP] record {i} in {dat_path} — already hashed.")
            out += chunk
            continue

        fields[pass_idx] = _sha256(password_field)
        out += struct.pack(fmt, *fields)

    # Atomic replace — original is safe until this line succeeds
    tmp_path = dat_path + ".migrating"
    with open(tmp_path, "wb") as fh:
        fh.write(out)
    os.replace(tmp_path, dat_path)

    return n_records


if __name__ == "__main__":
    # Quick self-check: print computed record sizes so you can cross-verify
    # with sizeof(User) and sizeof(AdminCreds) from a C printf if needed.
    print(f"  Computed USER_SIZE  : {USER_SIZE} bytes")
    print(f"  Computed ADMIN_SIZE : {ADMIN_SIZE} bytes")
    print()

    print("Fresh Picks — Password Migration (plaintext → SHA-256)")
    print("=" * 54)

    _BACKEND = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "backend")

    u = migrate(os.path.join(_BACKEND, "users.dat"),       USER_FMT,  USER_SIZE,  USER_PASS_IDX)
    a = migrate(os.path.join(_BACKEND, "admin_creds.dat"), ADMIN_FMT, ADMIN_SIZE, ADMIN_PASS_IDX)

    print(f"  users.dat       : {u} record(s) processed")
    print(f"  admin_creds.dat : {a} record(s) processed")
    print("Done.")