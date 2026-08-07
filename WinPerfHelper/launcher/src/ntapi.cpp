#include "ntapi.h"

namespace ntapi {

PFN_NtLoadDriver              NtLoadDriver              = nullptr;
PFN_NtUnloadDriver            NtUnloadDriver            = nullptr;
PFN_RtlAdjustPrivilege        RtlAdjustPrivilege        = nullptr;
PFN_RtlInitUnicodeString      RtlInitUnicodeString      = nullptr;
PFN_NtQuerySystemInformation  NtQuerySystemInformation  = nullptr;

bool init() {
    HMODULE h = GetModuleHandleA("ntdll.dll");
    if (!h) return false;

#define RESOLVE(name) \
    name = reinterpret_cast<PFN_##name>(GetProcAddress(h, #name)); \
    if (!name) return false

    RESOLVE(NtLoadDriver);
    RESOLVE(NtUnloadDriver);
    RESOLVE(RtlAdjustPrivilege);
    RESOLVE(RtlInitUnicodeString);
    RESOLVE(NtQuerySystemInformation);

#undef RESOLVE
    return true;
}

} // namespace ntapi
