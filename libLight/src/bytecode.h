#pragma once

#include <light/light_types.h>

namespace light
{

    // 32-bit instruction, little-endian in .lbc
    //   ABC:  op:8 A:8 B:8 C:8
    //   ABx:  op:8 A:8 Bx:16 (unsigned)
    //   AsBx: op:8 A:8 sBx:16 (signed)
    using Instruction = u32;

    enum class OpFormat : u8
    {
        ABC,
        ABx,
        AsBx
    };

    enum class Op : u8
    {
#define OP(name, format) name,
#include "light_opcodes.def"
#undef OP
        COUNT
    };

    constexpr u32 OP_COUNT = (u32)Op::COUNT;

    inline const char* op_name(Op op)
    {
        static const char* const names[] = {
#define OP(name, format) #name,
#include "light_opcodes.def"
#undef OP
        };
        return (u32)op < OP_COUNT ? names[(u32)op] : "INVALID";
    }

    inline OpFormat op_format(Op op)
    {
        static const OpFormat formats[] = {
#define OP(name, format) OpFormat::format,
#include "light_opcodes.def"
#undef OP
        };
        return formats[(u32)op];
    }

    inline Instruction encode_abc(Op op, u8 a, u8 b, u8 c)
    {
        return (u32)op | ((u32)a << 8) | ((u32)b << 16) | ((u32)c << 24);
    }

    inline Instruction encode_abx(Op op, u8 a, u16 bx)
    {
        return (u32)op | ((u32)a << 8) | ((u32)bx << 16);
    }

    inline Instruction encode_asbx(Op op, u8 a, i16 sbx) { return encode_abx(op, a, (u16)sbx); }

    inline Op decode_op(Instruction i) { return (Op)(i & 0xFF); }
    inline u8 decode_a(Instruction i) { return (u8)((i >> 8) & 0xFF); }
    inline u8 decode_b(Instruction i) { return (u8)((i >> 16) & 0xFF); }
    inline u8 decode_c(Instruction i) { return (u8)((i >> 24) & 0xFF); }
    inline u16 decode_bx(Instruction i) { return (u16)(i >> 16); }
    inline i16 decode_sbx(Instruction i) { return (i16)(u16)(i >> 16); }

} // namespace light
