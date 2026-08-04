// crypto.h — launcher-side crypto primitives.
//
// rolling_xor: stream cipher used to encrypt embedded PE blobs. Self-inverse.
// Algorithm matches sub_7FFAB78C9A50 in the original launcher, also mirrored
// in tools/encrypt_pe for symmetry at build time.

#pragma once

#include <cstdint>
#include <cstddef>

namespace crypto {

void rolling_xor(uint8_t* buf, size_t len);

} // namespace crypto
