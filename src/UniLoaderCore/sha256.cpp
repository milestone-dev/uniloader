// SHA-256, streamed.
//
// Its one job is checking a download against the hash a release states, and the
// download is hundreds of megabytes, so it hashes as the bytes arrive rather
// than over a buffer somebody had to keep. FIPS 180-4; the constants are the
// first thirty-two bits of the fractional parts of the cube roots of the first
// sixty-four primes, and the initial state the same of the square roots of the
// first eight.
//
// Written out rather than taken from a dependency for the same reason the rest
// of the core is: this is one file, it has an answer that is either right or
// wrong, and the test vectors in test_hash prove which.

#include "uniloader/uniloader.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t RotateRight(uint32_t value, int bits) {
  return (value >> bits) | (value << (32 - bits));
}

}  // namespace

struct ul_sha256 {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint64_t bits = 0;
  unsigned char block[64] = {};
  size_t pending = 0;

  void Compress(const unsigned char* chunk) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) |
             (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
             static_cast<uint32_t>(chunk[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 =
          RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 =
          RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
      const uint32_t choose = (e & f) ^ (~e & g);
      const uint32_t temp1 = h + s1 + choose + kRoundConstants[i] + w[i];
      const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + majority;
      h = g; g = f; f = e;
      e = d + temp1;
      d = c; c = b; b = a;
      a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
  }
};

extern "C" {

ul_sha256* ul_sha256_create(void) { return new (std::nothrow) ul_sha256(); }

void ul_sha256_update(ul_sha256* h, const void* data, size_t length) {
  if (!h || !data) return;
  const unsigned char* bytes = static_cast<const unsigned char*>(data);
  h->bits += static_cast<uint64_t>(length) * 8;
  while (length > 0) {
    const size_t room = 64 - h->pending;
    const size_t take = length < room ? length : room;
    std::memcpy(h->block + h->pending, bytes, take);
    h->pending += take;
    bytes += take;
    length -= take;
    if (h->pending == 64) {
      h->Compress(h->block);
      h->pending = 0;
    }
  }
}

void ul_sha256_finish(ul_sha256* h, char* out) {
  if (!h) {
    if (out) out[0] = '\0';
    return;
  }
  const uint64_t bits = h->bits;
  unsigned char padding = 0x80;
  ul_sha256_update(h, &padding, 1);
  h->bits = bits;                       // padding is not message length
  padding = 0;
  while (h->pending != 56) {
    ul_sha256_update(h, &padding, 1);
    h->bits = bits;
  }
  unsigned char tail[8];
  for (int i = 0; i < 8; ++i) {
    tail[i] = static_cast<unsigned char>((bits >> (56 - i * 8)) & 0xFF);
  }
  std::memcpy(h->block + 56, tail, 8);
  h->Compress(h->block);

  if (out) {
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 8; ++i) {
      for (int b = 0; b < 4; ++b) {
        const unsigned char byte =
            static_cast<unsigned char>((h->state[i] >> (24 - b * 8)) & 0xFF);
        out[i * 8 + b * 2] = kHex[byte >> 4];
        out[i * 8 + b * 2 + 1] = kHex[byte & 0xF];
      }
    }
    out[64] = '\0';
  }
  delete h;
}

}  // extern "C"
