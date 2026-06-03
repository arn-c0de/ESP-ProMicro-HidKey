#!/usr/bin/env python3
import os
import re
import sys
from pathlib import Path
from Cryptodome.Cipher import AES
from Cryptodome.Util.Padding import pad
from Cryptodome.Random import get_random_bytes

MAX_SEQUENCE_LENGTH = 20
CONTENT_TYPE_TEXT = 0x00
CONTENT_TYPE_GPG_PRIVATE_KEY = 0x01

def parse_env_value(raw_value):
    value = raw_value.strip()
    if len(value) < 2:
        return value

    quote = value[0]
    if quote not in ('"', "'") or value[-1] != quote:
        return value

    inner = value[1:-1]
    if quote == '"':
        return (
            inner
            .replace('\\"', '"')
            .replace("\\'", "'")
            .replace('\\\\', '\\')
        )
    return inner.replace("\\'", "'").replace('\\\\', '\\')

def load_env(env_path):
    env_vars = {}
    if not env_path.exists():
        print(f"Error: {env_path} not found!", file=sys.stderr)
        sys.exit(1)
    with open(env_path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#') and '=' in line:
                key, value = line.split('=', 1)
                env_vars[key.strip()] = parse_env_value(value)
    return env_vars

def decode_escaped_value(value):
    # Decode escape sequences to their literal characters. Line endings are
    # normalized later in normalize_secret(); here \r must decode to a real
    # carriage return, not a newline (mapping \r -> \n silently corrupts data).
    return (
        value
        .replace('\\r\\n', '\r\n')
        .replace('\\n', '\n')
        .replace('\\r', '\r')
        .replace('\\t', '\t')
        .replace('\\\\', '\\')
    )

def load_secret_from_file(path_value, base_dir):
    secret_path = Path(path_value)
    if not secret_path.is_absolute():
        secret_path = (base_dir / secret_path).resolve()
    if not secret_path.exists():
        print(f"Error: Secret file not found: {secret_path}", file=sys.stderr)
        sys.exit(1)
    return secret_path.read_text(encoding='utf-8')

def normalize_secret(secret_type, secret_value):
    normalized = secret_value.replace('\r\n', '\n').replace('\r', '\n')
    if secret_type == CONTENT_TYPE_GPG_PRIVATE_KEY and normalized and not normalized.endswith('\n'):
        normalized += '\n'
    return normalized

def parse_content_type(raw_value, index):
    normalized = raw_value.strip().lower().replace('_', '-')
    if normalized in ('text', 'password', 'secret'):
        return CONTENT_TYPE_TEXT
    if normalized in ('gpg', 'gpg-private-key', 'gpg-private', 'pgp-private-key', 'private-key'):
        return CONTENT_TYPE_GPG_PRIVATE_KEY
    print(f"Error: Unsupported COMBINATION_{index}_TYPE={raw_value}", file=sys.stderr)
    sys.exit(1)

def load_combination_secret(env_vars, index, content_type, base_dir):
    file_key = f'COMBINATION_{index}_SECRET_FILE'
    secret_key = f'COMBINATION_{index}_SECRET'
    password_key = f'COMBINATION_{index}_PASSWORD'
    gpg_file_key = f'COMBINATION_{index}_GPG_PRIVATE_KEY_FILE'

    if file_key in env_vars:
        value = load_secret_from_file(env_vars[file_key], base_dir)
    elif content_type == CONTENT_TYPE_GPG_PRIVATE_KEY and gpg_file_key in env_vars:
        value = load_secret_from_file(env_vars[gpg_file_key], base_dir)
    elif secret_key in env_vars:
        value = decode_escaped_value(env_vars[secret_key])
    elif password_key in env_vars:
        value = decode_escaped_value(env_vars[password_key])
    else:
        print(
            f"Error: Missing secret for COMBINATION_{index}. "
            f"Use COMBINATION_{index}_PASSWORD, COMBINATION_{index}_SECRET, "
            f"COMBINATION_{index}_SECRET_FILE or COMBINATION_{index}_GPG_PRIVATE_KEY_FILE.",
            file=sys.stderr,
        )
        sys.exit(1)

    return normalize_secret(content_type, value)

def parse_combinations(env_vars):
    count = int(env_vars.get('COMBINATION_COUNT', '0'))
    if count <= 0:
        print("Error: COMBINATION_COUNT must be > 0!", file=sys.stderr)
        sys.exit(1)
    combinations = []
    script_dir = Path(__file__).parent
    for i in range(count):
        seq_key = f'COMBINATION_{i}_SEQUENCE'
        if seq_key not in env_vars:
            print(f"Error: Missing {seq_key}!", file=sys.stderr)
            sys.exit(1)
        sequence = [int(x.strip()) for x in env_vars[seq_key].split(',')]
        if len(sequence) > MAX_SEQUENCE_LENGTH:
            print(f"Error: {seq_key} exceeds {MAX_SEQUENCE_LENGTH} presses", file=sys.stderr)
            sys.exit(1)

        type_key = f'COMBINATION_{i}_TYPE'
        content_type = parse_content_type(env_vars.get(type_key, 'text'), i)
        secret = load_combination_secret(env_vars, i, content_type, script_dir)

        combinations.append({
            'index': i,
            'sequence': sequence,
            'content_type': content_type,
            'secret': secret,
        })
    return combinations

def aes_encrypt_cbc(secret_bytes, key_hex):
    """Encrypt secret bytes using AES-128-CBC."""
    key = bytes.fromhex(key_hex)
    padded = pad(secret_bytes, AES.block_size)
    
    # Generate random IV (16 bytes)
    iv = get_random_bytes(AES.block_size)
    
    # Encrypt with CBC mode
    cipher = AES.new(key, AES.MODE_CBC, iv)
    ciphertext = cipher.encrypt(padded)
    
    # Return IV + ciphertext as list
    return list(iv + ciphertext)

def generate_header(combinations, aes_key, output_path):
    sequence_defs = []
    password_defs = []
    seq_refs = []
    max_plaintext_len = 0
    max_encrypted_len = 0
    
    for position, comb in enumerate(combinations):
        idx = comb['index']
        seq_arr = ", ".join(str(s) for s in comb['sequence'])
        sequence_defs.append(f"const byte PROGMEM seq_{idx}[] = {{{seq_arr}}};")

        # GPG keys are UTF-8; text secrets use Latin-1 so each character maps to a
        # single byte the firmware types verbatim (matches its DE high-byte table).
        encoding = 'utf-8' if comb['content_type'] == CONTENT_TYPE_GPG_PRIVATE_KEY else 'latin-1'
        try:
            secret_bytes = comb['secret'].encode(encoding)
        except UnicodeEncodeError:
            # Use `position` (from enumerate), not anything read from `comb`: the
            # comb dict holds the secret, and including any of its fields — or the
            # exception, which carries the secret — would taint the log message.
            print(
                f"Error: COMBINATION_{position} contains a character outside Latin-1; "
                f"only GPG keys support the full UTF-8 range.",
                file=sys.stderr,
            )
            sys.exit(1)
        encrypted = aes_encrypt_cbc(secret_bytes, aes_key)
        # Add stage-1 encryption flag (0x01) at the beginning
        pwd_with_flag = [0x01] + encrypted
        pwd_arr = ", ".join(f"0x{b:02X}" for b in pwd_with_flag)
        password_defs.append(f"const byte PROGMEM pwd_{idx}[] = {{{pwd_arr}}};")

        plaintext_len = len(secret_bytes)
        encrypted_len = len(pwd_with_flag)
        max_plaintext_len = max(max_plaintext_len, plaintext_len)
        max_encrypted_len = max(max_encrypted_len, encrypted_len)

        seq_refs.append(
            "  { "
            f".sequence = seq_{idx}, "
            f".sequence_len = {len(comb['sequence'])}, "
            f".password = pwd_{idx}, "
            f".password_len = {encrypted_len}, "
            f".plaintext_len = {plaintext_len}, "
            f".content_type = 0x{comb['content_type']:02X} "
            "},"
        )
    
    key_bytes = ', '.join(f'0x{int(aes_key[i:i+2], 16):02X}' for i in range(0, len(aes_key), 2))
    
    header = f"""// Auto-generiert von generate_password_header.py
// Secrets are encrypted (flag 0x01) with AES Master Key using CBC mode
// First 16 bytes after flag are the IV, followed by ciphertext
// ESP decrypts entries directly from PROGMEM before typing them over USB
#ifndef EMBEDDED_PASSWORDS_H
#define EMBEDDED_PASSWORDS_H
#include <Arduino.h>

#define ENCRYPTION_STAGE_1 0x01  // Encrypted with master key using AES-CBC
#define CONTENT_TYPE_TEXT 0x00
#define CONTENT_TYPE_GPG_PRIVATE_KEY 0x01
#define MAX_PLAINTEXT_LENGTH {max_plaintext_len}
#define MAX_ENCRYPTED_PASSWORD_LENGTH {max_encrypted_len}

const byte PROGMEM AES_MASTER_KEY[] = {{ {key_bytes} }};

struct PasswordEntry {{
  const byte* sequence;
  int sequence_len;
  const byte* password;
  int password_len;
  int plaintext_len;
  uint8_t content_type;
}};

{chr(10).join(sequence_defs)}

{chr(10).join(password_defs)}

const PasswordEntry PASSWORD_ENTRIES[] PROGMEM = {{
{chr(10).join(seq_refs)}
}};

#define PASSWORD_ENTRY_COUNT {len(combinations)}
#endif
"""
    
    # The header embeds the AES master key, so create it with 0600 from the start
    # (create-with-mode avoids the race window of a chmod-after-write).
    fd = os.open(output_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'w') as f:
        # Passwords are AES-CBC encrypted before storage - not clear text
        f.write(header)  # lgtm[py/clear-text-storage-sensitive-data]
    os.chmod(output_path, 0o600)  # tighten perms even if the file pre-existed
    print(f"Header created: {output_path} ({len(combinations)} combinations)")

def main():
    script_dir = Path(__file__).parent
    env_vars = load_env(script_dir / '.env')
    
    aes_key = env_vars.get('AES_MASTER_KEY', '').replace(' ', '').replace('-', '').upper()
    if not re.fullmatch(r'[0-9A-F]{32}', aes_key):
        print("Error: AES_MASTER_KEY must be 32 hexadecimal characters (AES-128)", file=sys.stderr)
        sys.exit(1)
    
    combinations = parse_combinations(env_vars)
    generate_header(combinations, aes_key, script_dir / 'embedded_passwords.h')

if __name__ == '__main__':
    main()
