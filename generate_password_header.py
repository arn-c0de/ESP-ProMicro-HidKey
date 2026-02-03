#!/usr/bin/env python3
"""
Generates embedded_password.h from .env file
Reads multiple combinations from .env and creates C header with structs
Format:
  COMBINATION_COUNT=3
  COMBINATION_0_SEQUENCE="0,0,1,0"
  COMBINATION_0_PASSWORD="password1"
  etc.
"""

import os
import sys
from pathlib import Path

MAX_SEQUENCE_LENGTH = 20

def load_env(env_path):
    """Reads .env file and returns dictionary"""
    env_vars = {}
    if not env_path.exists():
        print(f"Error: {env_path} not found!", file=sys.stderr)
        sys.exit(1)
    
    with open(env_path, 'r') as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#') and '=' in line:
                key, value = line.split('=', 1)
                # Remove quotation marks
                value = value.strip().strip('"').strip("'")
                env_vars[key.strip()] = value
    
    return env_vars

def parse_combinations(env_vars):
    """Extracts combinations from env_vars"""
    try:
        count = int(env_vars.get('COMBINATION_COUNT', '0'))
    except ValueError:
        print("Error: COMBINATION_COUNT is not a number!", file=sys.stderr)
        sys.exit(1)
    
    if count <= 0:
        print("Error: COMBINATION_COUNT must be > 0!", file=sys.stderr)
        sys.exit(1)
    
    combinations = []
    for i in range(count):
        seq_key = f'COMBINATION_{i}_SEQUENCE'
        pwd_key = f'COMBINATION_{i}_PASSWORD'
        
        if seq_key not in env_vars:
            print(f"Error: {seq_key} not found!", file=sys.stderr)
            sys.exit(1)
        if pwd_key not in env_vars:
            print(f"Error: {pwd_key} not found!", file=sys.stderr)
            sys.exit(1)
        
        seq_str = env_vars[seq_key]
        password = env_vars[pwd_key]
        
        # Parse sequence (comma-separated 0s and 1s)
        try:
            sequence = [int(x.strip()) for x in seq_str.split(',')]
            if not all(s in [0, 1] for s in sequence):
                raise ValueError("Sequence may only contain 0 and 1")
            if len(sequence) == 0:
                raise ValueError("Sequence cannot be empty")
            if len(sequence) > MAX_SEQUENCE_LENGTH:
                raise ValueError(f"Sequence too long (max {MAX_SEQUENCE_LENGTH}, got {len(sequence)})")
        except ValueError as e:
            print(f"Error in {seq_key}: {e}", file=sys.stderr)
            sys.exit(1)
        
        combinations.append({
            'index': i,
            'sequence': sequence,
            'password': password
        })
    
    return combinations

def generate_c_sequence_array(sequence, index):
    """Generates C array for a sequence"""
    seq_array = ", ".join(str(s) for s in sequence)
    return f"  {{{seq_array}}}"

def xor_encode(password, key=0x5A):
    """XOR obfuscation for password"""
    return [ord(c) ^ key for c in password]

def generate_header(combinations, output_path):
    """Generates embedded_passwords.h with XOR-obfuscated passwords"""

    xor_key = 0x5A  # XOR key for obfuscation

    # Generate sequence arrays and password arrays
    sequence_definitions = []
    password_definitions = []
    sequence_references = []

    for comb in combinations:
        seq_array = generate_c_sequence_array(comb['sequence'], comb['index'])
        sequence_definitions.append(f"const byte PROGMEM seq_{comb['index']}[] = {seq_array};")

        # XOR-encoded Passwort
        encoded = xor_encode(comb['password'], xor_key)
        pwd_array = ", ".join(f"0x{b:02X}" for b in encoded)
        password_definitions.append(f"const byte PROGMEM pwd_{comb['index']}[] = {{{pwd_array}}};")

        trailing_comma = "," if comb['index'] < len(combinations) - 1 else ""
        sequence_references.append(f"  {{ .sequence = seq_{comb['index']}, .sequence_len = {len(comb['sequence'])}, .password = pwd_{comb['index']}, .password_len = {len(comb['password'])} }}{trailing_comma}")
    
    # Header Content
    header_content = f'''// Auto-generiert von generate_password_header.py
// NICHT MANUELL BEARBEITEN - wird bei jedem Build überschrieben

#ifndef EMBEDDED_PASSWORDS_H
#define EMBEDDED_PASSWORDS_H

#include <Arduino.h>

// XOR-Key für Passwort-Deobfuskierung
#define XOR_KEY 0x{xor_key:02X}

// Struktur für eine Passwort-Kombination
struct PasswordEntry {{
  const byte* sequence;        // Pointer auf Sequenz-Array (im PROGMEM)
  int sequence_len;            // Länge der Sequenz
  const byte* password;        // XOR-obfuskiertes Passwort (im PROGMEM)
  int password_len;            // Länge des Passworts
}};

// Sequenz-Arrays (im Flash-Speicher)
{chr(10).join(sequence_definitions)}

// Passwort-Arrays (XOR-obfuskiert im Flash-Speicher)
{chr(10).join(password_definitions)}

// Kombinationen Array
const PasswordEntry PASSWORD_ENTRIES[] PROGMEM = {{
{chr(10).join(sequence_references)}
}};

// Anzahl der Kombinationen
#define PASSWORD_ENTRY_COUNT {len(combinations)}

#endif
'''
    
    with open(output_path, 'w') as f:
        f.write(header_content)
    
    print(f"Header file created: {output_path}")
    print(f"Combinations generated: {len(combinations)}")
    for comb in combinations:
        seq_str = ", ".join(str(s) for s in comb['sequence'])
        print(f"  [{comb['index']}] Sequence: {seq_str} -> '{comb['password']}'")

def main():
    script_dir = Path(__file__).parent
    env_path = script_dir / '.env'
    output_path = script_dir / 'embedded_passwords.h'

    # Load .env
    env_vars = load_env(env_path)

    # Extract combinations
    combinations = parse_combinations(env_vars)

    # Generate header
    generate_header(combinations, output_path)

if __name__ == '__main__':
    main()
