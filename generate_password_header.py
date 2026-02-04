#!/usr/bin/env python3
import os, sys
from pathlib import Path
from Cryptodome.Cipher import AES
from Cryptodome.Util.Padding import pad

MAX_SEQUENCE_LENGTH = 20

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
                env_vars[key.strip()] = value.strip().strip('"').strip("'")
    return env_vars

def parse_combinations(env_vars):
    count = int(env_vars.get('COMBINATION_COUNT', '0'))
    if count <= 0:
        print("Error: COMBINATION_COUNT must be > 0!", file=sys.stderr)
        sys.exit(1)
    combinations = []
    for i in range(count):
        seq_key = f'COMBINATION_{i}_SEQUENCE'
        pwd_key = f'COMBINATION_{i}_PASSWORD'
        if seq_key not in env_vars or pwd_key not in env_vars:
            print(f"Error: Missing {seq_key} or {pwd_key}!", file=sys.stderr)
            sys.exit(1)
        sequence = [int(x.strip()) for x in env_vars[seq_key].split(',')]
        combinations.append({'index': i, 'sequence': sequence, 'password': env_vars[pwd_key]})
    return combinations

def aes_encrypt(password, key_hex):
    key = bytes.fromhex(key_hex)
    plaintext = password.encode('utf-8')
    padded = pad(plaintext, AES.block_size)
    cipher = AES.new(key, AES.MODE_ECB)
    return list(cipher.encrypt(padded))

def generate_header(combinations, aes_key, output_path):
    sequence_defs = []
    password_defs = []
    seq_refs = []
    
    for comb in combinations:
        idx = comb['index']
        seq_arr = ", ".join(str(s) for s in comb['sequence'])
        sequence_defs.append(f"const byte PROGMEM seq_{idx}[] = {{{seq_arr}}};")
        
        encrypted = aes_encrypt(comb['password'], aes_key)
        # Add stage-1 encryption flag (0x01) at the beginning
        pwd_with_flag = [0x01] + encrypted
        pwd_arr = ", ".join(f"0x{b:02X}" for b in pwd_with_flag)
        password_defs.append(f"const byte PROGMEM pwd_{idx}[] = {{{pwd_arr}}};")
        
        comma = "," if idx < len(combinations) - 1 else ""
        seq_refs.append(f"  {{ .sequence = seq_{idx}, .sequence_len = {len(comb['sequence'])}, .password = pwd_{idx}, .password_len = {len(pwd_with_flag)}, .plaintext_len = {len(comb['password'])} }}{comma}")
    
    key_bytes = ', '.join(f'0x{int(aes_key[i:i+2], 16):02X}' for i in range(0, len(aes_key), 2))
    
    header = f"""// Auto-generiert von generate_password_header.py
// Passwords are STAGE-1 encrypted (flag 0x01) with AES Master Key
// ESP will decrypt and re-encrypt with device-specific key on first boot
#ifndef EMBEDDED_PASSWORDS_H
#define EMBEDDED_PASSWORDS_H
#include <Arduino.h>

#define ENCRYPTION_STAGE_1 0x01  // Encrypted with master key only
#define ENCRYPTION_STAGE_2 0x02  // Re-encrypted with device-specific key

const byte PROGMEM AES_MASTER_KEY[] = {{ {key_bytes} }};

struct PasswordEntry {{
  const byte* sequence;
  int sequence_len;
  const byte* password;
  int password_len;
  int plaintext_len;
}};

{chr(10).join(sequence_defs)}

{chr(10).join(password_defs)}

const PasswordEntry PASSWORD_ENTRIES[] PROGMEM = {{
{chr(10).join(seq_refs)}
}};

#define PASSWORD_ENTRY_COUNT {len(combinations)}
#endif
"""
    
    with open(output_path, 'w') as f:
        f.write(header)
    print(f"Header created: {output_path} ({len(combinations)} combinations)")

def main():
    script_dir = Path(__file__).parent
    env_vars = load_env(script_dir / '.env')
    
    aes_key = env_vars.get('AES_MASTER_KEY', '').replace(' ', '').replace('-', '').upper()
    if len(aes_key) != 32:
        print(f"Error: AES_MASTER_KEY must be 32 hex chars", file=sys.stderr)
        sys.exit(1)
    
    combinations = parse_combinations(env_vars)
    generate_header(combinations, aes_key, script_dir / 'embedded_passwords.h')

if __name__ == '__main__':
    main()
