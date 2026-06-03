# Code Review & Security Audit — ESP-ProMicro-HidKey

**Date:** 2026-06-03 · **Branch:** `2.0.2-refactor`
**Scope:** firmware (`.ino`), custom crypto headers, Python/Bash tooling.
**Method:** multi-worker analysis (security · refactoring · tooling), then every
recommendation cross-checked against 2026 best-practice sources (cited inline).

> **No real secrets are committed.** `.env`, `embedded_passwords.h`, and
> `dump/flash.bin` are untracked and absent from git history; `.gitignore` is
> correct. The only key in VCS is the placeholder in `.env.example`.

---

## Legend

- **Verdict** = result of the 2026 best-practice verification pass.
- Status: ☐ todo · ☑ done · ◧ architectural (deferred, needs design decision).

---

## A. Security findings

### S1 — Master key stored in plaintext next to ciphertext ◧
`embedded_passwords.h` (`AES_MASTER_KEY` in PROGMEM). Anyone with the firmware
image recovers all secrets (a worker decrypted `pwd_0` using only the header).
**Verdict:** CONFIRMED, but *largely inherent* to a self-decrypting keyboard with
no user secret. OWASP: keys must be stored separately from data.
**Action:** honest threat-model docs in README; AVR lock fuses as a *speed-bump
only* (glitch/fault-injection bypassable — Synacktiv, arXiv 1903.08102). Real
fix = derive key from a runtime secret (see S1b). **Architectural.**

### S1b — Key derivation can't rescue a tiny PIN ◧
**Verdict:** CONFIRMED (decisive). A ≤32-combo button PIN ≈ 5 bits; a KDF
stretches work-per-guess, not entropy. Argon2id/scrypt are infeasible on 2.5 KB
RAM; only weak PBKDF2 runs. Meaningful resistance needs a long passphrase or a
secure element this board lacks. *Document the real limit.* **Architectural.**

### S2 — No MAC / authentication on ciphertext ◧
Raw AES-CBC is malleable. **Verdict:** CONFIRMED — AEAD is the 2026 default;
**encrypt-then-MAC** is the correct ordering (Bellare–Namprempre; OWASP Crypto
Storage). **Refinement:** prefer **ChaCha20-Poly1305 / XChaCha20-Poly1305** over
AES-GCM on this AES-less AVR (constant-time in SW; GCM brittle under nonce
reuse). For a *static write-once flash blob* (one key, one message) nonce reuse
is a non-issue. Lib: **rweather/Crypto** (AVR-optimized) or **Monocypher**.
**Architectural.**

### S3 — `memset` secret-wipes are optimized away ☐
`.ino` `executePassword` (~306-308) and `decryptEntryFromProgmem` (~131-167).
**Verdict:** CONFIRMED (CWE-14; USENIX Sec'17). Multi-pass on SRAM is pointless.
**Refinement:** the **`"memory"` clobber is the load-bearing part**, not the
`volatile` pointer (CERT C MSC06-C). avr-libc has no `explicit_bzero`/`memset_s`,
so roll our own. Canonical form:
```c
static void secureWipe(void* p, size_t n) {
  memset(p, 0, n);
  asm volatile("" : : "r"(p) : "memory");
}
```

### S4 — Brute-force lockout bypassable by power-cycle ◧
`.ino` lockout state is RAM-only; the 30 s timer restarts each boot and the
counter auto-zeroes after timeout; EEPROM written every failed attempt (wear).
**Action:** persist a monotonic lockout deadline; wear-level. Underlying ≤32-combo
space is the real limit (see S1b). **Architectural.**

### S5 — `\r` escape silently corrupted into `\n` ☐
`generate_password_header.py:~49`. A literal `\r` in a text secret becomes a
newline → typed secret differs, undetectable until auth fails.
**Verdict:** CONFIRMED data-corruption bug (CWE-838). Map `\\r` → `'\r'`.

### S6 — Key-bearing files written world-readable ☐
Generated header, auto-created/rotated `.env`, decoded dumps — all default umask.
**Verdict:** CONFIRMED. **Refinement:** *create-with-mode beats chmod-after*
(create→chmod is a race window; CWE-276). Shell: `umask 077` / `install -m 600`.
Python: `os.open(path, O_WRONLY|O_CREAT|O_TRUNC, 0o600)`.

### S7 — `rotate_aes_master_key.sh` unsafe ☐
Non-atomic in-place write (crash ⇒ lost `.env`), no backup, echoes old key to
stdout, and does **not** regenerate the header (old secrets stay decryptable).
**Verdict:** CONFIRMED. Temp-file + atomic `os.replace` (same FS); temp itself
0600; drop the key echo (log-leak anti-pattern, OWASP Secrets Mgmt); a `.bak` of
key material is itself a leak risk (OWASP WSTG-CONF-04) — skip or shred. Trigger
a header rebuild.

### S8 — Dead hand-rolled crypto shipped ☐
`chacha20.h` + `sha256.h` are never `#include`d. **Verdict:** CONFIRMED liability
("don't roll your own", OWASP). Delete them.

---

## B. Refactoring / LOC reduction (firmware, behavior-preserving)

| # | Change | LOC | Status |
|---|--------|-----|--------|
| R1 | Collapse 4× `memset` cleanup in `decryptEntryFromProgmem` into one `fail:` path (combine with S3 `secureWipe`) | ~16-18 | ☐ |
| R2 | Merge `blinkSuccess/Fail/Lockout/Ready` into one `blink(times,onMs,offMs)` | ~20-25 | ☐ |
| R3 | Delete `TIMEOUT_MS` alias; fix stale "3 seconds" comments (actual 2000 ms) | ~2 | ☐ |
| R4 | `resetFailedAttempts()` helper; delete redundant double-write at `.ino:~403-404` | ~3-5 | ☐ |
| R5 | `resetSequence()` helper for duplicated index-reset + memset | ~2-3 | ☐ |
| R6 | Name magic numbers (136, 17, 0x80); reuse `aes.h` `AES_BLOCKLEN`/`AES_KEYLEN` | ~0 (clarity) | ☐ |

**Keep as-is:** DE-layout `switch` (table-driven is *not* fewer lines, loses
comments); `sequenceMatches` accumulate loop. **Note:** constant-time holds only
at source level — compilers can reintroduce a branch (ASIA CCS'25); soften the
code comment's claim. USB-pacing `delay()`s untouched.

**Estimated total: ~45-53 lines off a 443-line `.ino` (~10-12%).**

---

## C. Tooling robustness

### H3 — `build.sh` auto-`.env` path is broken ☐
When it creates a template `.env` it then compiles *without* generating the
header → confusing failure / stale build. Should `exit 0` after creating the
template; run header-gen unconditionally otherwise.

### M2 — ~25 dead lines in `build.sh` ☐
Every `if [ $? -ne 0 ]` is unreachable under `set -e` — incl. the "Upload
failed…" hint (never prints). **Verdict:** CONFIRMED (BashFAQ/105). Use
`if cmd; then…else…fi`.

### Other tooling ☐
- M1: non-latin-1 text secret → uncaught `UnicodeEncodeError`; catch + clear msg.
  *Refinement:* UTF-8 is the better default unless 1-byte mapping is required
  (firmware currently needs verbatim bytes → keep latin-1 but guard the encode).
- H2: AES key not validated as hex → opaque `ValueError`; pin charset+length.
- M6: `SEQUENCE_TIMEOUT_MS` injected into a C macro unvalidated; require integer.
- M5: hand-rolled `.env` parser misses `export ` / inline comments → prefer
  `python-dotenv` (v1.2.2, 2026-03; handles export/comments/escapes).
- `set -euo pipefail` recommended *with* caveats (`-u` empty-array, SIGPIPE-141).
- `dump_flash.sh`: add strict mode; replace `ls|head` with a glob into an array
  (Wooledge ParsingLs).

---

## Implementation plan

**Batch 1 — verified low-risk — ✅ DONE (this branch):**
S3, S5, S6, S7, S8, R1, R2, R3, R4, R5, R6, H2, H3, M1, M2, M6.

Verification performed:
- Firmware: `g++ -std=gnu++11 -fsyntax-only -Wall -Wextra` against the real
  `aes.h` + `embedded_passwords.h` (stubbed Arduino/Keyboard/EEPROM) — **passed,
  no warnings**, incl. the `goto fail` scoping (g++ is stricter than avr-gcc).
  *Full on-device compile pending `arduino-cli`, which is not installed here.*
- `generate_password_header.py`: round-trip encrypt→decrypt verified; header
  written 0600; bad-hex-key and non-Latin-1 error paths verified.
- `build.sh`: `bash -n` clean; no dead `$?` checks remain.
- `rotate_aes_master_key.sh`: round-trip rotates key, writes `.env`+header 0600,
  regenerates header, leaves no temp files, never prints the key.

**Batch 2 — architectural (separate discussion, NOT done):** S1, S1b, S2, S4,
M5 (adopt python-dotenv). These change the security model / add dependencies.

> ⚠️ The remaining S7 caveat: rotation regenerates the header but the **old key
> still exists in any prior flash dump / `dump/flash.bin`** — true key rotation
> also requires re-flashing the device (the script now reminds you to).
