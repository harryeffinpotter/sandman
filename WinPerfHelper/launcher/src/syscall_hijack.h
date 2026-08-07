// syscall_hijack.h — ring-3 -> ring-0 execution primitive via NtAddAtom
// prologue hijack. Corresponds to analysis_notes.txt "NtAddAtom HIJACK-TARGET
// FACTS" and mapping_process_detailed.txt Phase 6 Step 4 / Phase 12.

#pragma once

#include <cstdint>
#include <windows.h>

namespace syscall_hijack {

struct Context {
    HANDLE   device;
    uint64_t cr3;
    uint64_t ntoskrnl_base_va;
    uint64_t nt_add_atom_kva;
    uint64_t ntdll_nt_add_atom_user;
};

bool init(HANDLE device, uint64_t cr3, Context& out);

uint64_t resolve_kernel_export(const Context& ctx, uint64_t mod_base_va, const char* name);

bool invoke(const Context& ctx, uint64_t target_kva,
            uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9,
            uint64_t& rax_out);

bool smoke_test(const Context& ctx, uint64_t kqpc_kva);

} // namespace syscall_hijack
