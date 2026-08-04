// splitmix.h - FNV-1a × 3 seed derivation + SplitMix64 stream cipher.
//
// Used by Phase 14 command dispatcher for per-call key derivation and body
// encryption. Matches spec §4.3 (seed) + §4.4 (stream) verbatim so the
// driver and launcher can agree on every byte without cross-reference.
//
// Symmetric: apply stream twice with the same starting seed -> original.
// Seed advances in-place as the stream progresses. Two-pass encrypt-then-
// decrypt requires caller to save and restore the starting seed.
//
// C-compatible single header so both kerneldriver (.c) and launcher tooling
// (.cpp) can include it. No stdint dependency (vcruntime warnings under WDK).

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// FNV-1a × 3 rounds over (static_key_16 || nonce_12), then SplitMix64 finalizer.
// Produces a 64-bit seed deterministic in both inputs.
static __inline unsigned long long splitmix_derive_seed(
    const unsigned char* static_key_16,
    const unsigned char* nonce_12)
{
    unsigned long long state = 0x6C62272E07BB0142ULL;  // FNV-1a offset basis (64-bit)
    const unsigned long long FNV_PRIME = 0x100000001B3ULL;
    int round;
    unsigned i;
    for (round = 0; round < 3; ++round) {
        for (i = 0; i < 16; ++i) {
            state = FNV_PRIME * (static_key_16[i] ^ state);
        }
        for (i = 0; i < 12; ++i) {
            state = FNV_PRIME * (nonce_12[i] ^ state);
        }
    }
    // SplitMix64 finalizer (standard constants).
    state ^= state >> 33;
    state = state * 0xFF51AFD7ED558CCDULL;
    state ^= state >> 33;
    state = state * 0xC4CEB9FE1A85EC53ULL;
    state ^= state >> 33;
    return state;
}

// SplitMix64 stream cipher. XOR keystream onto buf[0..len). Self-inverse.
// *seed is advanced in place — every 8 bytes it subtracts the golden-ratio
// increment and runs the SplitMix64 mixer to produce the next qword of
// keystream. Matches spec §4.4.
static __inline void splitmix_xor_stream(
    unsigned char* buf,
    size_t len,
    unsigned long long* seed)
{
    unsigned long long keystream = 0;
    unsigned long long s;
    size_t i;
    for (i = 0; i < len; ++i) {
        if ((i & 7) == 0) {
            *seed -= 0x61C8864680B583EBULL;  // golden-ratio-qword counter decrement
            s = *seed;
            s ^= s >> 30;
            s *= 0xBF58476D1CE4E5B9ULL;
            s ^= s >> 27;
            s *= 0x94D049BB133111EBULL;
            s ^= s >> 31;
            keystream = s;
        }
        buf[i] ^= (unsigned char)((keystream >> ((i & 7) * 8)) & 0xFF);
    }
}

#ifdef __cplusplus
}
#endif
