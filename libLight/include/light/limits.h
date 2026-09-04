#pragma once

#include <light/light_types.h>
#include <light/error.h>

namespace light
{

    // registers per function is not configurable: A/B/C operands are u8, so 256
    constexpr u32 REGISTERS_PER_FUNCTION = 256;

    constexpr u32 DEFAULT_REGISTER_STACK_SLOTS = 4096;
    constexpr u32 DEFAULT_MAX_FRAMES = 256;
    constexpr u32 DEFAULT_MAX_CONSTANTS = 65536;
    constexpr u32 DEFAULT_MAX_INSTRUCTIONS = 1u << 20;
    constexpr u32 DEFAULT_MAX_PARSE_DEPTH = 200;
    constexpr u32 DEFAULT_MAX_AST_NODES = 1u << 20;
    constexpr u32 DEFAULT_MAX_DIAGNOSTICS = 100;
    constexpr u64 DEFAULT_MEMORY_LIMIT = 0; // 0 = unlimited

    struct VmLimits
    {
        u32 register_stack_slots = DEFAULT_REGISTER_STACK_SLOTS; // Value slots for all frames
        u32 max_frames = DEFAULT_MAX_FRAMES;                     // call depth
        u32 max_constants = DEFAULT_MAX_CONSTANTS;               // per chunk; LOAD_CONST Bx is u16
        u32 max_instructions = DEFAULT_MAX_INSTRUCTIONS;         // per function
        u32 max_parse_depth = DEFAULT_MAX_PARSE_DEPTH;
        u32 max_ast_nodes = DEFAULT_MAX_AST_NODES;
        u32 max_diagnostics = DEFAULT_MAX_DIAGNOSTICS;
        u64 memory_limit = DEFAULT_MEMORY_LIMIT; // GC heap bytes

        Status validate() const
        {
            if (register_stack_slots < REGISTERS_PER_FUNCTION)
                return Status::fail(ErrorCode::InvalidLimits);
            if (max_frames == 0 || max_instructions == 0)
                return Status::fail(ErrorCode::InvalidLimits);
            if (max_constants == 0 || max_constants > 65536)
                return Status::fail(ErrorCode::InvalidLimits);
            if (max_parse_depth == 0 || max_ast_nodes == 0 || max_diagnostics == 0)
                return Status::fail(ErrorCode::InvalidLimits);
            return Status::success();
        }
    };

} // namespace light
