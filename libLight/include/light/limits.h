#pragma once

#include <light/light_types.h>
#include <light/error.h>

namespace light
{

    // registers per function is not configurable: A/B/C operands are u8, so 256
    constexpr u32 REGISTERS_PER_FUNCTION = 256;

    struct VmLimits
    {
        u32 register_stack_slots = 4096; // Value slots shared by all frames
        u32 max_frames = 256;            // call depth
        u32 max_constants = 65536;       // per chunk; LOAD_CONST Bx is u16
        u32 max_instructions = 1u << 20; // per function
        u32 max_parse_depth = 200;
        u32 max_ast_nodes = 1u << 20;
        u32 max_diagnostics = 100;
        u64 memory_limit = 0; // GC heap bytes, 0 = unlimited

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
