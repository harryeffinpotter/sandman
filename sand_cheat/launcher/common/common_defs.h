// common_defs.h — shared between launcher (C++) and kerneldriver (C).
// Must be C-compatible. Avoid C++-only syntax.
//
// Protocol constants must match on both sides.

#pragma once

#include <stdint.h>

// SplitMix64 stream cipher constants (public, standard SplitMix64).
#define SM64_INCREMENT  0x61C8864680B583EBULL
#define SM64_MULT1      0xBF58476D1CE4E5B9ULL
#define SM64_MULT2      0x94D049BB133111EBULL

// Command buffer magic (post-decrypt sanity value).
#define CMD_MAGIC       0x7C5A

// Command IDs (plain #define for C compatibility).
#define CMD_READ_MEMORY      0
#define CMD_WRITE_MEMORY     1
#define CMD_FIND_MODULE      2
#define CMD_ALLOC_MEMORY     3
#define CMD_PROTECT_MEMORY   4
#define CMD_PROTECT_ALT      5
#define CMD_QUERY_MODULE     6
#define CMD_HEARTBEAT        7

// Command buffer wire layout:
//   [0x00-0x03]  uint32 total_size     (bytes including this header; 1..0x800)
//   [0x04-0x0F]  12 bytes random key   (per-message, caller-generated)
//   [0x10+]      encrypted payload:
//       [0x00-0x01]  uint16 magic      (CMD_MAGIC after decrypt)
//       [0x02-0x03]  uint16 cmd_id
//       [0x04+]      command-specific params
#define CMD_HEADER_SIZE  16  // 4 size + 12 key
#define CMD_MAX_SIZE     0x800

// Per-command param structs. Packed, little-endian.
#pragma pack(push, 1)

typedef struct CmdReadMemory {
    uint16_t magic;
    uint16_t cmd_id;
    uint32_t pid;
    uint32_t pad;
    uint64_t src;       // address in target process
    uint64_t dst;       // address in local (launcher) process
    uint64_t size;
} CmdReadMemory;

typedef struct CmdWriteMemory {
    uint16_t magic;
    uint16_t cmd_id;
    uint32_t pid;
    uint64_t src;       // address in local (launcher) process
    uint64_t dst;       // address in target process
    uint64_t size;
} CmdWriteMemory;

typedef struct CmdFindModule {
    uint16_t magic;
    uint16_t cmd_id;
    uint32_t pid;
    uint32_t pad;
    uint64_t name_ptr;  // usermode ptr to wchar_t*
    uint16_t name_len;  // bytes (wcslen * 2)
    uint16_t pad2[3];
    uint64_t out_base;
    uint64_t out_size;
} CmdFindModule;

typedef struct CmdAllocMemory {
    uint16_t magic;
    uint16_t cmd_id;
    uint32_t pid;
    uint32_t pad;
    uint64_t base_inout;
    uint64_t size;
    uint32_t alloc_type;
    uint32_t protect;
    uint64_t out_ptr;
} CmdAllocMemory;

typedef struct CmdProtectMemory {
    uint16_t magic;
    uint16_t cmd_id;
    uint32_t pid;
    uint64_t addr;
    uint64_t size;
    uint32_t new_protect;
} CmdProtectMemory;

typedef struct CmdQueryModule {
    uint16_t magic;
    uint16_t cmd_id;
    uint32_t pid;
    uint64_t addr;
    uint64_t out_info;  // usermode ptr to 48-byte buffer
} CmdQueryModule;

typedef struct CmdHeartbeat {
    uint16_t magic;
    uint16_t cmd_id;
    uint32_t pad;
    uint64_t target_addr;  // kernel writes 1 byte here
} CmdHeartbeat;

#pragma pack(pop)

// Rolling-XOR key for embedded encrypted blobs.
static const uint8_t ROLLING_XOR_KEY[3] = { 0x46, 0x28, 0x64 };  // "F(d"
