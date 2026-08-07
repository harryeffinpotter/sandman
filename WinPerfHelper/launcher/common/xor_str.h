// xor_str.h — compile-time XOR-encrypted string literal.
// Usage: const char* s = XSTR("ntoskrnl.exe");
// At runtime, the string is decoded on first use into a local buffer.
// Prevents the literal string from appearing in the binary.

#pragma once

#include <stdint.h>
#include <string.h>

namespace xstr_impl {

// 16-byte XOR pad matching the one used by the original launcher.
// Keeping same key = same family, but it lives inside our encrypt/decrypt
// routine either way. Changing the constants has zero detection impact.
constexpr uint8_t PAD[16] = {
    0xBA, 0xAE, 0xED, 0x57, 0x80, 0x0B, 0x98, 0xE8,
    0x45, 0xC0, 0xD3, 0x51, 0x08, 0x2F, 0x25, 0x1F
};

template <size_t N>
struct Encrypted {
    char data[N];
    constexpr Encrypted(const char (&s)[N]) : data{} {
        for (size_t i = 0; i < N; ++i) {
            data[i] = s[i] ^ PAD[i % 16];
        }
    }
};

template <size_t N>
__declspec(noinline) const char* decode(const Encrypted<N>& e) {
    static thread_local char buf[N];
    for (size_t i = 0; i < N; ++i) {
        buf[i] = e.data[i] ^ PAD[i % 16];
    }
    return buf;
}

} // namespace xstr_impl

#define XSTR(s) ([]() -> const char* { \
    static constexpr xstr_impl::Encrypted e(s); \
    return xstr_impl::decode(e); \
}())
