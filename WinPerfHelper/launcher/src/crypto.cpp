#include "crypto.h"

#include "../common/common_defs.h"  // ROLLING_XOR_KEY

namespace crypto {

void rolling_xor(uint8_t* buf, size_t len) {
    // Matches tools/encrypt_pe/src/main.cpp. Stream cipher is self-inverse.
    uint32_t v7 = 16, v9 = 329, v10 = 16;
    for (size_t i = 0; i < len; ++i) {
        v7 = (v10 * (ROLLING_XOR_KEY[i % 3] + v9 + 8) + (v7 >> 10)) & 0xFFFFFFFFu;
        v10 = v7 & 0xFF;
        v9  = v7 & 0xFF;
        buf[i] ^= static_cast<uint8_t>(v7 & 0xFF);
    }
}

} // namespace crypto
