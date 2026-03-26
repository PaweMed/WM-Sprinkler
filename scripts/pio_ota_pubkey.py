Import("env")

import os
import re


def find_pubkey_hex_file(project_dir):
    candidates = [
        os.path.join(project_dir, "secrets", "ota-ed25519-pubkey.hex"),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return ""


def read_pubkey_hex(path):
    with open(path, "r", encoding="utf-8") as f:
        value = f.read().strip().lower()
    value = "".join(ch for ch in value if ch in "0123456789abcdef")
    if not re.fullmatch(r"[0-9a-f]{64}", value or ""):
        raise SystemExit(
            "[OTA] Invalid Ed25519 public key hex in {}. Expected 64 hex chars.".format(path)
        )
    return value


project_dir = env.subst("$PROJECT_DIR")
pubkey_path = find_pubkey_hex_file(project_dir)
if not pubkey_path:
    raise SystemExit(
        "[OTA] Missing public key file: secrets/ota-ed25519-pubkey.hex"
    )

pubkey_hex = read_pubkey_hex(pubkey_path)
env.Append(
    CPPDEFINES=[
        ("OTA_ED25519_PUBLIC_KEY_HEX", '\\"{}\\"'.format(pubkey_hex))
    ]
)
print("[OTA] Embedded Ed25519 public key from: {}".format(pubkey_path))
