#include "vm.h"

// computed goto where available, switch fallback on MSVC; each handler ends
// with its own indirect jump so the branch predictor gets one site per opcode
#ifndef LIGHT_COMPUTED_GOTO
#if defined(__GNUC__) || defined(__clang__)
#define LIGHT_COMPUTED_GOTO 1
#else
#define LIGHT_COMPUTED_GOTO 0
#endif
#endif

namespace light
{

    Vm::Vm(const VmConfig& config, const VmLimits& limits)
        : config_(config), limits_(limits), limits_valid_(limits.validate().ok())
    {
        if (limits_valid_)
        {
            regs_.reserve(limits.register_stack_slots);
            for (u32 i = 0; i < limits.register_stack_slots; i++)
                regs_.push_back(Value::nil());
        }
    }

    Status Vm::fail(const Chunk& chunk, ErrorCode code, u32 at)
    {
        error_.code = code;
        error_.pc = at;
        error_.line = at < chunk.lines.size() ? chunk.lines[at] : 0;
        return Status::fail(code);
    }

#define VM_FETCH()                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (pc >= count)                                                                           \
            return fail(chunk, ErrorCode::CorruptBytecode, count ? count - 1 : 0);                 \
        cur = pc++;                                                                                \
        ins = code[cur];                                                                           \
        opcode = (u32)(ins & 0xFF);                                                                \
        if (opcode >= OP_COUNT)                                                                    \
            return fail(chunk, ErrorCode::CorruptBytecode, cur);                                   \
    } while (0)

#if LIGHT_COMPUTED_GOTO
#define VM_CASE(name) L_##name:
#define VM_NEXT                                                                                    \
    do                                                                                             \
    {                                                                                              \
        VM_FETCH();                                                                                \
        __extension__({ goto* dispatch_table[opcode]; });                                          \
    } while (0)
#else
#define VM_CASE(name) case (u32)Op::name:
#define VM_NEXT break
#endif

#define CHECK_INT2(rb, rc)                                                                         \
    if (regs[rb].type != ValueType::Int || regs[rc].type != ValueType::Int)                        \
    return fail(chunk, ErrorCode::TypeError, cur)
#define CHECK_FLOAT2(rb, rc)                                                                       \
    if (regs[rb].type != ValueType::Float || regs[rc].type != ValueType::Float)                    \
    return fail(chunk, ErrorCode::TypeError, cur)
#define CHECK_BOOL(r)                                                                              \
    if (regs[r].type != ValueType::Bool)                                                           \
    return fail(chunk, ErrorCode::TypeError, cur)

    Status Vm::execute(const Chunk& chunk)
    {
        if (!limits_valid_)
            return Status::fail(ErrorCode::InvalidLimits);

        const Instruction* code = chunk.code.data();
        const u32 count = (u32)chunk.code.size();
        Value* regs = regs_.data();

        u32 pc = 0;
        u32 cur = 0;
        Instruction ins = 0;
        u32 opcode = 0;

#if LIGHT_COMPUTED_GOTO
        __extension__ static const void* const dispatch_table[] = {
#define OP(name, format) &&L_##name,
#include "light_opcodes.def"
#undef OP
        };
        VM_NEXT;
#else
        for (;;)
        {
            VM_FETCH();
            switch (opcode)
            {
#endif

        VM_CASE(NOP) { VM_NEXT; }

        VM_CASE(LOAD_NIL)
        {
            regs[decode_a(ins)] = Value::nil();
            VM_NEXT;
        }

        VM_CASE(LOAD_BOOL)
        {
            regs[decode_a(ins)] = Value::boolean(decode_b(ins) != 0);
            VM_NEXT;
        }

        VM_CASE(LOAD_INT16)
        {
            regs[decode_a(ins)] = Value::integer((i64)decode_sbx(ins));
            VM_NEXT;
        }

        VM_CASE(LOAD_CONST)
        {
            u16 bx = decode_bx(ins);
            if (bx >= chunk.constants.size())
                return fail(chunk, ErrorCode::CorruptBytecode, cur);
            regs[decode_a(ins)] = chunk.constants[bx];
            VM_NEXT;
        }

        VM_CASE(MOVE)
        {
            regs[decode_a(ins)] = regs[decode_b(ins)];
            VM_NEXT;
        }

        VM_CASE(ADD_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            // unsigned wrap keeps signed overflow defined
            regs[decode_a(ins)] = Value::integer((i64)((u64)regs[b].as.i + (u64)regs[c].as.i));
            VM_NEXT;
        }

        VM_CASE(SUB_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            regs[decode_a(ins)] = Value::integer((i64)((u64)regs[b].as.i - (u64)regs[c].as.i));
            VM_NEXT;
        }

        VM_CASE(MUL_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            regs[decode_a(ins)] = Value::integer((i64)((u64)regs[b].as.i * (u64)regs[c].as.i));
            VM_NEXT;
        }

        VM_CASE(DIV_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            i64 lhs = regs[b].as.i, rhs = regs[c].as.i;
            if (rhs == 0)
                return fail(chunk, ErrorCode::DivisionByZero, cur);
            // INT64_MIN / -1 would trap on x86; wrap instead
            i64 result = (rhs == -1) ? (i64)(~(u64)lhs + 1) : lhs / rhs;
            regs[decode_a(ins)] = Value::integer(result);
            VM_NEXT;
        }

        VM_CASE(ADD_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            regs[decode_a(ins)] = Value::floating(regs[b].as.f + regs[c].as.f);
            VM_NEXT;
        }

        VM_CASE(SUB_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            regs[decode_a(ins)] = Value::floating(regs[b].as.f - regs[c].as.f);
            VM_NEXT;
        }

        VM_CASE(MUL_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            regs[decode_a(ins)] = Value::floating(regs[b].as.f * regs[c].as.f);
            VM_NEXT;
        }

        VM_CASE(DIV_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            // IEEE: x/0.0 is inf/nan, not an error
            regs[decode_a(ins)] = Value::floating(regs[b].as.f / regs[c].as.f);
            VM_NEXT;
        }

        VM_CASE(NEG_INT)
        {
            u8 b = decode_b(ins);
            if (regs[b].type != ValueType::Int)
                return fail(chunk, ErrorCode::TypeError, cur);
            regs[decode_a(ins)] = Value::integer((i64)(~(u64)regs[b].as.i + 1));
            VM_NEXT;
        }

        VM_CASE(NEG_FLOAT)
        {
            u8 b = decode_b(ins);
            if (regs[b].type != ValueType::Float)
                return fail(chunk, ErrorCode::TypeError, cur);
            regs[decode_a(ins)] = Value::floating(-regs[b].as.f);
            VM_NEXT;
        }

        VM_CASE(EQUAL)
        {
            regs[decode_a(ins)] = Value::boolean(regs[decode_b(ins)].equals(regs[decode_c(ins)]));
            VM_NEXT;
        }

        VM_CASE(LESS_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            regs[decode_a(ins)] = Value::boolean(regs[b].as.i < regs[c].as.i);
            VM_NEXT;
        }

        VM_CASE(LESS_EQUAL_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            regs[decode_a(ins)] = Value::boolean(regs[b].as.i <= regs[c].as.i);
            VM_NEXT;
        }

        VM_CASE(LESS_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            regs[decode_a(ins)] = Value::boolean(regs[b].as.f < regs[c].as.f);
            VM_NEXT;
        }

        VM_CASE(LESS_EQUAL_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            regs[decode_a(ins)] = Value::boolean(regs[b].as.f <= regs[c].as.f);
            VM_NEXT;
        }

        VM_CASE(NOT)
        {
            u8 b = decode_b(ins);
            CHECK_BOOL(b);
            regs[decode_a(ins)] = Value::boolean(!regs[b].as.b);
            VM_NEXT;
        }

        VM_CASE(JUMP)
        {
            i64 target = (i64)pc + decode_sbx(ins);
            if (target < 0 || target > (i64)count)
                return fail(chunk, ErrorCode::CorruptBytecode, cur);
            pc = (u32)target;
            VM_NEXT;
        }

        VM_CASE(JUMP_IF_TRUE)
        {
            u8 a = decode_a(ins);
            CHECK_BOOL(a);
            if (regs[a].as.b)
            {
                i64 target = (i64)pc + decode_sbx(ins);
                if (target < 0 || target > (i64)count)
                    return fail(chunk, ErrorCode::CorruptBytecode, cur);
                pc = (u32)target;
            }
            VM_NEXT;
        }

        VM_CASE(JUMP_IF_FALSE)
        {
            u8 a = decode_a(ins);
            CHECK_BOOL(a);
            if (!regs[a].as.b)
            {
                i64 target = (i64)pc + decode_sbx(ins);
                if (target < 0 || target > (i64)count)
                    return fail(chunk, ErrorCode::CorruptBytecode, cur);
                pc = (u32)target;
            }
            VM_NEXT;
        }

        VM_CASE(RETURN)
        {
            // no call frames yet (M2): ends execution, results stay in registers
            return Status::success();
        }

        VM_CASE(HALT) { return Status::success(); }

#if !LIGHT_COMPUTED_GOTO
    }
}
#endif
}

} // namespace light
