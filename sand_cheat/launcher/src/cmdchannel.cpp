// cmdchannel - user-mode → kernel command channel via the HAL-hooked
// NtConvertBetweenAuxiliaryCounterAndPerformanceCounter syscall.
//
// Phase 14.4: builds + encrypts command buffers matching spec §5.1,
// invokes the syscall, returns driver's NTSTATUS. Driver-side dispatcher
// (driver.c our_hal_dispatcher) decodes and routes to command handlers.

#include "cmdchannel.h"

#include <windows.h>
#include <cstring>
#include <cstdlib>

#include "../common/splitmix.h"
#include "../common/phase14_cipher_key.h"

namespace cmdchannel {

namespace {

// NtConvertBetweenAuxiliaryCounterAndPerformanceCounter signature.
typedef LONG (NTAPI *fnNtConv)(BOOLEAN, PULONG64, PULONG64, PBOOLEAN);

fnNtConv g_pNtConv = nullptr;

// Shared static key (same 16 bytes the driver has baked into its .rdata).
const unsigned char g_cipher_key[16] = { PHASE14_CIPHER_KEY_BYTES };

void fill_nonce_random(unsigned char out[12]) {
    // rand() suffices for our nonce — no cryptographic secrecy required; the
    // nonce's only job is to diversify per-call seeds so the same ciphertext
    // never recurs for the same plaintext+key.
    for (int i = 0; i < 12; ++i) {
        out[i] = static_cast<unsigned char>(std::rand() & 0xFF);
    }
}

} // namespace

bool init() {
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    if (!h) return false;
    g_pNtConv = reinterpret_cast<fnNtConv>(GetProcAddress(
        h, "NtConvertBetweenAuxiliaryCounterAndPerformanceCounter"));
    return g_pNtConv != nullptr;
}

// Build + encrypt + send a command body. Returns NTSTATUS from driver.
// body_plain points at the 8-byte dispatcher header (magic+reserved+cmd_id)
// followed by command-specific args. body_size = total body length.
int32_t send_raw(const void* body_plain, uint32_t body_size) {
    if (!g_pNtConv || !body_plain || body_size == 0 || body_size > 0x7FF) {
        return -1;  // STATUS_UNSUCCESSFUL-ish sentinel
    }

    // Build full packet: 4B size + 12B nonce + N encrypted bytes.
    unsigned char packet[4 + 12 + 0x800];
    *reinterpret_cast<uint32_t*>(packet) = body_size;
    fill_nonce_random(packet + 4);
    std::memcpy(packet + 16, body_plain, body_size);

    unsigned long long seed = splitmix_derive_seed(g_cipher_key, packet + 4);
    splitmix_xor_stream(packet + 16, body_size, &seed);

    // Smuggle the packet address via the PerformanceCounterValue slot.
    // Kernel's NtConvert syscall dereferences *slot to get the value, passes
    // as rcx to the hooked dispatcher. Dispatcher reads from that VA.
    ULONG64 smuggle = reinterpret_cast<ULONG64>(packet);
    ULONG64 out_val = 0;
    ULONG64 disc_pad = 0;
    PBOOLEAN disc = reinterpret_cast<PBOOLEAN>(&disc_pad);

    // direction=FALSE -> Perf→Aux → HAL slot B60 → our dispatcher.
    LONG rc = g_pNtConv(FALSE, &smuggle, &out_val, disc);
    return rc;
}

bool read_memory(uint32_t pid, uint64_t src, uint64_t dst, uint64_t size) {
    // Spec §5.3 Cmd 0 body (48 bytes):
    //   +0x00 magic (u16=0x7C4A)
    //   +0x02 reserved (u16)
    //   +0x04 cmd_id  (u32=0)
    //   +0x08 pid     (u32)
    //   +0x0C pad
    //   +0x10 src     (u64)
    //   +0x18 dst     (u64)
    //   +0x20 size    (u64)
    //   +0x28 pad (u64)
    unsigned char body[0x30] = {0};
    *reinterpret_cast<uint16_t*>(body + 0x00) = 0x7C4A;
    *reinterpret_cast<uint32_t*>(body + 0x04) = 0;
    *reinterpret_cast<uint32_t*>(body + 0x08) = pid;
    *reinterpret_cast<uint64_t*>(body + 0x10) = src;
    *reinterpret_cast<uint64_t*>(body + 0x18) = dst;
    *reinterpret_cast<uint64_t*>(body + 0x20) = size;
    int32_t rc = send_raw(body, sizeof(body));
    return rc == 0;
}

bool write_memory(uint32_t pid, uint64_t dst, uint64_t src, uint64_t size) {
    // Spec §5.3 Cmd 1 body (48 bytes, same layout as READ but semantics flipped):
    //   +0x00 magic / reserved / cmd_id=1
    //   +0x08 pid
    //   +0x10 dst (target-side)
    //   +0x18 src (launcher-side)
    //   +0x20 size
    unsigned char body[0x30] = {0};
    *reinterpret_cast<uint16_t*>(body + 0x00) = 0x7C4A;
    *reinterpret_cast<uint32_t*>(body + 0x04) = 1;
    *reinterpret_cast<uint32_t*>(body + 0x08) = pid;
    *reinterpret_cast<uint64_t*>(body + 0x10) = dst;
    *reinterpret_cast<uint64_t*>(body + 0x18) = src;
    *reinterpret_cast<uint64_t*>(body + 0x20) = size;
    int32_t rc = send_raw(body, sizeof(body));
    return rc == 0;
}

bool find_module(uint32_t pid, const wchar_t* name, uint64_t* out_base, uint32_t* out_size) {
    if (!name || !out_base) return false;
    size_t len = 0;
    while (name[len]) ++len;
    if (len == 0 || len > 127) return false;

    // Spec §5.3 Cmd 2 body (56 bytes min):
    //   +0x08 pid       (u32)
    //   +0x0C name_len  (u16, wchars excl. null)
    //   +0x10 name_ptr  (u64, launcher VA of wide name)
    //   +0x18 out_base  (u64, launcher VA u64 slot)
    //   +0x20 out_size  (u64, launcher VA u32 slot, 0 to skip)
    unsigned char body[0x38] = {0};
    *reinterpret_cast<uint16_t*>(body + 0x00) = 0x7C4A;
    *reinterpret_cast<uint32_t*>(body + 0x04) = 2;
    *reinterpret_cast<uint32_t*>(body + 0x08) = pid;
    *reinterpret_cast<uint16_t*>(body + 0x0C) = static_cast<uint16_t>(len);
    *reinterpret_cast<uint64_t*>(body + 0x10) = reinterpret_cast<uint64_t>(name);
    *reinterpret_cast<uint64_t*>(body + 0x18) = reinterpret_cast<uint64_t>(out_base);
    *reinterpret_cast<uint64_t*>(body + 0x20) = out_size ? reinterpret_cast<uint64_t>(out_size) : 0;

    *out_base = 0;
    if (out_size) *out_size = 0;

    int32_t rc = send_raw(body, sizeof(body));
    return rc == 0 && *out_base != 0;
}

bool alloc_memory(uint32_t pid, uint64_t hint, uint64_t size,
                  uint32_t alloc_type, uint32_t protect, uint64_t* out_base) {
    if (!out_base) return false;
    unsigned char body[0x30] = {0};
    *reinterpret_cast<uint16_t*>(body + 0x00) = 0x7C4A;
    *reinterpret_cast<uint32_t*>(body + 0x04) = 3;
    *reinterpret_cast<uint32_t*>(body + 0x08) = pid;
    *reinterpret_cast<uint64_t*>(body + 0x10) = hint;
    *reinterpret_cast<uint64_t*>(body + 0x18) = size;
    *reinterpret_cast<uint32_t*>(body + 0x20) = alloc_type;
    *reinterpret_cast<uint32_t*>(body + 0x24) = protect;
    *reinterpret_cast<uint64_t*>(body + 0x28) = reinterpret_cast<uint64_t>(out_base);
    *out_base = 0;
    int32_t rc = send_raw(body, sizeof(body));
    return rc == 0 && *out_base != 0;
}

bool free_memory(uint32_t pid, uint64_t addr, uint64_t size, uint32_t free_type) {
    unsigned char body[0x28] = {0};
    *reinterpret_cast<uint16_t*>(body + 0x00) = 0x7C4A;
    *reinterpret_cast<uint32_t*>(body + 0x04) = 4;
    *reinterpret_cast<uint32_t*>(body + 0x08) = pid;
    *reinterpret_cast<uint64_t*>(body + 0x10) = addr;
    *reinterpret_cast<uint64_t*>(body + 0x18) = size;
    *reinterpret_cast<uint32_t*>(body + 0x20) = free_type;
    int32_t rc = send_raw(body, sizeof(body));
    return rc == 0;
}

bool protect_memory(uint32_t pid, uint64_t addr, uint64_t size,
                    uint32_t new_protect, uint32_t* out_old) {
    unsigned char body[0x30] = {0};
    *reinterpret_cast<uint16_t*>(body + 0x00) = 0x7C4A;
    *reinterpret_cast<uint32_t*>(body + 0x04) = 5;
    *reinterpret_cast<uint32_t*>(body + 0x08) = pid;
    *reinterpret_cast<uint64_t*>(body + 0x10) = addr;
    *reinterpret_cast<uint64_t*>(body + 0x18) = size;
    *reinterpret_cast<uint32_t*>(body + 0x20) = new_protect;
    *reinterpret_cast<uint64_t*>(body + 0x28) = out_old ? reinterpret_cast<uint64_t>(out_old) : 0;
    if (out_old) *out_old = 0;
    int32_t rc = send_raw(body, sizeof(body));
    return rc == 0;
}

bool query_memory(uint32_t pid, uint64_t addr, void* out_mbi) {
    if (!out_mbi) return false;
    unsigned char body[0x20] = {0};
    *reinterpret_cast<uint16_t*>(body + 0x00) = 0x7C4A;
    *reinterpret_cast<uint32_t*>(body + 0x04) = 6;
    *reinterpret_cast<uint32_t*>(body + 0x08) = pid;
    *reinterpret_cast<uint64_t*>(body + 0x10) = addr;
    *reinterpret_cast<uint64_t*>(body + 0x18) = reinterpret_cast<uint64_t>(out_mbi);
    int32_t rc = send_raw(body, sizeof(body));
    return rc == 0;
}

bool set_pte_nx(uint32_t pid, uint64_t va, uint64_t size, uint32_t flags) {
    // Cmd 8 body (40 bytes):
    //   +0x00 magic / reserved / cmd_id=8
    //   +0x08 pid     (u32)
    //   +0x0C flags   (u32) — PTE_FLAG_*
    //   +0x10 va      (u64)
    //   +0x18 size    (u64)
    //   +0x20 pad     (u64)
    unsigned char body[0x28] = {0};
    *reinterpret_cast<uint16_t*>(body + 0x00) = 0x7C4A;
    *reinterpret_cast<uint32_t*>(body + 0x04) = 8;
    *reinterpret_cast<uint32_t*>(body + 0x08) = pid;
    *reinterpret_cast<uint32_t*>(body + 0x0C) = flags;
    *reinterpret_cast<uint64_t*>(body + 0x10) = va;
    *reinterpret_cast<uint64_t*>(body + 0x18) = size;
    int32_t rc = send_raw(body, sizeof(body));
    return rc == 0;
}

bool heartbeat(uint64_t target_addr) {
    // Spec §5.3 Cmd 7 body (24 bytes):
    //   +0x00 magic (u16=0x7C4A)
    //   +0x02 reserved (u16)
    //   +0x04 cmd_id  (u32=7)
    //   +0x08 out_byte_ptr (u64)
    //   +0x10 padding (8 bytes)
    unsigned char body[0x18] = {0};
    *reinterpret_cast<uint16_t*>(body + 0x00) = 0x7C4A;
    *reinterpret_cast<uint16_t*>(body + 0x02) = 0;
    *reinterpret_cast<uint32_t*>(body + 0x04) = 7;
    *reinterpret_cast<uint64_t*>(body + 0x08) = target_addr;
    int32_t rc = send_raw(body, sizeof(body));
    return rc == 0;  // STATUS_SUCCESS
}

} // namespace cmdchannel
