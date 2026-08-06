#pragma once
#include <cstdint>
#include "../common/common_defs.h"

namespace cmdchannel {

// Resolves NtConvertBetweenAuxiliaryCounterAndPerformanceCounter from ntdll.
bool init();

// Send a raw, already-built command payload. Returns NTSTATUS from kernel.
// Caller builds + encrypts the payload; we just call the syscall.
int32_t send_raw(const void* buf, uint32_t size);

// Convenience: heartbeat. Kernel writes 1 byte to target_addr.
bool heartbeat(uint64_t target_addr);

// Cross-process read: driver reads `size` bytes from src in process `pid`
// and writes them to dst in our process. size ≤ 1 MB.
bool read_memory(uint32_t pid, uint64_t src, uint64_t dst, uint64_t size);
int32_t read_memory_rc(uint32_t pid, uint64_t src, uint64_t dst, uint64_t size);

// Cross-process write: driver writes `size` bytes from src in our process
// to dst in process `pid`. Direction reversed from read. size ≤ 1 MB.
bool write_memory(uint32_t pid, uint64_t dst, uint64_t src, uint64_t size);

// Find module base + size in target process's PEB.Ldr by wide name.
// out_size may be nullptr to skip size readback. Returns true on STATUS_SUCCESS
// AND out_base populated.
bool find_module(uint32_t pid, const wchar_t* name, uint64_t* out_base, uint32_t* out_size);

// Diagnostic variant: returns raw NTSTATUS. On non-success, *out_size holds
// walk_count (Ldr entries visited by the driver walk).
int32_t find_module_ex(uint32_t pid, const wchar_t* name,
                       uint64_t* out_base, uint32_t* out_size);

// Allocate size bytes in target process. hint=0 for any address.
// alloc_type: MEM_COMMIT|MEM_RESERVE = 0x3000. protect: PAGE_READWRITE=0x04,
// PAGE_EXECUTE_READWRITE=0x40. out_base receives the allocated base.
bool alloc_memory(uint32_t pid, uint64_t hint, uint64_t size,
                  uint32_t alloc_type, uint32_t protect, uint64_t* out_base);

// Free previously-allocated region. size=0 required when free_type=MEM_RELEASE(0x8000).
bool free_memory(uint32_t pid, uint64_t addr, uint64_t size, uint32_t free_type);

// Change page protection in target. Returns old protect via out_old (nullable).
bool protect_memory(uint32_t pid, uint64_t addr, uint64_t size,
                    uint32_t new_protect, uint32_t* out_old);

// Query VAD for `addr` in target. out_mbi receives 48-byte MEMORY_BASIC_INFORMATION.
bool query_memory(uint32_t pid, uint64_t addr, void* out_mbi /* size 48 */);

// PTE-direct protection flip. Walks target's page tables physically, modifies
// NX/W bits in leaf PTE. Bypasses Mm — VAD bookkeeping unchanged. CPU sees the
// new protection; user-mode AC walking VADs sees the original VAD shape.
//   flag PTE_FLAG_CLEAR_NX (0x1) -> make pages executable (clear NX bit)
//   flag PTE_FLAG_SET_W    (0x2) -> make pages writable    (set RW bit)
//   flag PTE_FLAG_CLEAR_W  (0x4) -> make pages read-only   (clear RW bit)
//   flag PTE_FLAG_SET_NX   (0x8) -> make pages non-exec    (set NX bit)
constexpr uint32_t PTE_FLAG_CLEAR_NX = 0x1u;
constexpr uint32_t PTE_FLAG_SET_W    = 0x2u;
constexpr uint32_t PTE_FLAG_CLEAR_W  = 0x4u;
constexpr uint32_t PTE_FLAG_SET_NX   = 0x8u;
bool set_pte_nx(uint32_t pid, uint64_t va, uint64_t size, uint32_t flags);

bool arm_image_notify(const wchar_t* exe_name);
bool query_armed_pid(uint32_t* out_pid);
bool create_remote_thread(uint32_t pid, uint64_t start_address, uint64_t parameter, uint32_t* out_tid);
bool unhook_hal();

} // namespace cmdchannel
