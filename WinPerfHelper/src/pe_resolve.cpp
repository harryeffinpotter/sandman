#include "pe_resolve.h"
#include <cstring>

namespace pe_resolve {

typedef struct _UNICODE_STRING_LITE {
    USHORT Length; USHORT MaximumLength; PWSTR Buffer;
} UNICODE_STRING_LITE;

typedef struct _PEB_LDR_DATA_LITE {
    ULONG Length; UCHAR Initialized; PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
} PEB_LDR_DATA_LITE;

typedef struct _PEB_LITE {
    UCHAR Reserved1[2]; UCHAR BeingDebugged; UCHAR Reserved2[1];
    PVOID Reserved3[2];
    PEB_LDR_DATA_LITE* Ldr;
} PEB_LITE;

typedef struct _LDR_DATA_TABLE_ENTRY_LITE {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING_LITE FullDllName;
    UNICODE_STRING_LITE BaseDllName;
} LDR_DATA_TABLE_ENTRY_LITE;

static const char* g_last_tier = nullptr;

static inline char to_lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c | 0x20) : c;
}

static bool wide_eq_ascii_ci(PWSTR wide, USHORT wideLenBytes, const char* ascii) {
    if (!wide || !ascii) return false;
    USHORT wchars = (USHORT)(wideLenBytes / sizeof(wchar_t));
    USHORT i = 0;
    for (; i < wchars && ascii[i]; ++i) {
        wchar_t wc = wide[i];
        if (wc > 0x7f) return false;
        char a = (char)wc;
        if (to_lower_ascii(a) != to_lower_ascii(ascii[i])) return false;
    }
    return i == wchars && ascii[i] == 0;
}

static HMODULE try_peb(const char* name) {
    __try {
        PEB_LITE* peb = (PEB_LITE*)__readgsqword(0x60);
        if (!peb || !peb->Ldr) return NULL;
        LIST_ENTRY* head = &peb->Ldr->InLoadOrderModuleList;
        for (LIST_ENTRY* cur = head->Flink; cur && cur != head; cur = cur->Flink) {
            LDR_DATA_TABLE_ENTRY_LITE* e =
                CONTAINING_RECORD(cur, LDR_DATA_TABLE_ENTRY_LITE, InLoadOrderLinks);
            if (wide_eq_ascii_ci(e->BaseDllName.Buffer, e->BaseDllName.Length, name)) {
                return (HMODULE)e->DllBase;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
    return NULL;
}

HMODULE find_module(const char* name) {
    if (!name) { g_last_tier = "miss"; return NULL; }
    HMODULE m = try_peb(name);
    if (m) { g_last_tier = "peb"; return m; }
    g_last_tier = "miss";
    return NULL;
}

FARPROC get_proc(HMODULE mod, const char* name) {
    if (!mod || !name) return NULL;
    __try {
        BYTE* base = (BYTE*)mod;
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
        IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (dd.VirtualAddress == 0 || dd.Size == 0) return NULL;
        DWORD expRva = dd.VirtualAddress;
        DWORD expSize = dd.Size;
        IMAGE_EXPORT_DIRECTORY* exp = (IMAGE_EXPORT_DIRECTORY*)(base + expRva);
        DWORD* names = (DWORD*)(base + exp->AddressOfNames);
        WORD*  ordinals = (WORD*) (base + exp->AddressOfNameOrdinals);
        DWORD* funcs = (DWORD*)(base + exp->AddressOfFunctions);
        for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
            const char* nm = (const char*)(base + names[i]);
            const char* a = nm; const char* b = name;
            while (*a && *b && *a == *b) { ++a; ++b; }
            if (*a == 0 && *b == 0) {
                WORD ord = ordinals[i];
                DWORD rva = funcs[ord];
                if (rva >= expRva && rva < expRva + expSize) {
                    return NULL;
                }
                return (FARPROC)(base + rva);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
    return NULL;
}

const char* last_tier() { return g_last_tier; }

} // namespace pe_resolve
