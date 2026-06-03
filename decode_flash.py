#!/usr/bin/env python3
"""
Sucht im Flash-Dump nach verschlüsselten Einträgen (Flag 0x01 + 16-Byte-IV + AES-CBC)
und entschlüsselt sie mit dem AES-Master-Key aus .env
"""
import os, re, sys
from Cryptodome.Cipher import AES

BASE = os.path.dirname(__file__)
FLASH = os.path.join(BASE, "dump", "flash.bin")
ENV   = os.path.join(BASE, ".env")
OUT   = os.path.join(BASE, "dump")

def load_key():
    with open(ENV) as f:
        for line in f:
            m = re.match(r'AES_MASTER_KEY\s*=\s*([0-9A-Fa-f]{32})', line.strip())
            if m:
                return bytes.fromhex(m.group(1))
    raise ValueError("AES_MASTER_KEY nicht in .env gefunden")

def try_decrypt(key, data):
    """Versucht zu entschlüsseln; gibt Klartext zurück oder None bei ungültigem Padding."""
    if len(data) < 33 or data[0] != 0x01:
        return None
    iv         = data[1:17]
    ciphertext = data[17:]
    if len(ciphertext) % 16 != 0:
        return None
    try:
        pt  = AES.new(key, AES.MODE_CBC, iv).decrypt(ciphertext)
        pad = pt[-1]
        if pad == 0 or pad > 16:
            return None
        if pt[-pad:] != bytes([pad]) * pad:
            return None
        return pt[:-pad]
    except Exception:
        return None

def is_printable(b):
    return all(0x09 <= c <= 0x7E or c in (0x0A, 0x0D) for c in b)

def main():
    key  = load_key()
    data = open(FLASH, "rb").read()
    print("⚠️  This recovers plaintext secrets to dump/*.txt — delete them securely when done.")
    print(f"Flash: {len(data)} Bytes\n")

    found = 0
    i = 0
    while i < len(data) - 33:
        if data[i] == 0x01:
            # Probiere verschiedene Ciphertext-Längen (Vielfache von 16)
            for ct_len in range(16, min(800, len(data) - i - 17 + 1), 16):
                candidate = data[i : i + 1 + 16 + ct_len]
                pt = try_decrypt(key, candidate)
                if pt and len(pt) >= 4 and is_printable(pt):
                    found += 1
                    fname = os.path.join(OUT, f"secret_{found}_offset0x{i:04X}.txt")
                    # Plaintext secret on disk: create 0600 so it is not world-readable.
                    fd = os.open(fname, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
                    with os.fdopen(fd, "wb") as f:
                        f.write(pt)
                    preview = pt[:80].decode("latin-1").replace("\n", "↵")
                    print(f"[{found}] Offset 0x{i:04X}  {len(pt)} Bytes  →  dump/{os.path.basename(fname)}")
                    print(f"      Vorschau: {preview}")
                    i += 1 + 16 + ct_len - 1  # weiter nach diesem Block
                    break
        i += 1

    if found == 0:
        print("Keine gültigen Einträge gefunden — falscher Key oder kein Match.")
    else:
        print(f"\n{found} Einträge in dump/ gespeichert.")

if __name__ == "__main__":
    main()
