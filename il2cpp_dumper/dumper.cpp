// cl /EHsc /O2 /LD dumper.cpp /Fe:dumper.dll /link user32.lib
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <set>

// ---------------------------------------------------------------------------
// Console color constants
// ---------------------------------------------------------------------------
#define CLR_WHITE       0x07
#define CLR_DARK_GRAY   0x08
#define CLR_BRIGHT_GREEN  0x0A
#define CLR_BRIGHT_CYAN   0x0B
#define CLR_RED           0x0C
#define CLR_BRIGHT_MAGENTA 0x0D
#define CLR_BRIGHT_YELLOW  0x0E
#define CLR_BRIGHT_WHITE   0x0F

// ---------------------------------------------------------------------------
// safe_str
// ---------------------------------------------------------------------------
static const char* safe_str(const char* s) { return s ? s : ""; }

// ---------------------------------------------------------------------------
// IL2CPP function pointer typedefs -- all IL2CPP types are opaque void*
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// IL2CPP API struct
// ---------------------------------------------------------------------------
struct IL2CPP_API {
    fn_il2cpp_domain_get                il2cpp_domain_get;
    fn_il2cpp_domain_get_assemblies     il2cpp_domain_get_assemblies;
    fn_il2cpp_assembly_get_image        il2cpp_assembly_get_image;
    fn_il2cpp_image_get_class_count     il2cpp_image_get_class_count;
    fn_il2cpp_image_get_class           il2cpp_image_get_class;
    fn_il2cpp_image_get_name            il2cpp_image_get_name;

    fn_il2cpp_class_get_name            il2cpp_class_get_name;
    fn_il2cpp_class_get_namespace       il2cpp_class_get_namespace;
    fn_il2cpp_class_get_parent          il2cpp_class_get_parent;
    fn_il2cpp_class_get_type            il2cpp_class_get_type;
    fn_il2cpp_class_get_flags           il2cpp_class_get_flags;
    fn_il2cpp_class_get_interfaces      il2cpp_class_get_interfaces;
    fn_il2cpp_class_get_nested_types    il2cpp_class_get_nested_types;
    fn_il2cpp_class_is_enum             il2cpp_class_is_enum;
    fn_il2cpp_class_is_valuetype        il2cpp_class_is_valuetype;
    fn_il2cpp_class_instance_size       il2cpp_class_instance_size;
    fn_il2cpp_class_value_size          il2cpp_class_value_size;
    fn_il2cpp_class_num_fields          il2cpp_class_num_fields;
    fn_il2cpp_class_get_element_class   il2cpp_class_get_element_class;
    fn_il2cpp_class_is_generic          il2cpp_class_is_generic;
    fn_il2cpp_class_is_inflated         il2cpp_class_is_inflated;

    fn_il2cpp_class_get_fields          il2cpp_class_get_fields;
    fn_il2cpp_field_get_name            il2cpp_field_get_name;
    fn_il2cpp_field_get_offset          il2cpp_field_get_offset;
    fn_il2cpp_field_get_type            il2cpp_field_get_type;
    fn_il2cpp_field_get_flags           il2cpp_field_get_flags;
    fn_il2cpp_field_is_literal          il2cpp_field_is_literal;

    fn_il2cpp_class_get_methods         il2cpp_class_get_methods;
    fn_il2cpp_method_get_name           il2cpp_method_get_name;
    fn_il2cpp_method_get_param_count    il2cpp_method_get_param_count;
    fn_il2cpp_method_get_param          il2cpp_method_get_param;
    fn_il2cpp_method_get_return_type    il2cpp_method_get_return_type;
    fn_il2cpp_method_get_flags          il2cpp_method_get_flags;
    fn_il2cpp_method_get_token          il2cpp_method_get_token;
    fn_il2cpp_method_is_generic         il2cpp_method_is_generic;
    fn_il2cpp_method_is_inflated        il2cpp_method_is_inflated;
    fn_il2cpp_method_is_instance        il2cpp_method_is_instance;

    fn_il2cpp_class_get_properties      il2cpp_class_get_properties;
    fn_il2cpp_property_get_name         il2cpp_property_get_name;
    fn_il2cpp_property_get_get_method   il2cpp_property_get_get_method;
    fn_il2cpp_property_get_set_method   il2cpp_property_get_set_method;

    fn_il2cpp_class_get_events          il2cpp_class_get_events;
    fn_il2cpp_event_get_name            il2cpp_event_get_name;

    fn_il2cpp_type_get_name             il2cpp_type_get_name;
    fn_il2cpp_type_get_type             il2cpp_type_get_type;
    fn_il2cpp_type_is_byref             il2cpp_type_is_byref;
    fn_il2cpp_type_is_pointer_type      il2cpp_type_is_pointer_type;
};

// ---------------------------------------------------------------------------
// RESOLVE macro + resolve_all
// ---------------------------------------------------------------------------
static HANDLE g_hConsole = INVALID_HANDLE_VALUE;

#define RESOLVE(api, mod, name) \
    api.name = (fn_##name)GetProcAddress(mod, #name); \
    if (!api.name) { \
        SetConsoleTextAttribute(g_hConsole, CLR_RED); \
        printf("[!] Warning: %s not found\n", #name); \
        SetConsoleTextAttribute(g_hConsole, CLR_WHITE); \
    }

static void resolve_all(HMODULE ga, IL2CPP_API& api) {
    memset(&api, 0, sizeof(api));

    RESOLVE(api, ga, il2cpp_domain_get);
    RESOLVE(api, ga, il2cpp_domain_get_assemblies);
    RESOLVE(api, ga, il2cpp_assembly_get_image);
    RESOLVE(api, ga, il2cpp_image_get_class_count);
    RESOLVE(api, ga, il2cpp_image_get_class);
    RESOLVE(api, ga, il2cpp_image_get_name);

    RESOLVE(api, ga, il2cpp_class_get_name);
    RESOLVE(api, ga, il2cpp_class_get_namespace);
    RESOLVE(api, ga, il2cpp_class_get_parent);
    RESOLVE(api, ga, il2cpp_class_get_type);
    RESOLVE(api, ga, il2cpp_class_get_flags);
    RESOLVE(api, ga, il2cpp_class_get_interfaces);
    RESOLVE(api, ga, il2cpp_class_get_nested_types);
    RESOLVE(api, ga, il2cpp_class_is_enum);
    RESOLVE(api, ga, il2cpp_class_is_valuetype);
    RESOLVE(api, ga, il2cpp_class_instance_size);
    RESOLVE(api, ga, il2cpp_class_value_size);
    RESOLVE(api, ga, il2cpp_class_num_fields);
    RESOLVE(api, ga, il2cpp_class_get_element_class);
    RESOLVE(api, ga, il2cpp_class_is_generic);
    RESOLVE(api, ga, il2cpp_class_is_inflated);

    RESOLVE(api, ga, il2cpp_class_get_fields);
    RESOLVE(api, ga, il2cpp_field_get_name);
    RESOLVE(api, ga, il2cpp_field_get_offset);
    RESOLVE(api, ga, il2cpp_field_get_type);
    RESOLVE(api, ga, il2cpp_field_get_flags);
    RESOLVE(api, ga, il2cpp_field_is_literal);

    RESOLVE(api, ga, il2cpp_class_get_methods);
    RESOLVE(api, ga, il2cpp_method_get_name);
    RESOLVE(api, ga, il2cpp_method_get_param_count);
    RESOLVE(api, ga, il2cpp_method_get_param);
    RESOLVE(api, ga, il2cpp_method_get_return_type);
    RESOLVE(api, ga, il2cpp_method_get_flags);
    RESOLVE(api, ga, il2cpp_method_get_token);
    RESOLVE(api, ga, il2cpp_method_is_generic);
    RESOLVE(api, ga, il2cpp_method_is_inflated);
    RESOLVE(api, ga, il2cpp_method_is_instance);

    RESOLVE(api, ga, il2cpp_class_get_properties);
    RESOLVE(api, ga, il2cpp_property_get_name);
    RESOLVE(api, ga, il2cpp_property_get_get_method);
    RESOLVE(api, ga, il2cpp_property_get_set_method);

    RESOLVE(api, ga, il2cpp_class_get_events);
    RESOLVE(api, ga, il2cpp_event_get_name);

    RESOLVE(api, ga, il2cpp_type_get_name);
    RESOLVE(api, ga, il2cpp_type_get_type);
    RESOLVE(api, ga, il2cpp_type_is_byref);
    RESOLVE(api, ga, il2cpp_type_is_pointer_type);
}

// ---------------------------------------------------------------------------
// DumpStats
// ---------------------------------------------------------------------------
struct DumpStats {
    size_t assemblies;
    size_t classes;
    size_t methods;
    size_t fields;
    size_t properties;
    size_t events;
};

// ---------------------------------------------------------------------------
// DualOutput -- prints to console with color AND writes plain text to file
// ---------------------------------------------------------------------------
static void emit(HANDLE hConsole, FILE* file, WORD color, const char* fmt, ...) {
    va_list args;
    char buf[4096];

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    SetConsoleTextAttribute(hConsole, color);
    printf("%s", buf);

    if (file)
        fprintf(file, "%s", buf);
}

// ---------------------------------------------------------------------------
// Flag decoders
// ---------------------------------------------------------------------------
static const uint32_t FIELD_ATTRIBUTE_STATIC    = 0x0010;
static const uint32_t FIELD_ATTRIBUTE_INIT_ONLY = 0x0020;
static const uint32_t FIELD_ATTRIBUTE_LITERAL   = 0x0040;

static const uint32_t METHOD_ATTRIBUTE_STATIC   = 0x0010;
static const uint32_t METHOD_ATTRIBUTE_FINAL    = 0x0020;
static const uint32_t METHOD_ATTRIBUTE_VIRTUAL  = 0x0040;
static const uint32_t METHOD_ATTRIBUTE_ABSTRACT = 0x0400;

static std::string decode_method_attrs(uint32_t flags, bool is_instance) {
    std::string result;
    auto append = [&](const char* s) {
        if (!result.empty()) result += ", ";
        result += s;
    };

    if (flags & METHOD_ATTRIBUTE_STATIC)   append("static");
    else if (is_instance)                  append("instance");

    if (flags & METHOD_ATTRIBUTE_VIRTUAL)  append("virtual");
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) append("abstract");
    if (flags & METHOD_ATTRIBUTE_FINAL)    append("final");

    switch (flags & 0x0007) {
        case 1: append("private"); break;
        case 2: append("fam_and_assem"); break;
        case 3: append("assem"); break;
        case 4: append("family"); break;
        case 5: append("fam_or_assem"); break;
        case 6: append("public"); break;
    }

    return result;
}

static std::string decode_field_attrs(uint32_t flags, bool is_literal) {
    std::string result;
    auto append = [&](const char* s) {
        if (!result.empty()) result += ", ";
        result += s;
    };

    if (is_literal || (flags & FIELD_ATTRIBUTE_LITERAL))  append("literal/const");
    else if (flags & FIELD_ATTRIBUTE_STATIC)              append("static");
    else                                                  append("instance");

    if (flags & FIELD_ATTRIBUTE_INIT_ONLY) append("initonly");

    return result;
}

// ---------------------------------------------------------------------------
// Parameter list builder
// ---------------------------------------------------------------------------
static std::string build_param_list(void* method, const IL2CPP_API& api) {
    std::string result;
    if (!api.il2cpp_method_get_param_count || !api.il2cpp_method_get_param)
        return result;

    uint32_t count = api.il2cpp_method_get_param_count(method);
    for (uint32_t i = 0; i < count; i++) {
        if (i > 0) result += ", ";
        void* param_type = api.il2cpp_method_get_param(method, i);
        if (param_type && api.il2cpp_type_get_name) {
            const char* tname = api.il2cpp_type_get_name(param_type);
            result += safe_str(tname);
        } else {
            result += "???";
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// RVA computation
// ---------------------------------------------------------------------------
static uintptr_t compute_rva(void* method, uintptr_t ga_base, uintptr_t ga_end) {
    uintptr_t fn_ptr = *(uintptr_t*)method;
    if (fn_ptr == 0 || fn_ptr < ga_base || fn_ptr >= ga_end)
        return 0;
    return fn_ptr - ga_base;
}

// ---------------------------------------------------------------------------
// dump_class
// ---------------------------------------------------------------------------
static void dump_class(HANDLE hCon, FILE* file, void* klass, const IL2CPP_API& api,
                       uintptr_t ga_base, uintptr_t ga_end, DumpStats& stats) {
    const char* name  = api.il2cpp_class_get_name ? safe_str(api.il2cpp_class_get_name(klass)) : "";
    const char* ns    = api.il2cpp_class_get_namespace ? safe_str(api.il2cpp_class_get_namespace(klass)) : "";
    uint32_t flags    = api.il2cpp_class_get_flags ? api.il2cpp_class_get_flags(klass) : 0;
    bool is_enum      = api.il2cpp_class_is_enum && api.il2cpp_class_is_enum(klass);
    bool is_vtype     = api.il2cpp_class_is_valuetype && api.il2cpp_class_is_valuetype(klass);
    bool is_generic   = api.il2cpp_class_is_generic && api.il2cpp_class_is_generic(klass);
    uint32_t inst_sz  = api.il2cpp_class_instance_size ? api.il2cpp_class_instance_size(klass) : 0;

    emit(hCon, file, CLR_DARK_GRAY, "// Flags: 0x%08X | Instance Size: 0x%X\n", flags, inst_sz);
    emit(hCon, file, CLR_DARK_GRAY, "// Is Enum: %s | Is ValueType: %s | Is Generic: %s\n",
         is_enum ? "true" : "false", is_vtype ? "true" : "false", is_generic ? "true" : "false");

    const char* keyword = "class";
    if (is_enum)       keyword = "enum";
    else if (is_vtype) keyword = "struct";
    else if (flags & 0x20) keyword = "interface";

    std::string parent_str;
    if (api.il2cpp_class_get_parent) {
        void* parent = api.il2cpp_class_get_parent(klass);
        if (parent && api.il2cpp_class_get_name) {
            const char* pname = api.il2cpp_class_get_name(parent);
            if (pname && pname[0])
                parent_str = std::string(" : ") + pname;
        }
    }

    if (ns[0])
        emit(hCon, file, CLR_BRIGHT_CYAN, "namespace %s {\n", ns);

    emit(hCon, file, CLR_BRIGHT_CYAN, "    %s %s%s {\n", keyword, name, parent_str.c_str());

    // ----- Fields -----
    if (api.il2cpp_class_get_fields && api.il2cpp_field_get_name) {
        std::vector<void*> fields;
        void* iter = nullptr;
        while (void* f = api.il2cpp_class_get_fields(klass, &iter))
            fields.push_back(f);

        if (!fields.empty()) {
            emit(hCon, file, CLR_DARK_GRAY, "\n        // Fields (%zu)\n", fields.size());
            stats.fields += fields.size();

            for (void* f : fields) {
                const char* fname = safe_str(api.il2cpp_field_get_name(f));
                uint32_t fflags = api.il2cpp_field_get_flags ? api.il2cpp_field_get_flags(f) : 0;
                bool literal = (api.il2cpp_field_is_literal && api.il2cpp_field_is_literal(f))
                               || (fflags & FIELD_ATTRIBUTE_LITERAL);

                std::string type_name = "???";
                if (api.il2cpp_field_get_type && api.il2cpp_type_get_name) {
                    void* ftype = api.il2cpp_field_get_type(f);
                    if (ftype) type_name = safe_str(api.il2cpp_type_get_name(ftype));
                }

                std::string attrs = decode_field_attrs(fflags, literal);

                if (literal) {
                    emit(hCon, file, CLR_BRIGHT_GREEN, "        [const] %s %s; // %s\n",
                         type_name.c_str(), fname, attrs.c_str());
                } else if (fflags & FIELD_ATTRIBUTE_STATIC) {
                    emit(hCon, file, CLR_BRIGHT_GREEN, "        [static] %s %s; // %s\n",
                         type_name.c_str(), fname, attrs.c_str());
                } else {
                    size_t offset = api.il2cpp_field_get_offset ? api.il2cpp_field_get_offset(f) : 0;
                    emit(hCon, file, CLR_BRIGHT_GREEN, "        [0x%zX] %s %s; // %s\n",
                         offset, type_name.c_str(), fname, attrs.c_str());
                }
            }
        }
    }

    // ----- Methods -----
    if (api.il2cpp_class_get_methods && api.il2cpp_method_get_name) {
        std::vector<void*> methods;
        void* iter = nullptr;
        while (void* m = api.il2cpp_class_get_methods(klass, &iter))
            methods.push_back(m);

        if (!methods.empty()) {
            emit(hCon, file, CLR_DARK_GRAY, "\n        // Methods (%zu)\n", methods.size());
            stats.methods += methods.size();

            for (void* m : methods) {
                const char* mname = safe_str(api.il2cpp_method_get_name(m));
                uintptr_t rva = compute_rva(m, ga_base, ga_end);
                uint32_t token = api.il2cpp_method_get_token ? api.il2cpp_method_get_token(m) : 0;

                std::string ret_type = "void";
                if (api.il2cpp_method_get_return_type && api.il2cpp_type_get_name) {
                    void* rtype = api.il2cpp_method_get_return_type(m);
                    if (rtype) ret_type = safe_str(api.il2cpp_type_get_name(rtype));
                }

                std::string params = build_param_list(m, api);

                uint32_t impl_flags = 0;
                uint32_t mflags = api.il2cpp_method_get_flags ? api.il2cpp_method_get_flags(m, &impl_flags) : 0;
                bool is_inst = api.il2cpp_method_is_instance ? api.il2cpp_method_is_instance(m) : true;
                std::string attrs = decode_method_attrs(mflags, is_inst);

                if (api.il2cpp_method_is_generic && api.il2cpp_method_is_generic(m)) {
                    if (!attrs.empty()) attrs += ", ";
                    attrs += "generic";
                }
                if (api.il2cpp_method_is_inflated && api.il2cpp_method_is_inflated(m)) {
                    if (!attrs.empty()) attrs += ", ";
                    attrs += "inflated";
                }

                emit(hCon, file, CLR_BRIGHT_YELLOW,
                     "        [RVA: 0x%06zX] [Token: 0x%08X] %s%s %s(%s); // %s\n",
                     (size_t)rva, token,
                     (mflags & METHOD_ATTRIBUTE_STATIC) ? "static " : "",
                     ret_type.c_str(), mname, params.c_str(), attrs.c_str());
            }
        }
    }

    // ----- Properties -----
    if (api.il2cpp_class_get_properties && api.il2cpp_property_get_name) {
        std::vector<void*> props;
        void* iter = nullptr;
        while (void* p = api.il2cpp_class_get_properties(klass, &iter))
            props.push_back(p);

        if (!props.empty()) {
            emit(hCon, file, CLR_DARK_GRAY, "\n        // Properties (%zu)\n", props.size());
            stats.properties += props.size();

            for (void* p : props) {
                const char* pname = safe_str(api.il2cpp_property_get_name(p));

                void* getter = api.il2cpp_property_get_get_method ? api.il2cpp_property_get_get_method(p) : nullptr;
                void* setter = api.il2cpp_property_get_set_method ? api.il2cpp_property_get_set_method(p) : nullptr;

                std::string prop_type = "???";
                if (getter && api.il2cpp_method_get_return_type && api.il2cpp_type_get_name) {
                    void* rtype = api.il2cpp_method_get_return_type(getter);
                    if (rtype) prop_type = safe_str(api.il2cpp_type_get_name(rtype));
                } else if (setter && api.il2cpp_method_get_param && api.il2cpp_type_get_name) {
                    void* ptype = api.il2cpp_method_get_param(setter, 0);
                    if (ptype) prop_type = safe_str(api.il2cpp_type_get_name(ptype));
                }

                std::string acc = "{ ";
                if (getter) {
                    uintptr_t grva = compute_rva(getter, ga_base, ga_end);
                    char tmp[64];
                    snprintf(tmp, sizeof(tmp), "get [0x%zX]; ", (size_t)grva);
                    acc += tmp;
                }
                if (setter) {
                    uintptr_t srva = compute_rva(setter, ga_base, ga_end);
                    char tmp[64];
                    snprintf(tmp, sizeof(tmp), "set [0x%zX]; ", (size_t)srva);
                    acc += tmp;
                }
                acc += "}";

                emit(hCon, file, CLR_BRIGHT_MAGENTA, "        %s %s %s\n",
                     prop_type.c_str(), pname, acc.c_str());
            }
        }
    }

    // ----- Events -----
    if (api.il2cpp_class_get_events && api.il2cpp_event_get_name) {
        std::vector<void*> events;
        void* iter = nullptr;
        while (void* e = api.il2cpp_class_get_events(klass, &iter))
            events.push_back(e);

        if (!events.empty()) {
            emit(hCon, file, CLR_DARK_GRAY, "\n        // Events\n");
            stats.events += events.size();
            for (void* e : events) {
                const char* ename = safe_str(api.il2cpp_event_get_name(e));
                emit(hCon, file, CLR_BRIGHT_MAGENTA, "        %s;\n", ename);
            }
        }
    }

    // ----- Interfaces -----
    if (api.il2cpp_class_get_interfaces && api.il2cpp_class_get_name) {
        std::vector<void*> ifaces;
        void* iter = nullptr;
        while (void* iface = api.il2cpp_class_get_interfaces(klass, &iter))
            ifaces.push_back(iface);

        if (!ifaces.empty()) {
            emit(hCon, file, CLR_DARK_GRAY, "\n        // Interfaces\n");
            std::string line = "        implements ";
            for (size_t i = 0; i < ifaces.size(); i++) {
                if (i > 0) line += ", ";
                line += safe_str(api.il2cpp_class_get_name(ifaces[i]));
            }
            line += "\n";
            emit(hCon, file, CLR_BRIGHT_CYAN, "%s", line.c_str());
        }
    }

    // ----- Nested Types -----
    if (api.il2cpp_class_get_nested_types && api.il2cpp_class_get_name) {
        std::vector<void*> nested;
        void* iter = nullptr;
        while (void* n = api.il2cpp_class_get_nested_types(klass, &iter))
            nested.push_back(n);

        if (!nested.empty()) {
            emit(hCon, file, CLR_DARK_GRAY, "\n        // Nested Types\n");
            for (void* n : nested) {
                const char* nname = safe_str(api.il2cpp_class_get_name(n));
                emit(hCon, file, CLR_BRIGHT_CYAN, "        nested: %s.%s\n", name, nname);
            }
        }
    }

    emit(hCon, file, CLR_BRIGHT_CYAN, "    }\n");
    if (ns[0])
        emit(hCon, file, CLR_BRIGHT_CYAN, "}\n");

    emit(hCon, file, CLR_WHITE, "\n");
    stats.classes++;
}

// ---------------------------------------------------------------------------
// run_dump -- orchestrator
// ---------------------------------------------------------------------------
static void run_dump(HANDLE hCon, HMODULE ga, const IL2CPP_API& api) {
    uintptr_t ga_base = (uintptr_t)ga;
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)ga;
    PIMAGE_NT_HEADERS nt  = (PIMAGE_NT_HEADERS)((uint8_t*)ga + dos->e_lfanew);
    uintptr_t ga_end = ga_base + nt->OptionalHeader.SizeOfImage;

    void* domain = api.il2cpp_domain_get ? api.il2cpp_domain_get() : nullptr;
    if (!domain) {
        emit(hCon, nullptr, CLR_RED, "[!] il2cpp_domain_get returned null\n");
        return;
    }

    size_t asm_count = 0;
    void** assemblies = api.il2cpp_domain_get_assemblies ? api.il2cpp_domain_get_assemblies(domain, &asm_count) : nullptr;
    if (!assemblies || asm_count == 0) {
        emit(hCon, nullptr, CLR_RED, "[!] No assemblies found\n");
        return;
    }

    emit(hCon, nullptr, CLR_WHITE, "[*] Found %zu assemblies\n", asm_count);

    CreateDirectoryA("C:\\Users\\ysg\\projects\\il2cpp_dumper\\dumps", nullptr);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath),
             "C:\\Users\\ysg\\projects\\il2cpp_dumper\\dumps\\dump_%04d%02d%02d_%02d%02d%02d.txt",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    FILE* file = fopen(filepath, "w");
    if (!file) {
        emit(hCon, nullptr, CLR_RED, "[!] Failed to open: %s\n", filepath);
        return;
    }

    emit(hCon, nullptr, CLR_WHITE, "[*] Output file: %s\n", filepath);

    size_t total_class_count = 0;
    size_t total_method_count = 0;
    size_t total_field_count = 0;
    for (size_t a = 0; a < asm_count; a++) {
        void* image = api.il2cpp_assembly_get_image ? api.il2cpp_assembly_get_image(assemblies[a]) : nullptr;
        if (!image) continue;
        size_t cc = api.il2cpp_image_get_class_count ? api.il2cpp_image_get_class_count(image) : 0;
        total_class_count += cc;
        for (size_t c = 0; c < cc; c++) {
            void* klass = api.il2cpp_image_get_class ? api.il2cpp_image_get_class(image, c) : nullptr;
            if (!klass) continue;
            if (api.il2cpp_class_get_methods) {
                void* iter = nullptr;
                while (api.il2cpp_class_get_methods(klass, &iter)) total_method_count++;
            }
            if (api.il2cpp_class_get_fields) {
                void* iter = nullptr;
                while (api.il2cpp_class_get_fields(klass, &iter)) total_field_count++;
            }
        }
    }

    fprintf(file, "// IL2CPP Runtime Dump\n");
    fprintf(file, "// Timestamp: %04d-%02d-%02d %02d:%02d:%02d\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    fprintf(file, "// GameAssembly.dll Base: 0x%zX\n", (size_t)ga_base);
    fprintf(file, "// Total Assemblies: %zu\n", asm_count);
    fprintf(file, "// Total Classes: %zu\n", total_class_count);
    fprintf(file, "// Total Methods: %zu\n", total_method_count);
    fprintf(file, "// Total Fields: %zu\n", total_field_count);
    fprintf(file, "\n");

    DumpStats stats = {};
    stats.assemblies = asm_count;
    size_t global_class_idx = 0;

    for (size_t a = 0; a < asm_count; a++) {
        void* image = api.il2cpp_assembly_get_image ? api.il2cpp_assembly_get_image(assemblies[a]) : nullptr;
        if (!image) continue;

        const char* img_name = "unknown";
        if (api.il2cpp_image_get_name) {
            const char* n = api.il2cpp_image_get_name(image);
            if (n) img_name = n;
        }

        size_t cls_count = api.il2cpp_image_get_class_count ? api.il2cpp_image_get_class_count(image) : 0;

        emit(hCon, file, CLR_BRIGHT_WHITE, "\n// =========================================\n");
        emit(hCon, file, CLR_BRIGHT_WHITE, "// Image: %s\n", img_name);
        emit(hCon, file, CLR_BRIGHT_WHITE, "// =========================================\n\n");

        for (size_t c = 0; c < cls_count; c++) {
            void* klass = api.il2cpp_image_get_class ? api.il2cpp_image_get_class(image, c) : nullptr;
            if (!klass) continue;

            __try {
                dump_class(hCon, file, klass, api, ga_base, ga_end, stats);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                const char* cname = "";
                __try {
                    if (api.il2cpp_class_get_name)
                        cname = safe_str(api.il2cpp_class_get_name(klass));
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    cname = "<unreadable>";
                }
                emit(hCon, file, CLR_RED, "// !!! EXCEPTION dumping class: %s\n\n", cname);
                stats.classes++;
            }

            global_class_idx++;
            if (global_class_idx % 100 == 0) {
                emit(hCon, nullptr, CLR_BRIGHT_YELLOW,
                     "[Progress] %zu/%zu assemblies | %zu classes | %zu methods | %zu fields\n",
                     a + 1, asm_count, stats.classes, stats.methods, stats.fields);
            }
        }
    }

    emit(hCon, file, CLR_DARK_GRAY, "\n// =========================================\n");
    emit(hCon, file, CLR_DARK_GRAY, "// Dump Summary\n");
    emit(hCon, file, CLR_DARK_GRAY, "// =========================================\n");
    emit(hCon, file, CLR_DARK_GRAY, "// Total Assemblies: %zu\n", stats.assemblies);
    emit(hCon, file, CLR_DARK_GRAY, "// Total Classes: %zu\n", stats.classes);
    emit(hCon, file, CLR_DARK_GRAY, "// Total Methods: %zu\n", stats.methods);
    emit(hCon, file, CLR_DARK_GRAY, "// Total Fields: %zu\n", stats.fields);
    emit(hCon, file, CLR_DARK_GRAY, "// Total Properties: %zu\n", stats.properties);
    emit(hCon, file, CLR_DARK_GRAY, "// Total Events: %zu\n", stats.events);

    fclose(file);

    emit(hCon, nullptr, CLR_BRIGHT_GREEN, "\n[+] Dump complete: %s\n", filepath);
    emit(hCon, nullptr, CLR_BRIGHT_GREEN,
         "[+] Classes: %zu | Methods: %zu | Fields: %zu | Properties: %zu | Events: %zu\n",
         stats.classes, stats.methods, stats.fields, stats.properties, stats.events);

    char title[256];
    snprintf(title, sizeof(title), "IL2CPP Runtime Dumper - %zu classes", stats.classes);
    SetConsoleTitleA(title);
}

// ---------------------------------------------------------------------------
// worker_thread
// ---------------------------------------------------------------------------
static void dump_class_brief(HANDLE hCon, IL2CPP_API& api, void* klass) {
    const char* name = api.il2cpp_class_get_name(klass);
    const char* ns = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(klass) : "";

    SetConsoleTextAttribute(hCon, CLR_BRIGHT_CYAN);
    printf("  + %s.%s", safe_str(ns), safe_str(name));

    if (api.il2cpp_class_instance_size) {
        SetConsoleTextAttribute(hCon, CLR_DARK_GRAY);
        printf(" (0x%X)", api.il2cpp_class_instance_size(klass));
    }
    printf("\n");

    if (api.il2cpp_class_get_fields) {
        void* fiter = nullptr;
        while (void* field = api.il2cpp_class_get_fields(klass, &fiter)) {
            const char* fname = api.il2cpp_field_get_name ? api.il2cpp_field_get_name(field) : "?";
            size_t foff = api.il2cpp_field_get_offset ? api.il2cpp_field_get_offset(field) : 0;
            void* ftype = api.il2cpp_field_get_type ? api.il2cpp_field_get_type(field) : nullptr;
            const char* ftname = (ftype && api.il2cpp_type_get_name) ? api.il2cpp_type_get_name(ftype) : "?";
            uint32_t fflags = api.il2cpp_field_get_flags ? api.il2cpp_field_get_flags(field) : 0;
            bool is_static = (fflags & 0x0010) != 0;

            SetConsoleTextAttribute(hCon, is_static ? CLR_BRIGHT_YELLOW : CLR_BRIGHT_GREEN);
            printf("      %s0x%03zX  %s %s\n",
                   is_static ? "[S] " : "    ", foff, safe_str(ftname), safe_str(fname));
        }
    }

    if (api.il2cpp_class_get_methods) {
        void* miter = nullptr;
        while (void* method = api.il2cpp_class_get_methods(klass, &miter)) {
            const char* mname = api.il2cpp_method_get_name ? api.il2cpp_method_get_name(method) : "?";
            uint32_t pcount = api.il2cpp_method_get_param_count ? api.il2cpp_method_get_param_count(method) : 0;
            SetConsoleTextAttribute(hCon, CLR_BRIGHT_MAGENTA);
            printf("      -> %s(%u params) @ 0x%p\n", safe_str(mname), pcount, method);
        }
    }
}

static DWORD WINAPI worker_thread(LPVOID) {
    AllocConsole();

    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hConsole = hCon;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hCon, &csbi);
    COORD bufSize = { csbi.dwSize.X, 9999 };
    SetConsoleScreenBufferSize(hCon, bufSize);

    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);

    SetConsoleTitleA("IL2CPP Runtime Dumper — Live Monitor");

    SetConsoleTextAttribute(hCon, CLR_WHITE);
    printf("[*] IL2CPP Runtime Dumper loaded\n");
    printf("[*] Waiting 5 seconds for runtime initialization...\n");
    Sleep(5000);

    HMODULE ga = GetModuleHandleA("GameAssembly.dll");
    if (!ga) {
        SetConsoleTextAttribute(hCon, CLR_RED);
        printf("[!] GameAssembly.dll not found in process\n");
        return 1;
    }

    SetConsoleTextAttribute(hCon, CLR_WHITE);
    printf("[*] GameAssembly.dll at 0x%p\n", ga);

    IL2CPP_API api;
    resolve_all(ga, api);

    if (!api.il2cpp_domain_get || !api.il2cpp_domain_get_assemblies ||
        !api.il2cpp_assembly_get_image || !api.il2cpp_image_get_class_count ||
        !api.il2cpp_image_get_class) {
        SetConsoleTextAttribute(hCon, CLR_RED);
        printf("[!] Critical API functions missing\n");
        return 1;
    }

    SetConsoleTextAttribute(hCon, CLR_BRIGHT_GREEN);
    printf("[+] API resolved.\n\n");

    // Initial full dump to file (quiet — just saves)
    run_dump(hCon, ga, api);

    // Now go into live monitoring mode
    SetConsoleTextAttribute(hCon, CLR_BRIGHT_WHITE);
    printf("\n[*] Entering live monitor mode — polling every 3s for new classes...\n");
    printf("[*] Go play the game. New classes will print here as they load.\n\n");

    // Track every class pointer we've already seen
    std::set<void*> seen_classes;

    void* domain = api.il2cpp_domain_get();
    size_t asm_count = 0;
    void** assemblies = api.il2cpp_domain_get_assemblies(domain, &asm_count);

    // Seed with everything currently loaded
    for (size_t a = 0; a < asm_count; a++) {
        void* image = api.il2cpp_assembly_get_image(assemblies[a]);
        if (!image) continue;
        size_t cc = api.il2cpp_image_get_class_count(image);
        for (size_t c = 0; c < cc; c++) {
            void* klass = api.il2cpp_image_get_class(image, c);
            if (klass) seen_classes.insert(klass);
        }
    }

    SetConsoleTextAttribute(hCon, CLR_DARK_GRAY);
    printf("[*] Tracking %zu known classes. Watching for new ones...\n\n", seen_classes.size());

    size_t scan_num = 0;
    while (true) {
        Sleep(3000);
        scan_num++;

        // Re-fetch assemblies (new ones may have loaded)
        domain = api.il2cpp_domain_get();
        assemblies = api.il2cpp_domain_get_assemblies(domain, &asm_count);
        if (!assemblies) continue;

        int new_count = 0;
        for (size_t a = 0; a < asm_count; a++) {
            void* image = api.il2cpp_assembly_get_image(assemblies[a]);
            if (!image) continue;

            const char* img_name = api.il2cpp_image_get_name ? api.il2cpp_image_get_name(image) : "?";
            size_t cc = api.il2cpp_image_get_class_count(image);

            for (size_t c = 0; c < cc; c++) {
                void* klass = api.il2cpp_image_get_class(image, c);
                if (!klass) continue;
                if (seen_classes.count(klass)) continue;

                // NEW CLASS
                seen_classes.insert(klass);
                new_count++;

                if (new_count == 1) {
                    SetConsoleTextAttribute(hCon, CLR_BRIGHT_WHITE);
                    printf("── scan #%zu ─── NEW CLASSES ──────────────────\n", scan_num);
                }

                SetConsoleTextAttribute(hCon, CLR_DARK_GRAY);
                printf("  [%s]\n", safe_str(img_name));
                dump_class_brief(hCon, api, klass);
            }
        }

        if (new_count > 0) {
            SetConsoleTextAttribute(hCon, CLR_BRIGHT_GREEN);
            printf("── +%d new (total: %zu) ────────────────────────\n\n", new_count, seen_classes.size());

            // Re-dump to file with the new data
            run_dump(hCon, ga, api);
        }

        // Update title with status
        char title[256];
        snprintf(title, sizeof(title), "IL2CPP Live — %zu classes | scan #%zu", seen_classes.size(), scan_num);
        SetConsoleTitleA(title);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}
