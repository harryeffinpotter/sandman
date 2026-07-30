// cl /EHsc /O2 /LD proxy_winmm.cpp /Fe:winmm.dll /DEF:winmm.def /link user32.lib
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>

#define CLR_WHITE 0x07
#define CLR_DARK_GRAY 0x08
#define CLR_BRIGHT_GREEN 0x0A
#define CLR_BRIGHT_CYAN 0x0B
#define CLR_RED 0x0C
#define CLR_BRIGHT_MAGENTA 0x0D
#define CLR_BRIGHT_YELLOW 0x0E
#define CLR_BRIGHT_WHITE 0x0F

static const char* safe_str(const char* s) { return s ? s : ""; }

typedef void* (*fn_il2cpp_domain_get)();
typedef void** (*fn_il2cpp_domain_get_assemblies)(void*, size_t*);
typedef void* (*fn_il2cpp_assembly_get_image)(void*);
typedef size_t (*fn_il2cpp_image_get_class_count)(void*);
typedef void* (*fn_il2cpp_image_get_class)(void*, size_t);
typedef const char* (*fn_il2cpp_image_get_name)(void*);
typedef const char* (*fn_il2cpp_class_get_name)(void*);
typedef const char* (*fn_il2cpp_class_get_namespace)(void*);
typedef void* (*fn_il2cpp_class_get_parent)(void*);
typedef uint32_t (*fn_il2cpp_class_get_flags)(void*);
typedef void* (*fn_il2cpp_class_get_interfaces)(void*, void**);
typedef void* (*fn_il2cpp_class_get_nested_types)(void*, void**);
typedef bool (*fn_il2cpp_class_is_enum)(void*);
typedef bool (*fn_il2cpp_class_is_valuetype)(void*);
typedef uint32_t (*fn_il2cpp_class_instance_size)(void*);
typedef bool (*fn_il2cpp_class_is_generic)(void*);
typedef void* (*fn_il2cpp_class_get_fields)(void*, void**);
typedef const char* (*fn_il2cpp_field_get_name)(void*);
typedef size_t (*fn_il2cpp_field_get_offset)(void*);
typedef void* (*fn_il2cpp_field_get_type)(void*);
typedef uint32_t (*fn_il2cpp_field_get_flags)(void*);
typedef bool (*fn_il2cpp_field_is_literal)(void*);
typedef void* (*fn_il2cpp_class_get_methods)(void*, void**);
typedef const char* (*fn_il2cpp_method_get_name)(void*);
typedef uint32_t (*fn_il2cpp_method_get_param_count)(void*);
typedef void* (*fn_il2cpp_method_get_param)(void*, uint32_t);
typedef void* (*fn_il2cpp_method_get_return_type)(void*);
typedef uint32_t (*fn_il2cpp_method_get_flags)(void*, uint32_t*);
typedef uint32_t (*fn_il2cpp_method_get_token)(void*);
typedef bool (*fn_il2cpp_method_is_instance)(void*);
typedef void* (*fn_il2cpp_class_get_properties)(void*, void**);
typedef const char* (*fn_il2cpp_property_get_name)(void*);
typedef void* (*fn_il2cpp_property_get_get_method)(void*);
typedef void* (*fn_il2cpp_property_get_set_method)(void*);
typedef void* (*fn_il2cpp_class_get_events)(void*, void**);
typedef const char* (*fn_il2cpp_event_get_name)(void*);
typedef const char* (*fn_il2cpp_type_get_name)(void*);

struct IL2CPP_API {
    fn_il2cpp_domain_get domain_get;
    fn_il2cpp_domain_get_assemblies domain_get_assemblies;
    fn_il2cpp_assembly_get_image assembly_get_image;
    fn_il2cpp_image_get_class_count image_get_class_count;
    fn_il2cpp_image_get_class image_get_class;
    fn_il2cpp_image_get_name image_get_name;
    fn_il2cpp_class_get_name class_get_name;
    fn_il2cpp_class_get_namespace class_get_namespace;
    fn_il2cpp_class_get_parent class_get_parent;
    fn_il2cpp_class_get_flags class_get_flags;
    fn_il2cpp_class_get_interfaces class_get_interfaces;
    fn_il2cpp_class_get_nested_types class_get_nested_types;
    fn_il2cpp_class_is_enum class_is_enum;
    fn_il2cpp_class_is_valuetype class_is_valuetype;
    fn_il2cpp_class_instance_size class_instance_size;
    fn_il2cpp_class_is_generic class_is_generic;
    fn_il2cpp_class_get_fields class_get_fields;
    fn_il2cpp_field_get_name field_get_name;
    fn_il2cpp_field_get_offset field_get_offset;
    fn_il2cpp_field_get_type field_get_type;
    fn_il2cpp_field_get_flags field_get_flags;
    fn_il2cpp_field_is_literal field_is_literal;
    fn_il2cpp_class_get_methods class_get_methods;
    fn_il2cpp_method_get_name method_get_name;
    fn_il2cpp_method_get_param_count method_get_param_count;
    fn_il2cpp_method_get_param method_get_param;
    fn_il2cpp_method_get_return_type method_get_return_type;
    fn_il2cpp_method_get_flags method_get_flags;
    fn_il2cpp_method_get_token method_get_token;
    fn_il2cpp_method_is_instance method_is_instance;
    fn_il2cpp_class_get_properties class_get_properties;
    fn_il2cpp_property_get_name property_get_name;
    fn_il2cpp_property_get_get_method property_get_get_method;
    fn_il2cpp_property_get_set_method property_get_set_method;
    fn_il2cpp_class_get_events class_get_events;
    fn_il2cpp_event_get_name event_get_name;
    fn_il2cpp_type_get_name type_get_name;
};

static HANDLE g_hCon = INVALID_HANDLE_VALUE;

static void resolve_api(HMODULE ga, IL2CPP_API& a) {
    memset(&a, 0, sizeof(a));
    #define R(field, export_name) \
        a.field = (fn_il2cpp_##field)GetProcAddress(ga, "il2cpp_" export_name); \
        if (!a.field) { SetConsoleTextAttribute(g_hCon, CLR_RED); printf("[!] il2cpp_%s missing\n", export_name); SetConsoleTextAttribute(g_hCon, CLR_WHITE); }
    R(domain_get,"domain_get") R(domain_get_assemblies,"domain_get_assemblies")
    R(assembly_get_image,"assembly_get_image") R(image_get_class_count,"image_get_class_count")
    R(image_get_class,"image_get_class") R(image_get_name,"image_get_name")
    R(class_get_name,"class_get_name") R(class_get_namespace,"class_get_namespace")
    R(class_get_parent,"class_get_parent") R(class_get_flags,"class_get_flags")
    R(class_get_interfaces,"class_get_interfaces") R(class_get_nested_types,"class_get_nested_types")
    R(class_is_enum,"class_is_enum") R(class_is_valuetype,"class_is_valuetype")
    R(class_instance_size,"class_instance_size") R(class_is_generic,"class_is_generic")
    R(class_get_fields,"class_get_fields") R(field_get_name,"field_get_name")
    R(field_get_offset,"field_get_offset") R(field_get_type,"field_get_type")
    R(field_get_flags,"field_get_flags") R(field_is_literal,"field_is_literal")
    R(class_get_methods,"class_get_methods") R(method_get_name,"method_get_name")
    R(method_get_param_count,"method_get_param_count") R(method_get_param,"method_get_param")
    R(method_get_return_type,"method_get_return_type") R(method_get_flags,"method_get_flags")
    R(method_get_token,"method_get_token") R(method_is_instance,"method_is_instance")
    R(class_get_properties,"class_get_properties") R(property_get_name,"property_get_name")
    R(property_get_get_method,"property_get_get_method") R(property_get_set_method,"property_get_set_method")
    R(class_get_events,"class_get_events") R(event_get_name,"event_get_name")
    R(type_get_name,"type_get_name")
    #undef R
}

static void emit(HANDLE h, FILE* f, WORD c, const char* fmt, ...) {
    va_list args; char buf[4096];
    va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
    SetConsoleTextAttribute(h, c); printf("%s", buf);
    if (f) fprintf(f, "%s", buf);
}

static uintptr_t get_rva(void* m, uintptr_t base, uintptr_t end) {
    uintptr_t p = *(uintptr_t*)m;
    return (p && p >= base && p < end) ? p - base : 0;
}

struct Stats { size_t assemblies, classes, methods, fields; };

static void dump_class(HANDLE h, FILE* f, void* k, const IL2CPP_API& a, uintptr_t base, uintptr_t end, Stats& s) {
    const char* nm = a.class_get_name ? safe_str(a.class_get_name(k)) : "";
    const char* ns = a.class_get_namespace ? safe_str(a.class_get_namespace(k)) : "";
    uint32_t fl = a.class_get_flags ? a.class_get_flags(k) : 0;
    bool ise = a.class_is_enum && a.class_is_enum(k);
    bool isv = a.class_is_valuetype && a.class_is_valuetype(k);
    uint32_t isz = a.class_instance_size ? a.class_instance_size(k) : 0;

    emit(h, f, CLR_DARK_GRAY, "// Flags: 0x%08X | Size: 0x%X | %s%s\n", fl, isz,
         ise?"enum ":isv?"struct ":"", (fl&0x20)?"interface ":"");

    std::string par;
    if (a.class_get_parent) { void* p = a.class_get_parent(k);
        if (p && a.class_get_name) { const char* pn = a.class_get_name(p); if (pn && pn[0]) par = std::string(" : ") + pn; } }

    if (ns[0]) emit(h, f, CLR_BRIGHT_CYAN, "namespace %s {\n", ns);
    emit(h, f, CLR_BRIGHT_CYAN, "    %s %s%s {\n", ise?"enum":isv?"struct":(fl&0x20)?"interface":"class", nm, par.c_str());

    if (a.class_get_fields && a.field_get_name) {
        void* it = nullptr; void* x; bool first = true;
        while ((x = a.class_get_fields(k, &it))) {
            if (first) { emit(h, f, CLR_DARK_GRAY, "        // Fields\n"); first = false; }
            s.fields++;
            const char* fn = safe_str(a.field_get_name(x));
            uint32_t ff = a.field_get_flags ? a.field_get_flags(x) : 0;
            bool lit = (a.field_is_literal && a.field_is_literal(x)) || (ff & 0x40);
            std::string tn = "???";
            if (a.field_get_type && a.type_get_name) { void* ft = a.field_get_type(x); if (ft) tn = safe_str(a.type_get_name(ft)); }
            if (lit) emit(h, f, CLR_BRIGHT_GREEN, "        [const] %s %s;\n", tn.c_str(), fn);
            else if (ff & 0x10) emit(h, f, CLR_BRIGHT_GREEN, "        [static] %s %s;\n", tn.c_str(), fn);
            else { size_t off = a.field_get_offset ? a.field_get_offset(x) : 0;
                   emit(h, f, CLR_BRIGHT_GREEN, "        [0x%zX] %s %s;\n", off, tn.c_str(), fn); }
        }
    }

    if (a.class_get_methods && a.method_get_name) {
        void* it = nullptr; void* x; bool first = true;
        while ((x = a.class_get_methods(k, &it))) {
            if (first) { emit(h, f, CLR_DARK_GRAY, "\n        // Methods\n"); first = false; }
            s.methods++;
            const char* mn = safe_str(a.method_get_name(x));
            uintptr_t r = get_rva(x, base, end);
            uint32_t tk = a.method_get_token ? a.method_get_token(x) : 0;
            std::string rt = "void";
            if (a.method_get_return_type && a.type_get_name) { void* rty = a.method_get_return_type(x); if (rty) rt = safe_str(a.type_get_name(rty)); }
            uint32_t imp = 0; uint32_t mf = a.method_get_flags ? a.method_get_flags(x, &imp) : 0;
            std::string pr;
            if (a.method_get_param_count && a.method_get_param) {
                uint32_t n = a.method_get_param_count(x);
                for (uint32_t i = 0; i < n; i++) { if (i) pr += ", ";
                    void* pt = a.method_get_param(x, i);
                    pr += (pt && a.type_get_name) ? safe_str(a.type_get_name(pt)) : "???"; } }
            emit(h, f, CLR_BRIGHT_YELLOW, "        [RVA: 0x%06zX] [0x%08X] %s%s %s(%s);\n",
                 (size_t)r, tk, (mf&0x10)?"static ":"", rt.c_str(), mn, pr.c_str());
        }
    }

    if (a.class_get_properties && a.property_get_name) {
        void* it = nullptr; void* x; bool first = true;
        while ((x = a.class_get_properties(k, &it))) {
            if (first) { emit(h, f, CLR_DARK_GRAY, "\n        // Properties\n"); first = false; }
            const char* pn = safe_str(a.property_get_name(x));
            void* g = a.property_get_get_method ? a.property_get_get_method(x) : nullptr;
            void* st = a.property_get_set_method ? a.property_get_set_method(x) : nullptr;
            std::string acc = "{ ";
            if (g) { char t[64]; snprintf(t, 64, "get [0x%zX]; ", (size_t)get_rva(g, base, end)); acc += t; }
            if (st) { char t[64]; snprintf(t, 64, "set [0x%zX]; ", (size_t)get_rva(st, base, end)); acc += t; }
            acc += "}";
            emit(h, f, CLR_BRIGHT_MAGENTA, "        %s %s\n", pn, acc.c_str());
        }
    }

    if (a.class_get_interfaces && a.class_get_name) {
        std::vector<void*> ifs; void* it = nullptr;
        while (void* x = a.class_get_interfaces(k, &it)) ifs.push_back(x);
        if (!ifs.empty()) {
            emit(h, f, CLR_DARK_GRAY, "\n        // Interfaces\n");
            std::string l = "        implements ";
            for (size_t i = 0; i < ifs.size(); i++) { if (i) l += ", "; l += safe_str(a.class_get_name(ifs[i])); }
            emit(h, f, CLR_BRIGHT_CYAN, "%s\n", l.c_str());
        }
    }

    emit(h, f, CLR_BRIGHT_CYAN, "    }\n");
    if (ns[0]) emit(h, f, CLR_BRIGHT_CYAN, "}\n");
    emit(h, f, CLR_WHITE, "\n");
    s.classes++;
}

static DWORD WINAPI dumper_thread(LPVOID) {
    AllocConsole();
    g_hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD buf = {120, 9999}; SetConsoleScreenBufferSize(g_hCon, buf);
    FILE* dummy = nullptr; freopen_s(&dummy, "CONOUT$", "w", stdout);
    SetConsoleTitleA("IL2CPP Runtime Dumper (winmm proxy)");

    printf("[*] winmm.dll proxy loaded\n[*] Polling for GameAssembly.dll...\n");

    HMODULE ga = nullptr;
    for (int i = 0; i < 120; i++) {
        ga = GetModuleHandleA("GameAssembly.dll");
        if (ga) break;
        Sleep(1000);
        if (i % 10 == 9) printf("[*] Waiting... %ds\n", i + 1);
    }
    if (!ga) { printf("[!] GameAssembly.dll not found\n"); return 1; }

    printf("[+] GameAssembly at 0x%p\n[*] Waiting 10s for IL2CPP init...\n", ga);
    Sleep(10000);

    IL2CPP_API api; resolve_api(ga, api);
    if (!api.domain_get || !api.domain_get_assemblies || !api.assembly_get_image ||
        !api.image_get_class_count || !api.image_get_class) {
        printf("[!] Critical exports missing\n"); return 1;
    }

    uintptr_t base = (uintptr_t)ga;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((uint8_t*)ga + ((PIMAGE_DOS_HEADER)ga)->e_lfanew);
    uintptr_t end = base + nt->OptionalHeader.SizeOfImage;

    void* dom = api.domain_get();
    if (!dom) { printf("[!] domain_get null\n"); return 1; }
    size_t ac = 0; void** asms = api.domain_get_assemblies(dom, &ac);
    if (!asms || !ac) { printf("[!] No assemblies\n"); return 1; }
    printf("[+] %zu assemblies\n", ac);

    CreateDirectoryA("C:\\Users\\ysg\\projects\\il2cpp_dumper\\dumps", nullptr);
    SYSTEMTIME st; GetLocalTime(&st);
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "C:\\Users\\ysg\\projects\\il2cpp_dumper\\dumps\\runtime_%04d%02d%02d_%02d%02d%02d.txt",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    FILE* out = fopen(path, "w");
    if (!out) { printf("[!] Can't write %s\n", path); return 1; }

    fprintf(out, "// IL2CPP RUNTIME Dump\n// Base: 0x%zX\n// Assemblies: %zu\n\n", (size_t)base, ac);

    Stats s = {}; s.assemblies = ac; size_t gc = 0;
    for (size_t i = 0; i < ac; i++) {
        void* img = api.assembly_get_image(asms[i]); if (!img) continue;
        const char* in = api.image_get_name ? api.image_get_name(img) : "?";
        size_t cc = api.image_get_class_count(img);
        emit(g_hCon, out, CLR_BRIGHT_WHITE, "\n// === %s (%zu classes) ===\n\n", in?in:"?", cc);
        for (size_t c = 0; c < cc; c++) {
            void* k = api.image_get_class(img, c); if (!k) continue;
            __try { dump_class(g_hCon, out, k, api, base, end, s); }
            __except(EXCEPTION_EXECUTE_HANDLER) { emit(g_hCon, out, CLR_RED, "// EXCEPTION\n\n"); s.classes++; }
            if (++gc % 100 == 0)
                emit(g_hCon, nullptr, CLR_BRIGHT_YELLOW, "[%zu/%zu asm | %zu cls | %zu mth | %zu fld]\n", i+1, ac, s.classes, s.methods, s.fields);
        }
    }

    fprintf(out, "\n// TOTAL: %zu asm, %zu cls, %zu mth, %zu fld\n", s.assemblies, s.classes, s.methods, s.fields);
    fclose(out);

    emit(g_hCon, nullptr, CLR_BRIGHT_GREEN, "\n[+] DONE: %s\n[+] %zu cls | %zu mth | %zu fld\n", path, s.classes, s.methods, s.fields);
    char title[256]; snprintf(title, 256, "IL2CPP Dumper - Done! (%zu classes)", s.classes);
    SetConsoleTitleA(title);

    char msg[512]; snprintf(msg, 512, "Runtime dump complete!\n\nClasses: %zu\nMethods: %zu\nFields: %zu\n\nFile: %s",
                            s.classes, s.methods, s.fields, path);
    MessageBoxA(nullptr, msg, "IL2CPP Runtime Dumper", MB_OK | MB_ICONINFORMATION);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, dumper_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}
