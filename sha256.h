// Minimal SHA-256 implementation for ATmega32U4
// Optimized for code size
// Based on FIPS 180-4

#ifndef SHA256_H
#define SHA256_H

#include <Arduino.h>

#define SHA256_BLOCK_SIZE 64
#define SHA256_HASH_SIZE 32

// SHA-256 context
struct SHA256_ctx {
  uint32_t state[8];
  uint8_t buffer[SHA256_BLOCK_SIZE];
  uint32_t count[2];
};

// SHA-256 constants (first 32 bits of fractional parts of cube roots of first 64 primes)
static const uint32_t K[64] PROGMEM = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

// Load 32-bit big-endian
static inline uint32_t load32_be(const uint8_t* src) {
  return ((uint32_t)src[0] << 24) |
         ((uint32_t)src[1] << 16) |
         ((uint32_t)src[2] << 8) |
         (uint32_t)src[3];
}

// Store 32-bit big-endian
static inline void store32_be(uint8_t* dst, uint32_t value) {
  dst[0] = (uint8_t)(value >> 24);
  dst[1] = (uint8_t)(value >> 16);
  dst[2] = (uint8_t)(value >> 8);
  dst[3] = (uint8_t)value;
}

// SHA-256 transform
static void sha256_transform(SHA256_ctx* ctx, const uint8_t* data) {
  uint32_t W[64];
  uint32_t a, b, c, d, e, f, g, h, t1, t2;
  
  // Prepare message schedule
  for (int i = 0; i < 16; i++) {
    W[i] = load32_be(data + (i * 4));
  }
  
  for (int i = 16; i < 64; i++) {
    W[i] = SIG1(W[i - 2]) + W[i - 7] + SIG0(W[i - 15]) + W[i - 16];
  }
  
  // Initialize working variables
  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];
  
  // Main loop
  for (int i = 0; i < 64; i++) {
    t1 = h + EP1(e) + CH(e, f, g) + pgm_read_dword(&K[i]) + W[i];
    t2 = EP0(a) + MAJ(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  
  // Update state
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

// Initialize SHA-256 context
static void SHA256_init(SHA256_ctx* ctx) {
  // Initial hash values (first 32 bits of fractional parts of square roots of first 8 primes)
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
  
  ctx->count[0] = 0;
  ctx->count[1] = 0;
}

// Update SHA-256 with new data
static void SHA256_update(SHA256_ctx* ctx, const uint8_t* data, size_t len) {
  uint32_t bufferIndex = (ctx->count[0] >> 3) & 0x3F;
  uint32_t bits = (uint32_t)len << 3;
  
  ctx->count[0] += bits;
  if (ctx->count[0] < bits) {
    ctx->count[1]++;
  }
  
  size_t spaceInBuffer = SHA256_BLOCK_SIZE - bufferIndex;
  size_t offset = 0;
  
  if (len >= spaceInBuffer) {
    memcpy(ctx->buffer + bufferIndex, data, spaceInBuffer);
    sha256_transform(ctx, ctx->buffer);
    
    offset = spaceInBuffer;
    while (offset + SHA256_BLOCK_SIZE <= len) {
      sha256_transform(ctx, data + offset);
      offset += SHA256_BLOCK_SIZE;
    }
    
    bufferIndex = 0;
  }
  
  memcpy(ctx->buffer + bufferIndex, data + offset, len - offset);
}

// Finalize SHA-256 and get hash
static void SHA256_final(SHA256_ctx* ctx, uint8_t* hash) {
  uint32_t bufferIndex = (ctx->count[0] >> 3) & 0x3F;
  uint8_t pad = (bufferIndex < 56) ? (56 - bufferIndex) : (120 - bufferIndex);
  
  uint8_t padding[64];
  padding[0] = 0x80;
  memset(padding + 1, 0, pad - 1);
  
  SHA256_update(ctx, padding, pad);
  
  // Append length in bits (big-endian)
  uint8_t lengthBytes[8];
  store32_be(lengthBytes, ctx->count[1]);
  store32_be(lengthBytes + 4, ctx->count[0]);
  SHA256_update(ctx, lengthBytes, 8);
  
  // Output hash
  for (int i = 0; i < 8; i++) {
    store32_be(hash + (i * 4), ctx->state[i]);
  }
}

// Simplified one-shot hash function
static void SHA256_hash(uint8_t* output, const uint8_t* input, size_t length) {
  SHA256_ctx ctx;
  SHA256_init(&ctx);
  SHA256_update(&ctx, input, length);
  SHA256_final(&ctx, output);
  
  // Clear context
  memset(&ctx, 0, sizeof(ctx));
}

// HMAC-SHA256 implementation
static void HMAC_SHA256(uint8_t* output, const uint8_t* key, size_t keyLen, const uint8_t* message, size_t msgLen) {
  uint8_t k[SHA256_BLOCK_SIZE];
  uint8_t ipad[SHA256_BLOCK_SIZE];
  uint8_t opad[SHA256_BLOCK_SIZE];
  uint8_t innerHash[SHA256_HASH_SIZE];
  
  // Prepare key
  memset(k, 0, SHA256_BLOCK_SIZE);
  if (keyLen > SHA256_BLOCK_SIZE) {
    SHA256_hash(k, key, keyLen);
  } else {
    memcpy(k, key, keyLen);
  }
  
  // Create padded keys
  for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }
  
  // Inner hash: H((K XOR ipad) || message)
  SHA256_ctx ctx;
  SHA256_init(&ctx);
  SHA256_update(&ctx, ipad, SHA256_BLOCK_SIZE);
  SHA256_update(&ctx, message, msgLen);
  SHA256_final(&ctx, innerHash);
  
  // Outer hash: H((K XOR opad) || innerHash)
  SHA256_init(&ctx);
  SHA256_update(&ctx, opad, SHA256_BLOCK_SIZE);
  SHA256_update(&ctx, innerHash, SHA256_HASH_SIZE);
  SHA256_final(&ctx, output);
  
  // Clear sensitive data
  memset(k, 0, sizeof(k));
  memset(ipad, 0, sizeof(ipad));
  memset(opad, 0, sizeof(opad));
  memset(innerHash, 0, sizeof(innerHash));
  memset(&ctx, 0, sizeof(ctx));
}

// HKDF-Expand (RFC 5869) - simplified version
// Derives key material from input key material
static void HKDF_SHA256(uint8_t* output, size_t outLen, const uint8_t* ikm, size_t ikmLen, const uint8_t* info, size_t infoLen) {
  // For simplicity, we skip the Extract phase and use IKM directly as PRK
  // In production, you should do: PRK = HMAC-SHA256(salt, IKM)
  
  uint8_t prk[SHA256_HASH_SIZE];
  uint8_t salt = 0; // Empty salt
  HMAC_SHA256(prk, &salt, 0, ikm, ikmLen);
  
  // Expand phase
  uint8_t T[SHA256_HASH_SIZE];
  memset(T, 0, SHA256_HASH_SIZE);
  
  size_t offset = 0;
  uint8_t counter = 1;
  
  while (offset < outLen) {
    // T(i) = HMAC-SHA256(PRK, T(i-1) || info || counter)
    SHA256_ctx ctx;
    SHA256_init(&ctx);
    
    if (counter > 1) {
      SHA256_update(&ctx, T, SHA256_HASH_SIZE);
    }
    if (info && infoLen > 0) {
      SHA256_update(&ctx, info, infoLen);
    }
    SHA256_update(&ctx, &counter, 1);
    
    uint8_t temp[SHA256_HASH_SIZE + SHA256_BLOCK_SIZE];
    size_t tempLen = 0;
    if (counter > 1) {
      memcpy(temp, T, SHA256_HASH_SIZE);
      tempLen = SHA256_HASH_SIZE;
    }
    if (info && infoLen > 0) {
      memcpy(temp + tempLen, info, infoLen);
      tempLen += infoLen;
    }
    temp[tempLen++] = counter;
    
    HMAC_SHA256(T, prk, SHA256_HASH_SIZE, temp, tempLen);
    
    size_t toCopy = (outLen - offset < SHA256_HASH_SIZE) ? (outLen - offset) : SHA256_HASH_SIZE;
    memcpy(output + offset, T, toCopy);
    offset += toCopy;
    counter++;
  }
  
  // Clear sensitive data
  memset(prk, 0, sizeof(prk));
  memset(T, 0, sizeof(T));
}

#endif // SHA256_H
