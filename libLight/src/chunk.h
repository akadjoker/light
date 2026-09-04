#pragma once

#include <light/light_types.h>
#include <ct/vector.hpp>

#include "bytecode.h"
#include "value.h"

namespace light
{

    struct Chunk
    {
        ct::Vector<Instruction> code;
        ct::Vector<Value> constants;
        ct::Vector<u32> lines; // one entry per instruction

        u32 write(Instruction ins, u32 line)
        {
            code.push_back(ins);
            lines.push_back(line);
            return (u32)code.size() - 1;
        }

        // raw append, no dedup: the emitter dedups via a compile-time map
        // (bitwise keys for floats); the .lbc loader receives already-deduped pools
        u32 add_constant(Value v)
        {
            constants.push_back(v);
            return (u32)constants.size() - 1;
        }
    };

} // namespace light
