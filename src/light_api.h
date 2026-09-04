#pragma once

#include "light_types.h"

#define LIGHT_VERSION_MAJOR 0
#define LIGHT_VERSION_MINOR 1
#define LIGHT_VERSION_PATCH 0

namespace light
{

    const char* version_string();

    enum class LogLevel : u8
    {
        Info,
        Warn,
        Error
    };

    struct ModuleSource
    {
        const u8* bytes;
        u32 length;
        bool is_bytecode; // true = precompiled .lbc, false = Light source
    };

    struct VmConfig
    {
        // lua_Alloc contract: new_size == 0 frees, ptr == nullptr allocates
        void* (*alloc)(void* ud, void* ptr, usize old_size, usize new_size);
        void* alloc_ud;

        // bytes is not null-terminated
        void (*print)(void* ud, LogLevel level, const char* bytes, u32 length);
        void* print_ud;

        // the VM never keeps out->bytes; it always calls release_module when done
        bool (*load_module)(void* ud, const char* name, u32 name_len, ModuleSource* out);
        void (*release_module)(void* ud, ModuleSource* src);
        void* loader_ud;
    };

} // namespace light
