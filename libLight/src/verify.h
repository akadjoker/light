#pragma once

#include <light/error.h>
#include <light/limits.h>

#include "vm.h"

namespace light
{

    // Static bytecode validation, run once before execution.
    //
    // Everything checkable without running the program is checked here so the
    // dispatch loop can index regs[base + A] without a bounds test per operand.
    // The contract the loop relies on afterwards:
    //   - every register operand read by an opcode is < proto->register_count
    //   - every jump target is within [0, code.size()]
    //   - every constant / function / native index resolves
    //
    // Which operands are registers depends on the opcode, not on the format:
    // LOAD_CONST's Bx is a constant pool index, CALL's Bx is a function index.
    // Register operands are therefore listed per opcode below.
    struct VerifyError
    {
        ErrorCode code;
        u32 function; // index into Program::functions
        u32 pc;       // instruction offset inside that function's chunk
    };

    namespace detail
    {
        // register operands used by each opcode, as a bitmask
        enum : u8
        {
            USES_A = 1 << 0,
            USES_B = 1 << 1,
            USES_C = 1 << 2
        };

        inline u8 register_operands(Op op)
        {
            switch (op)
            {
            case Op::NOP:
            case Op::HALT:
                return 0;

            // A is a register, Bx/sBx is an immediate or a table index
            case Op::LOAD_NIL:
            case Op::LOAD_INT16:
            case Op::LOAD_CONST:
            case Op::JUMP_IF_TRUE:
            case Op::JUMP_IF_FALSE:
                return USES_A;

            // LOAD_BOOL's B is an immediate, not a register
            case Op::LOAD_BOOL:
                return USES_A;

            case Op::MOVE:
            case Op::NEG_INT:
            case Op::NEG_FLOAT:
            case Op::NOT:
                return USES_A | USES_B;

            case Op::ADD_INT:
            case Op::SUB_INT:
            case Op::MUL_INT:
            case Op::DIV_INT:
            case Op::ADD_FLOAT:
            case Op::SUB_FLOAT:
            case Op::MUL_FLOAT:
            case Op::DIV_FLOAT:
            case Op::EQUAL:
            case Op::LESS_INT:
            case Op::LESS_EQUAL_INT:
            case Op::LESS_FLOAT:
            case Op::LESS_EQUAL_FLOAT:
                return USES_A | USES_B | USES_C;

            // sBx is a displacement, A is unused
            case Op::JUMP:
                return 0;

            // A is the window start; the window itself is bounds-checked at
            // run time against the register stack, not against register_count
            case Op::CALL:
            case Op::CALL_NATIVE:
                return 0;

            // A is checked against register_count together with result_count
            case Op::RETURN:
                return 0;

            case Op::COUNT:
                break;
            }
            return 0;
        }
    } // namespace detail

    // Validates one function body. `natives_count` bounds CALL_NATIVE indices.
    inline Status verify_function(const Program& program, u32 function_index, u32 natives_count,
                                  const VmLimits& limits, VerifyError* out)
    {
        const Prototype& proto = program.functions[function_index];
        if (!proto.chunk)
            return Status::fail(ErrorCode::CorruptBytecode);

        const Chunk& chunk = *proto.chunk;
        const u32 count = (u32)chunk.code.size();
        const u32 regs = proto.register_count;

        VerifyError err{ErrorCode::CorruptBytecode, function_index, 0};
        // report the first offending instruction; `out` is filled on every failure
        struct Reporter
        {
            VerifyError* out;
            VerifyError* err;
            Status fail(ErrorCode code, u32 pc)
            {
                err->code = code;
                err->pc = pc;
                if (out)
                    *out = *err;
                return Status::fail(code);
            }
        } report{out, &err};

        if (count == 0)
            return report.fail(ErrorCode::CorruptBytecode, 0);
        if (count > limits.max_instructions)
            return report.fail(ErrorCode::BytecodeTooLarge, 0);
        if (chunk.constants.size() > limits.max_constants)
            return report.fail(ErrorCode::TooManyConstants, 0);
        if (regs > REGISTERS_PER_FUNCTION)
            return report.fail(ErrorCode::TooManyRegisters, 0);
        // the loop reads lines[pc] when reporting a runtime error
        if (chunk.lines.size() != count)
            return report.fail(ErrorCode::CorruptBytecode, 0);
        // result_count must fit the window RETURN copies into
        if ((u32)proto.result_count > regs || (u32)proto.param_count > regs)
            return report.fail(ErrorCode::CorruptBytecode, 0);

        for (u32 pc = 0; pc < count; pc++)
        {
            const Instruction ins = chunk.code[pc];
            const u32 opcode = ins & 0xFF;
            if (opcode >= OP_COUNT)
                return report.fail(ErrorCode::CorruptBytecode, pc);
            const Op op = (Op)opcode;

            const u8 uses = detail::register_operands(op);
            if ((uses & detail::USES_A) && (u32)decode_a(ins) >= regs)
                return report.fail(ErrorCode::CorruptBytecode, pc);
            if ((uses & detail::USES_B) && (u32)decode_b(ins) >= regs)
                return report.fail(ErrorCode::CorruptBytecode, pc);
            if ((uses & detail::USES_C) && (u32)decode_c(ins) >= regs)
                return report.fail(ErrorCode::CorruptBytecode, pc);

            switch (op)
            {
            case Op::LOAD_CONST:
                if (decode_bx(ins) >= chunk.constants.size())
                    return report.fail(ErrorCode::CorruptBytecode, pc);
                break;

            case Op::JUMP:
            case Op::JUMP_IF_TRUE:
            case Op::JUMP_IF_FALSE:
            {
                // the loop adds sBx after the fetch has already advanced pc
                const i64 target = (i64)pc + 1 + decode_sbx(ins);
                if (target < 0 || target > (i64)count)
                    return report.fail(ErrorCode::CorruptBytecode, pc);
                break;
            }

            case Op::CALL:
            {
                if (decode_bx(ins) >= program.functions.size())
                    return report.fail(ErrorCode::CorruptBytecode, pc);
                // the callee window starts at A and must begin inside this frame
                if ((u32)decode_a(ins) >= regs)
                    return report.fail(ErrorCode::CorruptBytecode, pc);
                break;
            }

            case Op::CALL_NATIVE:
            {
                if (decode_bx(ins) >= natives_count)
                    return report.fail(ErrorCode::CorruptBytecode, pc);
                if ((u32)decode_a(ins) >= regs)
                    return report.fail(ErrorCode::CorruptBytecode, pc);
                break;
            }

            case Op::RETURN:
                // results are copied from R[A .. A+result_count) to the window start
                if ((u32)decode_a(ins) + proto.result_count > regs)
                    return report.fail(ErrorCode::CorruptBytecode, pc);
                break;

            default:
                break;
            }
        }

        // Falling off the end is prevented by the run-time pc bound below, not
        // here: dead code after a RETURN is legitimate output from a compiler,
        // so the last instruction is not required to be a terminator.
        return Status::success();
    }

    inline Status verify(const Program& program, u32 natives_count, const VmLimits& limits,
                         VerifyError* out = nullptr)
    {
        if (program.functions.size() == 0)
        {
            if (out)
                *out = VerifyError{ErrorCode::CorruptBytecode, 0, 0};
            return Status::fail(ErrorCode::CorruptBytecode);
        }
        if (program.entry >= program.functions.size())
        {
            if (out)
                *out = VerifyError{ErrorCode::CorruptBytecode, program.entry, 0};
            return Status::fail(ErrorCode::CorruptBytecode);
        }
        for (u32 i = 0; i < (u32)program.functions.size(); i++)
        {
            Status s = verify_function(program, i, natives_count, limits, out);
            if (!s.ok())
                return s;
        }
        return Status::success();
    }

} // namespace light
