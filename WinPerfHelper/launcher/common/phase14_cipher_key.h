// phase14_cipher_key.h - shared 16-byte static key for command-channel cipher.
//
// Both driver and launcher include this header so they agree on the key used
// for FNV-1a seed derivation (spec §4.3). Our own bytes, not sample's —
// byte-identity to sample is not a project goal per CLAUDE.md.
//
// Not encrypted or obfuscated in either binary. This is acceptable because:
//   (a) our launcher is already disk-visible (Amcache, ShimCache, BAM)
//   (b) the driver is fileless and never appears on disk as a PE
//   (c) recovering the key from launcher memory or driver kernel memory
//       requires a capability that already defeats us on other axes
// Sample does not obfuscate the static key either (spec §4.3: first 16 bytes
// of .rdata at byte_140005028, plain).

#pragma once

// 16 random bytes. Regenerate if ever suspected compromised; both driver
// and launcher must rebuild together if this changes.
#define PHASE14_CIPHER_KEY_BYTES \
    0x7E, 0xBB, 0x41, 0x09, 0xA5, 0x5D, 0xCE, 0xF3, \
    0x12, 0x88, 0x66, 0x2B, 0xD1, 0x7F, 0x0C, 0x9A

#define PHASE14_CIPHER_KEY_LEN 16
