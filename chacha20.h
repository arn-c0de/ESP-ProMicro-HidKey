// ChaCha20 stream cipher implementation for ATmega32U4
// Based on RFC 7539 - Optimized for embedded systems
// Public domain / CC0

#ifndef CHACHA20_H
#define CHACHA20_H

#include <Arduino.h>

#define CHACHA20_KEY_SIZE 32
#define CHACHA20_NONCE_SIZE 12
#define CHACHA20_BLOCK_SIZE 64

// ChaCha20 context structure
struct ChaCha20_ctx {
  uint32_t state[16];
  uint8_t keystream[CHACHA20_BLOCK_SIZE];
  uint8_t position;
};

// Rotate left operation
#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

// Quarter round operation
#define QUARTERROUND(a, b, c, d) \
  a += b; d ^= a; d = ROTL32(d, 16); \
  c += d; b ^= c; b = ROTL32(b, 12); \
  a += b; d ^= a; d = ROTL32(d, 8); \
  c += d; b ^= c; b = ROTL32(b, 7);

// Read 32-bit little-endian value
static inline uint32_t load32_le(const uint8_t* src) {
  return (uint32_t)src[0] |
         ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) |
         ((uint32_t)src[3] << 24);
}

// Write 32-bit little-endian value
static inline void store32_le(uint8_t* dst, uint32_t value) {
  dst[0] = (uint8_t)value;
  dst[1] = (uint8_t)(value >> 8);
  dst[2] = (uint8_t)(value >> 16);
  dst[3] = (uint8_t)(value >> 24);
}

// ChaCha20 block function
static void chacha20_block(uint32_t out[16], const uint32_t in[16]) {
  uint32_t x[16];
  
  // Copy input state
  for (int i = 0; i < 16; i++) {
    x[i] = in[i];
  }
  
  // 20 rounds (10 column + 10 diagonal rounds)
  for (int i = 0; i < 10; i++) {
    // Column rounds
    QUARTERROUND(x[0], x[4], x[8], x[12])
    QUARTERROUND(x[1], x[5], x[9], x[13])
    QUARTERROUND(x[2], x[6], x[10], x[14])
    QUARTERROUND(x[3], x[7], x[11], x[15])
    
    // Diagonal rounds
    QUARTERROUND(x[0], x[5], x[10], x[15])
    QUARTERROUND(x[1], x[6], x[11], x[12])
    QUARTERROUND(x[2], x[7], x[8], x[13])
    QUARTERROUND(x[3], x[4], x[9], x[14])
  }
  
  // Add original state
  for (int i = 0; i < 16; i++) {
    out[i] = x[i] + in[i];
  }
}

// Initialize ChaCha20 context
// key: 32-byte key
// nonce: 12-byte nonce
// counter: initial block counter (usually 0 or 1)
static void ChaCha20_init(ChaCha20_ctx* ctx, const uint8_t* key, const uint8_t* nonce, uint32_t counter) {
  // ChaCha20 constants "expand 32-byte k"
  ctx->state[0] = 0x61707865;
  ctx->state[1] = 0x3320646e;
  ctx->state[2] = 0x79622d32;
  ctx->state[3] = 0x6b206574;
  
  // 256-bit key
  ctx->state[4] = load32_le(key + 0);
  ctx->state[5] = load32_le(key + 4);
  ctx->state[6] = load32_le(key + 8);
  ctx->state[7] = load32_le(key + 12);
  ctx->state[8] = load32_le(key + 16);
  ctx->state[9] = load32_le(key + 20);
  ctx->state[10] = load32_le(key + 24);
  ctx->state[11] = load32_le(key + 28);
  
  // Block counter (32-bit)
  ctx->state[12] = counter;
  
  // 96-bit nonce
  ctx->state[13] = load32_le(nonce + 0);
  ctx->state[14] = load32_le(nonce + 4);
  ctx->state[15] = load32_le(nonce + 8);
  
  ctx->position = CHACHA20_BLOCK_SIZE;
}

// Generate keystream block
static void ChaCha20_generate_block(ChaCha20_ctx* ctx) {
  uint32_t output[16];
  
  // Generate keystream block
  chacha20_block(output, ctx->state);
  
  // Convert to bytes
  for (int i = 0; i < 16; i++) {
    store32_le(ctx->keystream + (i * 4), output[i]);
  }
  
  // Increment block counter
  ctx->state[12]++;
  
  ctx->position = 0;
}

// Encrypt/decrypt data (same operation for stream cipher)
// plaintext and ciphertext can point to same buffer
static void ChaCha20_xor(ChaCha20_ctx* ctx, uint8_t* output, const uint8_t* input, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (ctx->position >= CHACHA20_BLOCK_SIZE) {
      ChaCha20_generate_block(ctx);
    }
    output[i] = input[i] ^ ctx->keystream[ctx->position];
    ctx->position++;
  }
}

// Simplified encrypt function (in-place)
static void ChaCha20_encrypt(uint8_t* data, size_t length, const uint8_t* key, const uint8_t* nonce, uint32_t counter = 0) {
  ChaCha20_ctx ctx;
  ChaCha20_init(&ctx, key, nonce, counter);
  ChaCha20_xor(&ctx, data, data, length);
  
  // Clear context
  memset(&ctx, 0, sizeof(ctx));
}

// Decrypt is same as encrypt for stream cipher
static void ChaCha20_decrypt(uint8_t* data, size_t length, const uint8_t* key, const uint8_t* nonce, uint32_t counter = 0) {
  ChaCha20_encrypt(data, length, key, nonce, counter);
}

#endif // CHACHA20_H
