#!/usr/bin/env python3
"""
Generiert embedded_password.h aus .env Datei
Liest mehrere Kombinationen aus .env und erstellt C-Header mit Structs
Format:
  COMBINATION_COUNT=3
  COMBINATION_0_SEQUENCE="0,0,1,0"
  COMBINATION_0_PASSWORD="passwort1"
  etc.
"""

import os
import sys
from pathlib import Path

def load_env(env_path):
    """Liest .env Datei und gibt Dictionary zurück"""
    env_vars = {}
    if not env_path.exists():
        print(f"Fehler: {env_path} nicht gefunden!", file=sys.stderr)
        sys.exit(1)
    
    with open(env_path, 'r') as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#') and '=' in line:
                key, value = line.split('=', 1)
                # Entferne Anführungszeichen
                value = value.strip().strip('"').strip("'")
                env_vars[key.strip()] = value
    
    return env_vars

def parse_combinations(env_vars):
    """Extrahiert Kombinationen aus env_vars"""
    try:
        count = int(env_vars.get('COMBINATION_COUNT', '0'))
    except ValueError:
        print("Fehler: COMBINATION_COUNT ist keine Zahl!", file=sys.stderr)
        sys.exit(1)
    
    if count <= 0:
        print("Fehler: COMBINATION_COUNT muss > 0 sein!", file=sys.stderr)
        sys.exit(1)
    
    combinations = []
    for i in range(count):
        seq_key = f'COMBINATION_{i}_SEQUENCE'
        pwd_key = f'COMBINATION_{i}_PASSWORD'
        
        if seq_key not in env_vars:
            print(f"Fehler: {seq_key} nicht gefunden!", file=sys.stderr)
            sys.exit(1)
        if pwd_key not in env_vars:
            print(f"Fehler: {pwd_key} nicht gefunden!", file=sys.stderr)
            sys.exit(1)
        
        seq_str = env_vars[seq_key]
        password = env_vars[pwd_key]
        
        # Parse Sequenz (komma-getrennte 0er und 1er)
        try:
            sequence = [int(x.strip()) for x in seq_str.split(',')]
            if not all(s in [0, 1] for s in sequence):
                raise ValueError("Sequenz darf nur 0 und 1 enthalten")
            if len(sequence) == 0:
                raise ValueError("Sequenz darf nicht leer sein")
        except ValueError as e:
            print(f"Fehler in {seq_key}: {e}", file=sys.stderr)
            sys.exit(1)
        
        combinations.append({
            'index': i,
            'sequence': sequence,
            'password': password
        })
    
    return combinations

def generate_c_sequence_array(sequence, index):
    """Generiert C-Array für eine Sequenz"""
    seq_array = ", ".join(str(s) for s in sequence)
    return f"  {{{seq_array}}}"

def generate_header(combinations, output_path):
    """Generiert embedded_password.h mit Structs und Arrays"""
    
    # Sequenz-Arrays generieren
    sequence_definitions = []
    sequence_references = []
    
    for comb in combinations:
        seq_array = generate_c_sequence_array(comb['sequence'], comb['index'])
        sequence_definitions.append(f"const byte PROGMEM seq_{comb['index']}[] = {seq_array};")
        trailing_comma = "," if comb['index'] < len(combinations) - 1 else ""
        # Escape characters that would break C string literals
        pwd_escaped = comb['password'].replace('\\', '\\\\').replace('"', '\\"')
        sequence_references.append(f"  {{ .sequence = seq_{comb['index']}, .sequence_len = {len(comb['sequence'])}, .password = \"{pwd_escaped}\" }}{trailing_comma}")
    
    # Header Content
    header_content = f'''// Auto-generiert von generate_password_header.py
// NICHT MANUELL BEARBEITEN - wird bei jedem Build überschrieben
// Datei: embedded_password.h

#ifndef EMBEDDED_PASSWORD_H
#define EMBEDDED_PASSWORD_H

#include <Arduino.h>

// Struktur für eine Passwort-Kombination
struct PasswordEntry {{
  const byte* sequence;        // Pointer auf Sequenz-Array (im PROGMEM)
  int sequence_len;            // Länge der Sequenz
  const char* password;        // Passwort-String
}};

// Sequenz-Arrays (im Flash-Speicher)
{chr(10).join(sequence_definitions)}

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
    
    print(f"Header-Datei erstellt: {output_path}")
    print(f"Kombinationen generiert: {len(combinations)}")
    for comb in combinations:
        seq_str = ", ".join(str(s) for s in comb['sequence'])
        print(f"  [{comb['index']}] Sequenz: {seq_str} -> '{comb['password']}'")

def main():
    script_dir = Path(__file__).parent
    env_path = script_dir / '.env'
    output_main = script_dir / 'embedded_passwords.h'
    output_compat = script_dir / 'embedded_password.h'
    
    # Lade .env
    env_vars = load_env(env_path)
    
    # Extrahiere Kombinationen
    combinations = parse_combinations(env_vars)
    
    # Generiere Haupt-Header (Plural)
    generate_header(combinations, output_main)

    # Erstelle kompatible Einzelfilename-Kopie (falls nötig)
    with open(output_main, 'r') as src, open(output_compat, 'w') as dst:
        dst.write(src.read())
    print(f"Kompatible Header-Datei erstellt: {output_compat}")

if __name__ == '__main__':
    main()
