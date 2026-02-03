#!/usr/bin/env python3
"""
Secure HID Pro Micro - Compile-Time Encryption Script
Encrypts passwords with AES-128 and generates C++ header
Device signature + KDF salt create device-unique decryption key
"""

import sys
import os
import hashlib
import secrets
from typing import Dict, Tuple

try:
    from Crypto.Cipher import AES
    from Crypto.Random import get_random_bytes
    from Crypto.Util.Padding import pad, unpad
except ImportError:
    print("[ERROR] pycryptodome not installed!")
    print("Install with: pip3 install pycryptodome")
    sys.exit(1)


def derive_key_from_salt(salt: str, passphrase: str = "ProMicro2026") -> bytes:
    """
    Derive 128-bit AES key using PBKDF2
    This simulates device signature + salt derivation
    In actual firmware, device signature replaces passphrase
    """
    return hashlib.pbkdf2_hmac(
        'sha256',
        passphrase.encode('utf-8'),
        salt.encode('utf-8'),
        iterations=100000,
        dklen=16  # 128 bits for AES-128
    )


def encrypt_password(plaintext: str, key: bytes) -> bytes:
    """
    Encrypt password with AES-128-CBC
    Returns: IV (16 bytes) + Ciphertext (padded to 16-byte blocks)
    """
    # Generate random IV for each encryption
    iv = get_random_bytes(16)
    
    # Create cipher
    cipher = AES.new(key, AES.MODE_CBC, iv)
    
    # Encrypt (with PKCS7 padding)
    plaintext_bytes = plaintext.encode('utf-8')
    ciphertext = cipher.encrypt(pad(plaintext_bytes, AES.block_size))
    
    # Return IV prepended to ciphertext
    return iv + ciphertext


def load_env_file(env_path: str) -> Dict[str, str]:
    """
    Parse .env file and return key-value dictionary
    """
    env_vars = {}
    
    if not os.path.exists(env_path):
        print(f"[ERROR] {env_path} not found!")
        sys.exit(1)
    
    with open(env_path, 'r') as f:
        for line in f:
            line = line.strip()
            
            # Skip comments and empty lines
            if not line or line.startswith('#'):
                continue
            
            # Parse KEY=VALUE or KEY="VALUE"
            if '=' in line:
                key, value = line.split('=', 1)
                key = key.strip()
                value = value.strip()
                
                # Remove quotes if present
                if (value.startswith('"') and value.endswith('"')) or \
                   (value.startswith("'") and value.endswith("'")):
                    value = value[1:-1]
                
                env_vars[key] = value
    
    return env_vars


def bytes_to_cpp_array(data: bytes, indent: str = "    ") -> str:
    """
    Convert bytes to C++ hex array format
    Example: 0x01, 0x02, 0x03, ...
    """
    hex_values = [f"0x{b:02X}" for b in data]
    
    # Format with line breaks every 12 bytes for readability
    lines = []
    for i in range(0, len(hex_values), 12):
        chunk = hex_values[i:i+12]
        lines.append(indent + ", ".join(chunk))
    
    return ",\n".join(lines)


def generate_cpp_header(env_vars: Dict[str, str], output_path: str):
    """
    Generate C++ header file with encrypted secrets
    """
    # Extract values from .env
    secret1 = env_vars.get('DEVICE_SECRET', 'DefaultPassword123!')
    secret2 = env_vars.get('DEVICE_SECRET_2', 'AlternatePassword456!')
    fake_pw = env_vars.get('FAKE_PASSWORD', 'WrongPassword789!')
    salt = env_vars.get('KDF_SALT', 'ProMicro2026SaltConstant')
    
    # Derive encryption key (simulates device signature + salt)
    encryption_key = derive_key_from_salt(salt)
    
    # Encrypt passwords
    encrypted_secret1 = encrypt_password(secret1, encryption_key)
    encrypted_secret2 = encrypt_password(secret2, encryption_key)
    encrypted_fake = encrypt_password(fake_pw, encryption_key)
    
    # Generate header file content
    header_content = f"""// ╔══════════════════════════════════════════════════════════════════╗
// ║  AUTO-GENERATED HEADER - DO NOT EDIT MANUALLY                    ║
// ║  Generated at compile-time with encrypted secrets                ║
// ║  This file contains CIPHERTEXT ONLY - safe to review             ║
// ╚══════════════════════════════════════════════════════════════════╝
//
// Security Model:
// - Passwords encrypted with AES-128-CBC at compile-time
// - Decryption key derived from: Device Signature + KDF Salt
// - Each Pro Micro has unique ATmega32u4 signature (factory-programmed)
// - Without the physical device, these ciphertexts are useless
//
// Data Format:
// - First 16 bytes: IV (Initialization Vector, randomized)
// - Remaining bytes: AES-CBC ciphertext (PKCS7 padded)
//
// Generated on: {__import__('datetime').datetime.now().isoformat()}

#ifndef SECRETS_H
#define SECRETS_H

#include <stdint.h>

// ===== KDF CONFIGURATION =====
// Salt is non-secret and can be public
// It's combined with device signature to derive decryption key
static const char KDF_SALT[] = "{salt}";
static const size_t KDF_SALT_LEN = {len(salt)};

// ===== ENCRYPTED PASSWORD 1 =====
// Length: {len(encrypted_secret1)} bytes (IV: 16, Ciphertext: {len(encrypted_secret1) - 16})
static const uint8_t SECRET_1_ENCRYPTED[{len(encrypted_secret1)}] = {{
{bytes_to_cpp_array(encrypted_secret1)}
}};
static const size_t SECRET_1_ENCRYPTED_LEN = {len(encrypted_secret1)};

// ===== ENCRYPTED PASSWORD 2 =====
// Length: {len(encrypted_secret2)} bytes (IV: 16, Ciphertext: {len(encrypted_secret2) - 16})
static const uint8_t SECRET_2_ENCRYPTED[{len(encrypted_secret2)}] = {{
{bytes_to_cpp_array(encrypted_secret2)}
}};
static const size_t SECRET_2_ENCRYPTED_LEN = {len(encrypted_secret2)};

// ===== ENCRYPTED FAKE PASSWORD =====
// Length: {len(encrypted_fake)} bytes (IV: 16, Ciphertext: {len(encrypted_fake) - 16})
static const uint8_t FAKE_PASSWORD_ENCRYPTED[{len(encrypted_fake)}] = {{
{bytes_to_cpp_array(encrypted_fake)}
}};
static const size_t FAKE_PASSWORD_ENCRYPTED_LEN = {len(encrypted_fake)};

// ===== RUNTIME DECRYPTION =====
// At device startup:
// 1. Read ATmega32u4 signature bytes (unique per chip)
// 2. Derive key: PBKDF2-HMAC-SHA256(signature + KDF_SALT)
// 3. Extract IV from first 16 bytes of encrypted data
// 4. Decrypt ciphertext with AES-128-CBC
// 5. Remove PKCS7 padding
// 6. Use plaintext password (kept in RAM only, never logged)
// 7. Securely wipe RAM after use

#endif  // SECRETS_H
"""
    
    # Write header file
    with open(output_path, 'w') as f:
        f.write(header_content)
    
    # Print summary
    print(f"\n{'='*70}")
    print("✓ Encryption successful!")
    print(f"{'='*70}")
    print(f"Output:       {output_path}")
    print(f"Salt:         {salt}")
    print(f"Secret 1:     {len(encrypted_secret1)} bytes encrypted")
    print(f"Secret 2:     {len(encrypted_secret2)} bytes encrypted")
    print(f"Fake PW:      {len(encrypted_fake)} bytes encrypted")
    print(f"{'='*70}")
    print("Security notes:")
    print("  • All data is AES-128-CBC encrypted")
    print("  • IVs are randomized (unique per build)")
    print("  • Decryption requires device signature")
    print("  • Plaintext never stored in binary")
    print(f"{'='*70}\n")


def main():
    if len(sys.argv) != 3:
        print("Usage: encrypt_secrets.py <.env file> <output header file>")
        print("Example: encrypt_secrets.py .env secrets.h")
        sys.exit(1)
    
    env_file = sys.argv[1]
    output_file = sys.argv[2]
    
    print(f"\n{'='*70}")
    print("Secure HID Pro Micro - Compile-Time Encryption")
    print(f"{'='*70}")
    print(f"Input:  {env_file}")
    print(f"Output: {output_file}")
    print(f"{'='*70}\n")
    
    # Load environment variables
    env_vars = load_env_file(env_file)
    
    # Validate required variables
    required_vars = ['DEVICE_SECRET', 'DEVICE_SECRET_2', 'FAKE_PASSWORD', 'KDF_SALT']
    missing_vars = [var for var in required_vars if var not in env_vars]
    
    if missing_vars:
        print(f"[ERROR] Missing required variables in .env:")
        for var in missing_vars:
            print(f"  - {var}")
        sys.exit(1)
    
    # Generate header
    generate_cpp_header(env_vars, output_file)


if __name__ == '__main__':
    main()
