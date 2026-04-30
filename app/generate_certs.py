"""
generate_certs.py - Fresh Picks: SSL Certificate Generator
============================================================
Run this script ONCE to generate two files:
  - cert.pem  (the "Digital ID Card" — public identity of the server)
  - key.pem   (the "Private Key"     — secret used to encrypt/decrypt)

These two files together enable HTTPS on our Flask server.

HOW TO RUN:
  cd Fresh-Picks/app
  pip install cryptography
  python generate_certs.py

WHAT GETS CREATED:
  Fresh-Picks/app/cert.pem   <- Give this to anyone who wants to verify you
  Fresh-Picks/app/key.pem    <- NEVER share this. Keep it secret.

═══════════════════════════════════════════════════════════════
  DEVELOPER GLOSSARY (Read this once — it will make sense!)
═══════════════════════════════════════════════════════════════

  SSL / TLS:
    SSL (Secure Sockets Layer) and its modern replacement TLS
    (Transport Layer Security) are protocols that ENCRYPT the
    data travelling between a browser and our server.

    Without SSL:  Browser → [username=alice&pass=1234] → Server
                  Anyone on the same Wi-Fi can READ this with a
                  tool like Wireshark. This is called a
                  "Man-in-the-Middle" (MITM) attack.

    With SSL:     Browser → [X#@!$%^&*9kQ2...] → Server
                  The data is scrambled. Only our server (holding
                  key.pem) can unscramble it. Safe on public Wi-Fi!

  Certificate (cert.pem):
    Think of this as our server's "Digital ID Card" or "Passport."
    It contains:
      - Our server's public key (used by browsers to encrypt data)
      - Our server's identity info (name, organization)
      - A validity period (expiry date)

    A REAL certificate is issued and signed by a trusted authority
    like DigiCert or Let's Encrypt. Browsers trust these.

  Self-Signed Certificate:
    Since we are a student project, we SIGN our own cert —
    like writing your own recommendation letter.
    The browser knows it wasn't signed by a trusted authority,
    so it shows a "⚠️ Your connection is not private" warning.
    This is EXPECTED and NORMAL for local/demo use.

    HOW TO BYPASS FOR DEMO:
      Chrome/Edge: Click "Advanced" → "Proceed to <IP> (unsafe)"
      Firefox:     Click "Advanced" → "Accept the Risk and Continue"

  key.pem (Private Key):
    This is the "master key" that decrypts everything the browser
    sends using our certificate. NEVER commit this to GitHub.
    It should be in your .gitignore file.

  RSA 2048-bit:
    The encryption algorithm we use. 2048-bit refers to the key
    size — like a password that is 617 digits long. Unbreakable
    with current computers.

Team: CodeCrafters | Project: Fresh Picks | SDP-1
"""

import os
import datetime
import sys
sys.stdout.reconfigure(encoding='utf-8')

try:
    from cryptography import x509
    from cryptography.x509.oid import NameOID
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.hazmat.backends import default_backend
except ImportError:
    print("ERROR: 'cryptography' library not found.")
    print("Fix:  pip install cryptography")
    exit(1)


# ─────────────────────────────────────────────────────────────
# CONFIGURATION — Edit these if you want different values.
# These are embedded in the certificate's identity fields.
# ─────────────────────────────────────────────────────────────

# Where to save the generated files (same folder as app.py)
OUTPUT_DIR   = os.path.dirname(os.path.abspath(__file__))
CERT_FILE    = os.path.join(OUTPUT_DIR, "cert.pem")
KEY_FILE     = os.path.join(OUTPUT_DIR, "key.pem")

# Certificate identity metadata
ORG_NAME     = "CodeCrafters"          # Your team/organization name
PROJECT_NAME = "FreshPicks"            # Common Name (CN) — like a hostname label
COUNTRY      = "IN"                    # 2-letter ISO country code (IN = India)
STATE        = "Tamil Nadu"            # State or province
CITY         = "Chennai"               # City / Locality

# How long the certificate is valid (in days).
# 365 = 1 year. For a demo project, this is more than enough.
VALIDITY_DAYS = 365


def generate_private_key():
    """
    PURPOSE: Generate an RSA private key.

    RSA (Rivest–Shamir–Adleman) is a public-key cryptography algorithm.
    It generates a KEY PAIR:
      - Private Key (key.pem): kept secret on the server
      - Public Key  (embedded in cert.pem): shared with everyone

    HOW ENCRYPTION WORKS WITH THIS PAIR:
      1. Browser gets our public key from cert.pem
      2. Browser encrypts its data using the public key
      3. Only our private key (key.pem) can decrypt it
      → Even if someone steals the encrypted data, they can't
        read it without key.pem

    public_exponent=65537:
      A standard value used in RSA. Think of it as a parameter
      in the math formula. 65537 is chosen because it's fast
      to compute and cryptographically secure.

    key_size=2048:
      The size of the key in bits. 2048-bit = ~617 decimal digits.
      The larger the key, the harder it is to crack.
      2048-bit is the current industry standard minimum.
    """
    print("[1/3] Generating RSA 2048-bit private key...")

    private_key = rsa.generate_private_key(
        public_exponent = 65537,          # Standard RSA public exponent
        key_size        = 2048,           # Key strength in bits
        backend         = default_backend()
    )

    print("      ✓ Private key generated.")
    return private_key


def generate_certificate(private_key):
    """
    PURPOSE: Build and self-sign an X.509 certificate.

    X.509 is the standard format for SSL/TLS certificates.
    It's like a form with fields:
      - Who is this certificate for? (Subject)
      - Who issued/signed it?        (Issuer — same as Subject for self-signed)
      - When does it expire?         (Validity period)
      - What is the public key?      (Embedded inside)

    SELF-SIGNED means:
      Normally, a Certificate Authority (CA) like DigiCert checks
      your identity and then signs your certificate with their
      trusted key. Browsers trust those CAs by default.

      Since we are signing it ourselves (subject == issuer),
      browsers don't recognize our "authority" — hence the warning.
      For a college demo over LAN, this is perfectly fine.
    """
    print("[2/3] Building self-signed X.509 certificate...")

    # ── Subject & Issuer ─────────────────────────────────────────
    # For a self-signed cert, Subject == Issuer (we sign our own cert)
    # x509.Name() is like filling out an identity form
    identity = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME,             COUNTRY),
        x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME,   STATE),
        x509.NameAttribute(NameOID.LOCALITY_NAME,            CITY),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME,        ORG_NAME),
        x509.NameAttribute(NameOID.COMMON_NAME,              PROJECT_NAME),
    ])

    # ── Validity Period ──────────────────────────────────────────
    # The certificate is valid from RIGHT NOW until VALIDITY_DAYS later.
    # datetime.timezone.utc ensures we use UTC (the internet's standard timezone).
    now     = datetime.datetime.now(datetime.timezone.utc)
    expires = now + datetime.timedelta(days=VALIDITY_DAYS)

    # ── Subject Alternative Names (SAN) ──────────────────────────
    # SANs tell the browser which hostnames/IPs this cert is valid for.
    # We add both localhost AND the wildcard IP so the cert works whether
    # accessed via http://localhost:5000 or http://192.168.x.x:5000
    san = x509.SubjectAlternativeName([
        x509.DNSName("localhost"),           # Valid for: https://localhost:5000
        x509.IPAddress(__import__("ipaddress").IPv4Address("127.0.0.1")),
        x509.IPAddress(__import__("ipaddress").IPv4Address("0.0.0.0")),
    ])

    # ── Build the Certificate ────────────────────────────────────
    cert = (
        x509.CertificateBuilder()
            .subject_name(identity)          # WHO this cert belongs to
            .issuer_name(identity)           # WHO signed it (us, for self-signed)
            .public_key(private_key.public_key())  # Embed our PUBLIC key
            .serial_number(x509.random_serial_number())  # Unique ID for this cert
            .not_valid_before(now)           # Valid starting: now
            .not_valid_after(expires)        # Valid until: now + 365 days
            .add_extension(san, critical=False)  # Add the SAN extension
            .sign(
                private_key,                 # Sign WITH our private key
                hashes.SHA256(),             # Using SHA-256 hash algorithm
                default_backend()
            )
    )

    print("      ✓ Certificate built and self-signed.")
    return cert


def save_files(private_key, cert):
    """
    PURPOSE: Write the private key and certificate to .pem files.

    PEM format (Privacy Enhanced Mail):
      An old name for a common text-based format for storing
      cryptographic objects. It looks like:
        -----BEGIN CERTIFICATE-----
        MIIBIjANBgkq...base64 encoded data...
        -----END CERTIFICATE-----

    The data inside is Base64-encoded — a way to represent binary
    data as readable ASCII text so it can be stored in a text file.

    Encryption for key.pem:
      serialization.NoEncryption() means we DON'T password-protect
      the key file. For a demo project this is fine. In production,
      you would use BestAvailableEncryption(b"your_passphrase").
    """
    print("[3/3] Writing files to disk...")

    # ── Save Private Key → key.pem ───────────────────────────────
    with open(KEY_FILE, "wb") as f:
        f.write(private_key.private_bytes(
            encoding             = serialization.Encoding.PEM,
            format               = serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm = serialization.NoEncryption()
            # NoEncryption = key is NOT password protected (fine for demos)
        ))
    print(f"      ✓ Private key saved → {KEY_FILE}")

    # ── Save Certificate → cert.pem ──────────────────────────────
    with open(CERT_FILE, "wb") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))
    print(f"      ✓ Certificate saved → {CERT_FILE}")


# ─────────────────────────────────────────────────────────────
# MAIN - Entry point. Runs all three steps in sequence.
# ─────────────────────────────────────────────────────────────
if __name__ == "__main__":
    print()
    print("=" * 54)
    print("  FreshPicks — SSL Certificate Generator")
    print("  Team: CodeCrafters | SDP-1")
    print("=" * 54)

    # Check if files already exist and warn before overwriting
    if os.path.exists(CERT_FILE) or os.path.exists(KEY_FILE):
        print()
        print("  ⚠️  WARNING: cert.pem / key.pem already exist.")
        answer = input("  Overwrite them? (y/n): ").strip().lower()
        if answer != "y":
            print("  Aborted. Existing certificates kept.")
            exit(0)

    print()

    # Step 1: Generate the private key
    private_key = generate_private_key()

    # Step 2: Generate the certificate (signed by the private key)
    cert = generate_certificate(private_key)

    # Step 3: Write both to disk
    save_files(private_key, cert)

    print()
    print("=" * 54)
    print("  Done! Two files created:")
    print(f"    cert.pem → {CERT_FILE}")
    print(f"    key.pem  → {KEY_FILE}")
    print()
    print("  NEXT STEPS:")
    print("  1. Run: python app.py")
    print("  2. Open: https://localhost:5000")
    print("  3. Browser warning? Click: Advanced → Proceed")
    print()
    print("  ⚠️  Add key.pem and cert.pem to your .gitignore!")
    print("      Never push private keys to GitHub.")
    print("=" * 54)
    print()
