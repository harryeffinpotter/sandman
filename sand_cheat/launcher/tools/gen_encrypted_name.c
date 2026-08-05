#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void lcg_xor_apply(unsigned char* buf, size_t len, uint32_t key_const) {
    uint32_t state = 0;
    for (size_t i = 0; i < len; ++i) {
        uint32_t prev = state;
        state = state - 0x61C88647u;
        uint32_t mix = 0x41C64E6Du * (prev ^ key_const) + 0x3039u;
        mix ^= mix >> 16;
        unsigned char keybyte = (unsigned char)((59u * mix) ^ ((0x045D9F3Bu * mix) >> 16));
        buf[i] ^= keybyte;
    }
}

static uint32_t fnv1a(const char* str) {
    uint32_t h = 0x811C9DC5u;
    for (const char* p = str; *p; ++p) {
        h ^= (unsigned char)*p;
        h *= 0x01000193u;
    }
    h ^= 0;
    h *= 0x01000193u;
    return h;
}

static void gen(const char* name, int index) {
    size_t len = strlen(name) + 1;
    unsigned char buf[128];
    memcpy(buf, name, len);
    uint32_t key = fnv1a(name);
    lcg_xor_apply(buf, len, key);

    printf("\n// [%d] \"%s\" (len %zu incl. null), key 0x%08X\n", index, name, len, key);
    printf("static const unsigned char enc_name_%s[%zu] = {\n", name, len);
    for (size_t i = 0; i < len; i += 8) {
        printf("    ");
        for (size_t j = i; j < len && j < i + 8; ++j) {
            printf("0x%02X, ", buf[j]);
        }
        printf("\n");
    }
    printf("};\n");
    printf("#define KEY_enc_name_%s 0x%08Xu\n", name, key);
    printf("#define LEN_enc_name_%s %zuu\n", name, len);
}

int main(void) {
    gen("PsSetLoadImageNotifyRoutine", 20);
    gen("PsRemoveLoadImageNotifyRoutine", 21);
    gen("RtlCreateUserThread", 22);
    return 0;
}
