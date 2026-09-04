#pragma once

#include <light/light_api.h>

#include "chunk.h"
#include "fmt.h"

namespace light
{

    // "0000  1  LOAD_INT16       0 42"; LOAD_CONST appends "; <constant>"
    inline void disassemble_instruction(const Chunk& chunk, u32 offset, ct::String& out)
    {
        Instruction ins = chunk.code[offset];
        Op op = decode_op(ins);

        fmt::append_u32_zero_padded(out, offset, 4);
        out += "  ";
        fmt::append_u32(out, offset < chunk.lines.size() ? chunk.lines[offset] : 0);
        out += "  ";
        fmt::append_padded(out, op_name(op), 17);

        switch (op_format(op))
        {
        case OpFormat::ABC:
            fmt::append_u32(out, decode_a(ins));
            out += " ";
            fmt::append_u32(out, decode_b(ins));
            out += " ";
            fmt::append_u32(out, decode_c(ins));
            break;
        case OpFormat::ABx:
            fmt::append_u32(out, decode_a(ins));
            out += " ";
            fmt::append_u32(out, decode_bx(ins));
            break;
        case OpFormat::AsBx:
            fmt::append_u32(out, decode_a(ins));
            out += " ";
            fmt::append_i64(out, decode_sbx(ins));
            break;
        }

        if (op == Op::LOAD_CONST && decode_bx(ins) < chunk.constants.size())
        {
            const Value& v = chunk.constants[decode_bx(ins)];
            out += " ; ";
            switch (v.type)
            {
            case ValueType::Nil:
                out += "nil";
                break;
            case ValueType::Bool:
                out += v.as_bool() ? "true" : "false";
                break;
            case ValueType::Int:
                fmt::append_i64(out, v.as_int());
                break;
            case ValueType::Float:
                out += "float";
                break;
            case ValueType::Object:
                out += "object";
                break;
            }
        }
    }

    inline void disassemble_chunk(const Chunk& chunk, const char* name, const VmConfig& config)
    {
        if (!config.print)
            return;
        {
            ct::String header("== ");
            header += name;
            header += " ==";
            config.print(config.print_ud, LogLevel::Info, header.c_str(), (u32)header.size());
        }
        for (u32 i = 0; i < (u32)chunk.code.size(); i++)
        {
            ct::String line;
            disassemble_instruction(chunk, i, line);
            config.print(config.print_ud, LogLevel::Info, line.c_str(), (u32)line.size());
        }
    }

} // namespace light
