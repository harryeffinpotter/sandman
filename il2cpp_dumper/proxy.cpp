// cl /EHsc /O2 /LD proxy.cpp /Fe:version.dll /link user32.lib
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Real version.dll handle + forwarded function pointers
// ---------------------------------------------------------------------------
static HMODULE g_realVersion = nullptr;

#define PROXY_FUNC(name) \
    static FARPROC pfn_##name = nullptr; \
    extern "C" __declspec(dllexport) void __stdcall name() { \
        if (!pfn_##name) pfn_##name = GetProcAddress(g_realVersion, #name); \
        if (pfn_##name) { \
            __asm { jmp [pfn_##name] } \
        } \
    }

// MSVC x64 doesn't support inline asm — use a different forwarding approach
#ifdef _M_X64

#define DECL_PROXY(name) static FARPROC pfn_##name = nullptr;
#define LOAD_PROXY(name) pfn_##name = GetProcAddress(g_realVersion, #name);

DECL_PROXY(GetFileVersionInfoA)
DECL_PROXY(GetFileVersionInfoByHandle)
DECL_PROXY(GetFileVersionInfoExA)
DECL_PROXY(GetFileVersionInfoExW)
DECL_PROXY(GetFileVersionInfoSizeA)
DECL_PROXY(GetFileVersionInfoSizeExA)
DECL_PROXY(GetFileVersionInfoSizeExW)
DECL_PROXY(GetFileVersionInfoSizeW)
DECL_PROXY(GetFileVersionInfoW)
DECL_PROXY(VerFindFileA)
DECL_PROXY(VerFindFileW)
DECL_PROXY(VerInstallFileA)
DECL_PROXY(VerInstallFileW)
DECL_PROXY(VerLanguageNameA)
DECL_PROXY(VerLanguageNameW)
DECL_PROXY(VerQueryValueA)
DECL_PROXY(VerQueryValueW)

// x64 forwarding via linker isn't possible without the real DLL at link time
// Use wrapper functions that call through the stored pointers
#define WRAP_PROXY(name) \
    extern "C" __declspec(dllexport) LPVOID __cdecl proxy_##name() { \
        return (LPVOID)pfn_##name; \
    }

// Variadic-arg forwarding stubs — these just redirect via pointer
// For version.dll, all functions use __stdcall with known signatures
extern "C" {
    __declspec(dllexport) BOOL __stdcall _GetFileVersionInfoA(LPCSTR f, DWORD h, DWORD l, LPVOID d) {
        typedef BOOL(__stdcall* fn)(LPCSTR, DWORD, DWORD, LPVOID);
        return pfn_GetFileVersionInfoA ? ((fn)pfn_GetFileVersionInfoA)(f, h, l, d) : FALSE;
    }
    __declspec(dllexport) BOOL __stdcall _GetFileVersionInfoW(LPCWSTR f, DWORD h, DWORD l, LPVOID d) {
        typedef BOOL(__stdcall* fn)(LPCWSTR, DWORD, DWORD, LPVOID);
        return pfn_GetFileVersionInfoW ? ((fn)pfn_GetFileVersionInfoW)(f, h, l, d) : FALSE;
    }
    __declspec(dllexport) DWORD __stdcall _GetFileVersionInfoSizeA(LPCSTR f, LPDWORD h) {
        typedef DWORD(__stdcall* fn)(LPCSTR, LPDWORD);
        return pfn_GetFileVersionInfoSizeA ? ((fn)pfn_GetFileVersionInfoSizeA)(f, h) : 0;
    }
    __declspec(dllexport) DWORD __stdcall _GetFileVersionInfoSizeW(LPCWSTR f, LPDWORD h) {
        typedef DWORD(__stdcall* fn)(LPCWSTR, LPDWORD);
        return pfn_GetFileVersionInfoSizeW ? ((fn)pfn_GetFileVersionInfoSizeW)(f, h) : 0;
    }
    __declspec(dllexport) BOOL __stdcall _GetFileVersionInfoExA(DWORD fl, LPCSTR f, DWORD h, DWORD l, LPVOID d) {
        typedef BOOL(__stdcall* fn)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
        return pfn_GetFileVersionInfoExA ? ((fn)pfn_GetFileVersionInfoExA)(fl, f, h, l, d) : FALSE;
    }
    __declspec(dllexport) BOOL __stdcall _GetFileVersionInfoExW(DWORD fl, LPCWSTR f, DWORD h, DWORD l, LPVOID d) {
        typedef BOOL(__stdcall* fn)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
        return pfn_GetFileVersionInfoExW ? ((fn)pfn_GetFileVersionInfoExW)(fl, f, h, l, d) : FALSE;
    }
    __declspec(dllexport) DWORD __stdcall _GetFileVersionInfoSizeExA(DWORD fl, LPCSTR f, LPDWORD h) {
        typedef DWORD(__stdcall* fn)(DWORD, LPCSTR, LPDWORD);
        return pfn_GetFileVersionInfoSizeExA ? ((fn)pfn_GetFileVersionInfoSizeExA)(fl, f, h) : 0;
    }
    __declspec(dllexport) DWORD __stdcall _GetFileVersionInfoSizeExW(DWORD fl, LPCWSTR f, LPDWORD h) {
        typedef DWORD(__stdcall* fn)(DWORD, LPCWSTR, LPDWORD);
        return pfn_GetFileVersionInfoSizeExW ? ((fn)pfn_GetFileVersionInfoSizeExW)(fl, f, h) : 0;
    }
    __declspec(dllexport) int __stdcall _GetFileVersionInfoByHandle(DWORD a, HANDLE b, LPVOID c, DWORD d) {
        typedef int(__stdcall* fn)(DWORD, HANDLE, LPVOID, DWORD);
        return pfn_GetFileVersionInfoByHandle ? ((fn)pfn_GetFileVersionInfoByHandle)(a, b, c, d) : 0;
    }
    __declspec(dllexport) DWORD __stdcall _VerFindFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPSTR e, PUINT f, LPSTR g, PUINT h) {
        typedef DWORD(__stdcall* fn)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
        return pfn_VerFindFileA ? ((fn)pfn_VerFindFileA)(a, b, c, d, e, f, g, h) : 0;
    }
    __declspec(dllexport) DWORD __stdcall _VerFindFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPWSTR e, PUINT f, LPWSTR g, PUINT h) {
        typedef DWORD(__stdcall* fn)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
        return pfn_VerFindFileW ? ((fn)pfn_VerFindFileW)(a, b, c, d, e, f, g, h) : 0;
    }
    __declspec(dllexport) DWORD __stdcall _VerInstallFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPCSTR e, LPCSTR f, LPSTR g, PUINT h) {
        typedef DWORD(__stdcall* fn)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
        return pfn_VerInstallFileA ? ((fn)pfn_VerInstallFileA)(a, b, c, d, e, f, g, h) : 0;
    }
    __declspec(dllexport) DWORD __stdcall _VerInstallFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPCWSTR e, LPCWSTR f, LPWSTR g, PUINT h) {
        typedef DWORD(__stdcall* fn)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
        return pfn_VerInstallFileW ? ((fn)pfn_VerInstallFileW)(a, b, c, d, e, f, g, h) : 0;
    }
    __declspec(dllexport) DWORD __stdcall _VerLanguageNameA(DWORD a, LPSTR b, DWORD c) {
        typedef DWORD(__stdcall* fn)(DWORD, LPSTR, DWORD);
        return pfn_VerLanguageNameA ? ((fn)pfn_VerLanguageNameA)(a, b, c) : 0;
    }
    __declspec(dllexport) DWORD __stdcall _VerLanguageNameW(DWORD a, LPWSTR b, DWORD c) {
        typedef DWORD(__stdcall* fn)(DWORD, LPWSTR, DWORD);
        return pfn_VerLanguageNameW ? ((fn)pfn_VerLanguageNameW)(a, b, c) : 0;
    }
    __declspec(dllexport) BOOL __stdcall _VerQueryValueA(LPCVOID a, LPCSTR b, LPVOID* c, PUINT d) {
        typedef BOOL(__stdcall* fn)(LPCVOID, LPCSTR, LPVOID*, PUINT);
        return pfn_VerQueryValueA ? ((fn)pfn_VerQueryValueA)(a, b, c, d) : FALSE;
    }
    __declspec(dllexport) BOOL __stdcall _VerQueryValueW(LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d) {
        typedef BOOL(__stdcall* fn)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
        return pfn_VerQueryValueW ? ((fn)pfn_VerQueryValueW)(a, b, c, d) : FALSE;
    }
}

#endif // _M_X64

// Linker aliases: export the real names pointing to our wrapper functions
#pragma comment(linker, "/export:GetFileVersionInfoA=_GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoW=_GetFileVersionInfoW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=_GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=_GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoExA=_GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoExW=_GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA=_GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=_GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle=_GetFileVersionInfoByHandle")
#pragma comment(linker, "/export:VerFindFileA=_VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW=_VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA=_VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW=_VerInstallFileW")
#pragma comment(linker, "/export:VerLanguageNameA=_VerLanguageNameA")
#pragma comment(linker, "/export:VerLanguageNameW=_VerLanguageNameW")
#pragma comment(linker, "/export:VerQueryValueA=_VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=_VerQueryValueW")

static void load_real_version_dll() {
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    strcat_s(sys, "\\version.dll");
    g_realVersion = LoadLibraryA(sys);
    if (!g_realVersion) return;

    LOAD_PROXY(GetFileVersionInfoA)
    LOAD_PROXY(GetFileVersionInfoByHandle)
    LOAD_PROXY(GetFileVersionInfoExA)
    LOAD_PROXY(GetFileVersionInfoExW)
    LOAD_PROXY(GetFileVersionInfoSizeA)
    LOAD_PROXY(GetFileVersionInfoSizeExA)
    LOAD_PROXY(GetFileVersionInfoSizeExW)
    LOAD_PROXY(GetFileVersionInfoSizeW)
    LOAD_PROXY(GetFileVersionInfoW)
    LOAD_PROXY(VerFindFileA)
    LOAD_PROXY(VerFindFileW)
    LOAD_PROXY(VerInstallFileA)
    LOAD_PROXY(VerInstallFileW)
    LOAD_PROXY(VerLanguageNameA)
    LOAD_PROXY(VerLanguageNameW)
    LOAD_PROXY(VerQueryValueA)
    LOAD_PROXY(VerQueryValueW)
}

// ===========================================================================
// IL2CPP DUMPER (embedded from dumper.cpp)
// ===========================================================================

#define CLR_WHITE       0x07
#define CLR_DARK_GRAY   0x08
#define CLR_BRIGHT_GREEN  0x0A
#define CLR_BRIGHT_CYAN   0x0B
#define CLR_RED           0x0C
#define CLR_BRIGHT_MAGENTA 0x0D
#define CLR_BRIGHT_YELLOW  0x0E
#define CLR_BRIGHT_WHITE   0x0F

static const char* safe_str(const char* s) { return s ? s : ""; }

typedef void*       (*fn_il2cpp_domain_get)();
typedef void**      (*fn_il2cpp_domain_get_assemblies)(void* domain, size_t* count);
typedef void*       (*fn_il2cpp_assembly_get_image)(void* assembly);
typedef size_t      (*fn_il2cpp_image_get_class_count)(void* image);
typedef void*       (*fn_il2cpp_image_get_class)(void* image, size_t index);
typedef const char* (*fn_il2cpp_image_get_name)(void* image);
typedef const char* (*fn_il2cpp_class_get_name)(void* klass);
typedef const char* (*fn_il2cpp_class_get_namespace)(void* klass);
typedef void*       (*fn_il2cpp_class_get_parent)(void* klass);
typedef void*       (*fn_il2cpp_class_get_type)(void* klass);
typedef uint32_t    (*fn_il2cpp_class_get_flags)(void* klass);
typedef void*       (*fn_il2cpp_class_get_interfaces)(void* klass, void** iter);
typedef void*       (*fn_il2cpp_class_get_nested_types)(void* klass, void** iter);
typedef bool        (*fn_il2cpp_class_is_enum)(void* klass);
typedef bool        (*fn_il2cpp_class_is_valuetype)(void* klass);
typedef uint32_t    (*fn_il2cpp_class_instance_size)(void* klass);
typedef int32_t     (*fn_il2cpp_class_value_size)(void* klass, uint32_t* align);
typedef uint16_t    (*fn_il2cpp_class_num_fields)(void* klass);
typedef void*       (*fn_il2cpp_class_get_element_class)(void* klass);
typedef bool        (*fn_il2cpp_class_is_generic)(void* klass);
typedef bool        (*fn_il2cpp_class_is_inflated)(void* klass);
typedef void*       (*fn_il2cpp_class_get_fields)(void* klass, void** iter);
typedef const char* (*fn_il2cpp_field_get_name)(void* field);
typedef size_t      (*fn_il2cpp_field_get_offset)(void* field);
typedef void*       (*fn_il2cpp_field_get_type)(void* field);
typedef uint32_t    (*fn_il2cpp_field_get_flags)(void* field);
typedef bool        (*fn_il2cpp_field_is_literal)(void* field);
typedef void*       (*fn_il2cpp_class_get_methods)(void* klass, void** iter);
typedef const char* (*fn_il2cpp_method_get_name)(void* method);
typedef uint32_t    (*fn_il2cpp_method_get_param_count)(void* method);
typedef void*       (*fn_il2cpp_method_get_param)(void* method, uint32_t index);
typedef void*       (*fn_il2cpp_method_get_return_type)(void* method);
typedef uint32_t    (*fn_il2cpp_method_get_flags)(void* method, uint32_t* impl_flags);
typedef uint32_t    (*fn_il2cpp_method_get_token)(void* method);
typedef bool        (*fn_il2cpp_method_is_generic)(void* method);
typedef bool        (*fn_il2cpp_method_is_inflated)(void* method);
typedef bool        (*fn_il2cpp_method_is_instance)(void* method);
typedef void*       (*fn_il2cpp_class_get_properties)(void* klass, void** iter);
typedef const char* (*fn_il2cpp_property_get_name)(void* prop);
typedef void*       (*fn_il2cpp_property_get_get_method)(void* prop);
typedef void*       (*fn_il2cpp_property_get_set_method)(void* prop);
typedef void*       (*fn_il2cpp_class_get_events)(void* klass, void** iter);
typedef const char* (*fn_il2cpp_event_get_name)(void* event);
typedef const char* (*fn_il2cpp_type_get_name)(void* type);
typedef int         (*fn_il2cpp_type_get_type)(void* type);
typedef bool        (*fn_il2cpp_type_is_byref)(void* type);
typedef bool        (*fn_il2cpp_type_is_pointer_type)(void* type);

struct IL2CPP_API {
    fn_il2cpp_domain_get il2cpp_domain_get;
    fn_il2cpp_domain_get_assemblies il2cpp_domain_get_assemblies;
    fn_il2cpp_assembly_get_image il2cpp_assembly_get_image;
    fn_il2cpp_image_get_class_count il2cpp_image_get_class_count;
    fn_il2cpp_image_get_class il2cpp_image_get_class;
    fn_il2cpp_image_get_name il2cpp_image_get_name;
    fn_il2cpp_class_get_name il2cpp_class_get_name;
    fn_il2cpp_class_get_namespace il2cpp_class_get_namespace;
    fn_il2cpp_class_get_parent il2cpp_class_get_parent;
    fn_il2cpp_class_get_type il2cpp_class_get_type;
    fn_il2cpp_class_get_flags il2cpp_class_get_flags;
    fn_il2cpp_class_get_interfaces il2cpp_class_get_interfaces;
    fn_il2cpp_class_get_nested_types il2cpp_class_get_nested_types;
    fn_il2cpp_class_is_enum il2cpp_class_is_enum;
    fn_il2cpp_class_is_valuetype il2cpp_class_is_valuetype;
    fn_il2cpp_class_instance_size il2cpp_class_instance_size;
    fn_il2cpp_class_value_size il2cpp_class_value_size;
    fn_il2cpp_class_num_fields il2cpp_class_num_fields;
    fn_il2cpp_class_get_element_class il2cpp_class_get_element_class;
    fn_il2cpp_class_is_generic il2cpp_class_is_generic;
    fn_il2cpp_class_is_inflated il2cpp_class_is_inflated;
    fn_il2cpp_class_get_fields il2cpp_class_get_fields;
    fn_il2cpp_field_get_name il2cpp_field_get_name;
    fn_il2cpp_field_get_offset il2cpp_field_get_offset;
    fn_il2cpp_field_get_type il2cpp_field_get_type;
    fn_il2cpp_field_get_flags il2cpp_field_get_flags;
    fn_il2cpp_field_is_literal il2cpp_field_is_literal;
    fn_il2cpp_class_get_methods il2cpp_class_get_methods;
    fn_il2cpp_method_get_name il2cpp_method_get_name;
    fn_il2cpp_method_get_param_count il2cpp_method_get_param_count;
    fn_il2cpp_method_get_param il2cpp_method_get_param;
    fn_il2cpp_method_get_return_type il2cpp_method_get_return_type;
    fn_il2cpp_method_get_flags il2cpp_method_get_flags;
    fn_il2cpp_method_get_token il2cpp_method_get_token;
    fn_il2cpp_method_is_generic il2cpp_method_is_generic;
    fn_il2cpp_method_is_inflated il2cpp_method_is_inflated;
    fn_il2cpp_method_is_instance il2cpp_method_is_instance;
    fn_il2cpp_class_get_properties il2cpp_class_get_properties;
    fn_il2cpp_property_get_name il2cpp_property_get_name;
    fn_il2cpp_property_get_get_method il2cpp_property_get_get_method;
    fn_il2cpp_property_get_set_method il2cpp_property_get_set_method;
    fn_il2cpp_class_get_events il2cpp_class_get_events;
    fn_il2cpp_event_get_name il2cpp_event_get_name;
    fn_il2cpp_type_get_name il2cpp_type_get_name;
    fn_il2cpp_type_get_type il2cpp_type_get_type;
    fn_il2cpp_type_is_byref il2cpp_type_is_byref;
    fn_il2cpp_type_is_pointer_type il2cpp_type_is_pointer_type;
};

static HANDLE g_hConsole = INVALID_HANDLE_VALUE;

static void resolve_il2cpp(HMODULE ga, IL2CPP_API& api) {
    memset(&api, 0, sizeof(api));
    #define R(name) \
        api.name = (fn_##name)GetProcAddress(ga, #name); \
        if (!api.name) { \
            SetConsoleTextAttribute(g_hConsole, CLR_RED); \
            printf("[!] %s not exported\n", #name); \
            SetConsoleTextAttribute(g_hConsole, CLR_WHITE); \
        }

    R(il2cpp_domain_get) R(il2cpp_domain_get_assemblies) R(il2cpp_assembly_get_image)
    R(il2cpp_image_get_class_count) R(il2cpp_image_get_class) R(il2cpp_image_get_name)
    R(il2cpp_class_get_name) R(il2cpp_class_get_namespace) R(il2cpp_class_get_parent)
    R(il2cpp_class_get_type) R(il2cpp_class_get_flags) R(il2cpp_class_get_interfaces)
    R(il2cpp_class_get_nested_types) R(il2cpp_class_is_enum) R(il2cpp_class_is_valuetype)
    R(il2cpp_class_instance_size) R(il2cpp_class_value_size) R(il2cpp_class_num_fields)
    R(il2cpp_class_get_element_class) R(il2cpp_class_is_generic) R(il2cpp_class_is_inflated)
    R(il2cpp_class_get_fields) R(il2cpp_field_get_name) R(il2cpp_field_get_offset)
    R(il2cpp_field_get_type) R(il2cpp_field_get_flags) R(il2cpp_field_is_literal)
    R(il2cpp_class_get_methods) R(il2cpp_method_get_name) R(il2cpp_method_get_param_count)
    R(il2cpp_method_get_param) R(il2cpp_method_get_return_type) R(il2cpp_method_get_flags)
    R(il2cpp_method_get_token) R(il2cpp_method_is_generic) R(il2cpp_method_is_inflated)
    R(il2cpp_method_is_instance) R(il2cpp_class_get_properties) R(il2cpp_property_get_name)
    R(il2cpp_property_get_get_method) R(il2cpp_property_get_set_method)
    R(il2cpp_class_get_events) R(il2cpp_event_get_name) R(il2cpp_type_get_name)
    R(il2cpp_type_get_type) R(il2cpp_type_is_byref) R(il2cpp_type_is_pointer_type)
    #undef R
}

struct DumpStats { size_t assemblies, classes, methods, fields, properties, events; };

static void emit(HANDLE hCon, FILE* f, WORD clr, const char* fmt, ...) {
    va_list args; char buf[4096];
    va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
    SetConsoleTextAttribute(hCon, clr);
    printf("%s", buf);
    if (f) fprintf(f, "%s", buf);
}

static const uint32_t FA_STATIC = 0x0010, FA_INITONLY = 0x0020, FA_LITERAL = 0x0040;
static const uint32_t MA_STATIC = 0x0010, MA_FINAL = 0x0020, MA_VIRTUAL = 0x0040, MA_ABSTRACT = 0x0400;

static std::string method_attrs(uint32_t fl, bool inst) {
    std::string r;
    auto a = [&](const char* s) { if (!r.empty()) r += ", "; r += s; };
    if (fl & MA_STATIC) a("static"); else if (inst) a("instance");
    if (fl & MA_VIRTUAL) a("virtual");
    if (fl & MA_ABSTRACT) a("abstract");
    if (fl & MA_FINAL) a("final");
    switch (fl & 7) { case 1: a("private"); break; case 3: a("internal"); break;
                       case 4: a("protected"); break; case 6: a("public"); break; }
    return r;
}

static std::string field_attrs(uint32_t fl, bool lit) {
    if (lit || (fl & FA_LITERAL)) return "literal/const";
    if (fl & FA_STATIC) return "static";
    std::string r = "instance";
    if (fl & FA_INITONLY) r += ", initonly";
    return r;
}

static std::string params(void* m, const IL2CPP_API& a) {
    std::string r;
    if (!a.il2cpp_method_get_param_count || !a.il2cpp_method_get_param) return r;
    uint32_t n = a.il2cpp_method_get_param_count(m);
    for (uint32_t i = 0; i < n; i++) {
        if (i) r += ", ";
        void* pt = a.il2cpp_method_get_param(m, i);
        r += (pt && a.il2cpp_type_get_name) ? safe_str(a.il2cpp_type_get_name(pt)) : "???";
    }
    return r;
}

static uintptr_t rva(void* m, uintptr_t base, uintptr_t end) {
    uintptr_t p = *(uintptr_t*)m;
    return (p && p >= base && p < end) ? p - base : 0;
}

static void dump_class(HANDLE h, FILE* f, void* k, const IL2CPP_API& a,
                       uintptr_t base, uintptr_t end, DumpStats& s) {
    const char* nm = a.il2cpp_class_get_name ? safe_str(a.il2cpp_class_get_name(k)) : "";
    const char* ns = a.il2cpp_class_get_namespace ? safe_str(a.il2cpp_class_get_namespace(k)) : "";
    uint32_t fl = a.il2cpp_class_get_flags ? a.il2cpp_class_get_flags(k) : 0;
    bool ise = a.il2cpp_class_is_enum && a.il2cpp_class_is_enum(k);
    bool isv = a.il2cpp_class_is_valuetype && a.il2cpp_class_is_valuetype(k);
    bool isg = a.il2cpp_class_is_generic && a.il2cpp_class_is_generic(k);
    uint32_t isz = a.il2cpp_class_instance_size ? a.il2cpp_class_instance_size(k) : 0;

    emit(h, f, CLR_DARK_GRAY, "// Flags: 0x%08X | Instance Size: 0x%X\n", fl, isz);
    emit(h, f, CLR_DARK_GRAY, "// Enum: %s | ValueType: %s | Generic: %s\n",
         ise?"true":"false", isv?"true":"false", isg?"true":"false");

    const char* kw = ise ? "enum" : isv ? "struct" : (fl & 0x20) ? "interface" : "class";
    std::string par;
    if (a.il2cpp_class_get_parent) {
        void* p = a.il2cpp_class_get_parent(k);
        if (p && a.il2cpp_class_get_name) {
            const char* pn = a.il2cpp_class_get_name(p);
            if (pn && pn[0]) par = std::string(" : ") + pn;
        }
    }
    if (ns[0]) emit(h, f, CLR_BRIGHT_CYAN, "namespace %s {\n", ns);
    emit(h, f, CLR_BRIGHT_CYAN, "    %s %s%s {\n", kw, nm, par.c_str());

    if (a.il2cpp_class_get_fields && a.il2cpp_field_get_name) {
        std::vector<void*> flds; void* it = nullptr;
        while (void* x = a.il2cpp_class_get_fields(k, &it)) flds.push_back(x);
        if (!flds.empty()) {
            emit(h, f, CLR_DARK_GRAY, "\n        // Fields (%zu)\n", flds.size());
            s.fields += flds.size();
            for (void* x : flds) {
                const char* fn = safe_str(a.il2cpp_field_get_name(x));
                uint32_t ff = a.il2cpp_field_get_flags ? a.il2cpp_field_get_flags(x) : 0;
                bool lit = (a.il2cpp_field_is_literal && a.il2cpp_field_is_literal(x)) || (ff & FA_LITERAL);
                std::string tn = "???";
                if (a.il2cpp_field_get_type && a.il2cpp_type_get_name) {
                    void* ft = a.il2cpp_field_get_type(x);
                    if (ft) tn = safe_str(a.il2cpp_type_get_name(ft));
                }
                if (lit) emit(h, f, CLR_BRIGHT_GREEN, "        [const] %s %s; // %s\n", tn.c_str(), fn, field_attrs(ff,lit).c_str());
                else if (ff & FA_STATIC) emit(h, f, CLR_BRIGHT_GREEN, "        [static] %s %s; // %s\n", tn.c_str(), fn, field_attrs(ff,lit).c_str());
                else {
                    size_t off = a.il2cpp_field_get_offset ? a.il2cpp_field_get_offset(x) : 0;
                    emit(h, f, CLR_BRIGHT_GREEN, "        [0x%zX] %s %s; // %s\n", off, tn.c_str(), fn, field_attrs(ff,lit).c_str());
                }
            }
        }
    }

    if (a.il2cpp_class_get_methods && a.il2cpp_method_get_name) {
        std::vector<void*> mths; void* it = nullptr;
        while (void* x = a.il2cpp_class_get_methods(k, &it)) mths.push_back(x);
        if (!mths.empty()) {
            emit(h, f, CLR_DARK_GRAY, "\n        // Methods (%zu)\n", mths.size());
            s.methods += mths.size();
            for (void* x : mths) {
                const char* mn = safe_str(a.il2cpp_method_get_name(x));
                uintptr_t r = rva(x, base, end);
                uint32_t tk = a.il2cpp_method_get_token ? a.il2cpp_method_get_token(x) : 0;
                std::string rt = "void";
                if (a.il2cpp_method_get_return_type && a.il2cpp_type_get_name) {
                    void* rty = a.il2cpp_method_get_return_type(x);
                    if (rty) rt = safe_str(a.il2cpp_type_get_name(rty));
                }
                uint32_t imp = 0;
                uint32_t mf = a.il2cpp_method_get_flags ? a.il2cpp_method_get_flags(x, &imp) : 0;
                bool inst = a.il2cpp_method_is_instance ? a.il2cpp_method_is_instance(x) : true;
                std::string at = method_attrs(mf, inst);
                std::string pr = params(x, a);
                emit(h, f, CLR_BRIGHT_YELLOW, "        [RVA: 0x%06zX] [Token: 0x%08X] %s%s %s(%s); // %s\n",
                     (size_t)r, tk, (mf & MA_STATIC) ? "static " : "", rt.c_str(), mn, pr.c_str(), at.c_str());
            }
        }
    }

    if (a.il2cpp_class_get_properties && a.il2cpp_property_get_name) {
        std::vector<void*> props; void* it = nullptr;
        while (void* x = a.il2cpp_class_get_properties(k, &it)) props.push_back(x);
        if (!props.empty()) {
            emit(h, f, CLR_DARK_GRAY, "\n        // Properties (%zu)\n", props.size());
            s.properties += props.size();
            for (void* x : props) {
                const char* pn = safe_str(a.il2cpp_property_get_name(x));
                void* g = a.il2cpp_property_get_get_method ? a.il2cpp_property_get_get_method(x) : nullptr;
                void* st = a.il2cpp_property_get_set_method ? a.il2cpp_property_get_set_method(x) : nullptr;
                std::string pt = "???";
                if (g && a.il2cpp_method_get_return_type && a.il2cpp_type_get_name) {
                    void* rt = a.il2cpp_method_get_return_type(g);
                    if (rt) pt = safe_str(a.il2cpp_type_get_name(rt));
                }
                std::string acc = "{ ";
                if (g) { char t[64]; snprintf(t, 64, "get [0x%zX]; ", (size_t)rva(g, base, end)); acc += t; }
                if (st) { char t[64]; snprintf(t, 64, "set [0x%zX]; ", (size_t)rva(st, base, end)); acc += t; }
                acc += "}";
                emit(h, f, CLR_BRIGHT_MAGENTA, "        %s %s %s\n", pt.c_str(), pn, acc.c_str());
            }
        }
    }

    if (a.il2cpp_class_get_events && a.il2cpp_event_get_name) {
        std::vector<void*> evts; void* it = nullptr;
        while (void* x = a.il2cpp_class_get_events(k, &it)) evts.push_back(x);
        if (!evts.empty()) {
            emit(h, f, CLR_DARK_GRAY, "\n        // Events\n");
            s.events += evts.size();
            for (void* x : evts) emit(h, f, CLR_BRIGHT_MAGENTA, "        %s;\n", safe_str(a.il2cpp_event_get_name(x)));
        }
    }

    if (a.il2cpp_class_get_interfaces && a.il2cpp_class_get_name) {
        std::vector<void*> ifs; void* it = nullptr;
        while (void* x = a.il2cpp_class_get_interfaces(k, &it)) ifs.push_back(x);
        if (!ifs.empty()) {
            emit(h, f, CLR_DARK_GRAY, "\n        // Interfaces\n");
            std::string l = "        implements ";
            for (size_t i = 0; i < ifs.size(); i++) {
                if (i) l += ", ";
                l += safe_str(a.il2cpp_class_get_name(ifs[i]));
            }
            emit(h, f, CLR_BRIGHT_CYAN, "%s\n", l.c_str());
        }
    }

    if (a.il2cpp_class_get_nested_types && a.il2cpp_class_get_name) {
        std::vector<void*> nst; void* it = nullptr;
        while (void* x = a.il2cpp_class_get_nested_types(k, &it)) nst.push_back(x);
        if (!nst.empty()) {
            emit(h, f, CLR_DARK_GRAY, "\n        // Nested Types\n");
            for (void* x : nst) emit(h, f, CLR_BRIGHT_CYAN, "        nested: %s.%s\n", nm, safe_str(a.il2cpp_class_get_name(x)));
        }
    }

    emit(h, f, CLR_BRIGHT_CYAN, "    }\n");
    if (ns[0]) emit(h, f, CLR_BRIGHT_CYAN, "}\n");
    emit(h, f, CLR_WHITE, "\n");
    s.classes++;
}

static DWORD WINAPI dumper_thread(LPVOID) {
    AllocConsole();
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD buf = { 120, 9999 };
    SetConsoleScreenBufferSize(g_hConsole, buf);
    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    SetConsoleTitleA("IL2CPP Runtime Dumper (proxy)");

    SetConsoleTextAttribute(g_hConsole, CLR_WHITE);
    printf("[*] version.dll proxy loaded — dumper active\n");
    printf("[*] Waiting for GameAssembly.dll...\n");

    HMODULE ga = nullptr;
    for (int i = 0; i < 120; i++) {
        ga = GetModuleHandleA("GameAssembly.dll");
        if (ga) break;
        Sleep(1000);
        if (i % 10 == 9) printf("[*] Still waiting... (%ds)\n", i + 1);
    }
    if (!ga) {
        SetConsoleTextAttribute(g_hConsole, CLR_RED);
        printf("[!] GameAssembly.dll not found after 120s\n");
        return 1;
    }

    printf("[+] GameAssembly.dll at 0x%p\n", ga);
    printf("[*] Waiting 10s for IL2CPP runtime init...\n");
    Sleep(10000);

    IL2CPP_API api;
    resolve_il2cpp(ga, api);

    if (!api.il2cpp_domain_get || !api.il2cpp_domain_get_assemblies ||
        !api.il2cpp_assembly_get_image || !api.il2cpp_image_get_class_count ||
        !api.il2cpp_image_get_class) {
        SetConsoleTextAttribute(g_hConsole, CLR_RED);
        printf("[!] Critical IL2CPP exports missing\n");
        return 1;
    }

    uintptr_t ga_base = (uintptr_t)ga;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((uint8_t*)ga + ((PIMAGE_DOS_HEADER)ga)->e_lfanew);
    uintptr_t ga_end = ga_base + nt->OptionalHeader.SizeOfImage;

    void* domain = api.il2cpp_domain_get();
    if (!domain) { printf("[!] domain_get returned null\n"); return 1; }

    size_t asm_count = 0;
    void** asms = api.il2cpp_domain_get_assemblies(domain, &asm_count);
    if (!asms || !asm_count) { printf("[!] No assemblies\n"); return 1; }

    printf("[+] %zu assemblies found\n", asm_count);

    CreateDirectoryA("C:\\Users\\ysg\\projects\\il2cpp_dumper\\dumps", nullptr);
    SYSTEMTIME st; GetLocalTime(&st);
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "C:\\Users\\ysg\\projects\\il2cpp_dumper\\dumps\\dump_%04d%02d%02d_%02d%02d%02d.txt",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    FILE* out = fopen(path, "w");
    if (!out) { printf("[!] Can't write %s\n", path); return 1; }

    fprintf(out, "// IL2CPP Runtime Dump\n// Timestamp: %04d-%02d-%02d %02d:%02d:%02d\n// GameAssembly.dll Base: 0x%zX\n// Assemblies: %zu\n\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, (size_t)ga_base, asm_count);

    DumpStats stats = {};
    stats.assemblies = asm_count;
    size_t gcl = 0;

    for (size_t i = 0; i < asm_count; i++) {
        void* img = api.il2cpp_assembly_get_image(asms[i]);
        if (!img) continue;
        const char* iname = api.il2cpp_image_get_name ? api.il2cpp_image_get_name(img) : "?";
        size_t cc = api.il2cpp_image_get_class_count(img);
        emit(g_hConsole, out, CLR_BRIGHT_WHITE, "\n// =========================================\n// Image: %s (%zu classes)\n// =========================================\n\n", iname?iname:"?", cc);

        for (size_t c = 0; c < cc; c++) {
            void* k = api.il2cpp_image_get_class(img, c);
            if (!k) continue;
            __try { dump_class(g_hConsole, out, k, api, ga_base, ga_end, stats); }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                emit(g_hConsole, out, CLR_RED, "// !!! EXCEPTION dumping class\n\n");
                stats.classes++;
            }
            if (++gcl % 100 == 0)
                emit(g_hConsole, nullptr, CLR_BRIGHT_YELLOW, "[Progress] %zu/%zu asm | %zu cls | %zu mth | %zu fld\n",
                     i+1, asm_count, stats.classes, stats.methods, stats.fields);
        }
    }

    fprintf(out, "\n// Summary: %zu asm, %zu cls, %zu mth, %zu fld, %zu prop, %zu evt\n",
            stats.assemblies, stats.classes, stats.methods, stats.fields, stats.properties, stats.events);
    fclose(out);

    emit(g_hConsole, nullptr, CLR_BRIGHT_GREEN, "\n[+] DONE: %s\n", path);
    emit(g_hConsole, nullptr, CLR_BRIGHT_GREEN, "[+] %zu classes | %zu methods | %zu fields | %zu properties | %zu events\n",
         stats.classes, stats.methods, stats.fields, stats.properties, stats.events);

    char title[256];
    snprintf(title, 256, "IL2CPP Dumper - Complete! (%zu classes)", stats.classes);
    SetConsoleTitleA(title);

    char msg[512];
    snprintf(msg, 512, "Dump complete!\n\nClasses: %zu\nMethods: %zu\nFields: %zu\nProperties: %zu\nEvents: %zu\n\nFile: %s",
             stats.classes, stats.methods, stats.fields, stats.properties, stats.events, path);
    MessageBoxA(nullptr, msg, "IL2CPP Dumper", MB_OK | MB_ICONINFORMATION);
    return 0;
}

// ===========================================================================
// DllMain
// ===========================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        load_real_version_dll();
        CreateThread(nullptr, 0, dumper_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}
