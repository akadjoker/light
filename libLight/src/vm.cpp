#include "vm.h"
#include "verify.h"

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

    void Fiber::init(u32 regs_n, u32 frames_n)
    {
        ct::HeapAlloc alloc;
        regs = (Value*)alloc.allocate(regs_n * sizeof(Value), alignof(Value));
        frames = (CallFrame*)alloc.allocate(frames_n * sizeof(CallFrame), alignof(CallFrame));
        reg_count = regs_n;
        frame_capacity = frames_n;
        frame_depth = 0;
        for (u32 i = 0; i < regs_n; i++)
            regs[i] = Value::nil();
    }

    void Fiber::release()
    {
        ct::HeapAlloc alloc;
        if (regs)
            alloc.deallocate(regs, reg_count * sizeof(Value));
        if (frames)
            alloc.deallocate(frames, frame_capacity * sizeof(CallFrame));
        regs = nullptr;
        frames = nullptr;
        reg_count = 0;
        frame_capacity = 0;
        frame_depth = 0;
    }

    Vm::Vm(const VmConfig& config, const VmLimits& limits)
        : config_(config), limits_(limits), limits_valid_(limits.validate().ok())
    {
        if (limits_valid_)
            main_.init(limits.register_stack_slots, limits.max_frames);
    }

    Vm::Vm(Vm&& other)
        : config_(other.config_), limits_(other.limits_), limits_valid_(other.limits_valid_),
          main_(other.main_), natives_(static_cast<ct::Vector<NativeDescriptor>&&>(other.natives_)),
          error_(other.error_)
    {
        other.main_ = Fiber{};
    }

    Vm::~Vm() { main_.release(); }

    Status Vm::fail(const Chunk& chunk, ErrorCode code, u32 at)
    {
        error_.code = code;
        error_.pc = at;
        error_.line = at < chunk.lines.size() ? chunk.lines[at] : 0;
        return Status::fail(code);
    }

    u32 Vm::register_native(const char* name, NativeFn fn, void* ud, u8 param_count,
                            u8 result_count)
    {
        NativeDescriptor d;
        d.fn.checked = fn;
        d.ud = ud;
        d.name = name;
        d.param_count = param_count;
        d.result_count = result_count;
        d.can_fail = true;
        natives_.push_back(d);
        return (u32)natives_.size() - 1;
    }

    u32 Vm::register_native(const char* name, NativeFnSimple fn, void* ud, u8 param_count,
                            u8 result_count)
    {
        NativeDescriptor d;
        d.fn.simple = fn;
        d.ud = ud;
        d.name = name;
        d.param_count = param_count;
        d.result_count = result_count;
        d.can_fail = false;
        natives_.push_back(d);
        return (u32)natives_.size() - 1;
    }

    Status Vm::execute(const Chunk& chunk)
    {
        Prototype entry{&chunk, 0, 0, (u16)REGISTERS_PER_FUNCTION};
        Program program;
        program.functions.push_back(entry);
        program.entry = 0;
        return execute(program);
    }

// verify() guarantees every opcode decodes and every operand is in range, so
// what remains per instruction is the budget and the end-of-chunk bound:
// dead code after a RETURN is legal, so running off the end is still possible
#define VM_FETCH()                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (budget-- == 0)                                                                         \
            return fail(*chunk, ErrorCode::InstructionLimit, cur);                                 \
        if (pc >= count)                                                                           \
            return fail(*chunk, ErrorCode::CorruptBytecode, count ? count - 1 : 0);                \
        cur = pc++;                                                                                \
        ins = code[cur];                                                                           \
        opcode = (u32)(ins & 0xFF);                                                                \
    } while (0)

#define VM_RELOAD_FRAME()                                                                          \
    do                                                                                             \
    {                                                                                              \
        const CallFrame& fr = fiber.frames[fiber.frame_depth - 1];                                 \
        proto = fr.proto;                                                                          \
        chunk = proto->chunk;                                                                      \
        code = chunk->code.data();                                                                 \
        count = (u32)chunk->code.size();                                                           \
        base = fr.base;                                                                            \
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
    if (regs[base + (rb)].type != ValueType::Int || regs[base + (rc)].type != ValueType::Int)      \
    return fail(*chunk, ErrorCode::TypeError, cur)
#define CHECK_FLOAT2(rb, rc)                                                                       \
    if (regs[base + (rb)].type != ValueType::Float || regs[base + (rc)].type != ValueType::Float)  \
    return fail(*chunk, ErrorCode::TypeError, cur)
#define CHECK_BOOL(r)                                                                              \
    if (regs[base + (r)].type != ValueType::Bool)                                                  \
    return fail(*chunk, ErrorCode::TypeError, cur)

#define RA regs[base + decode_a(ins)]
#define RB regs[base + decode_b(ins)]
#define RC regs[base + decode_c(ins)]

    namespace
    {
        struct ExecGuard
        {
            bool* flag;
            ~ExecGuard() { *flag = false; }
        };
    } // namespace

    Status Vm::execute(const Program& program)
    {
        if (!limits_valid_)
            return Status::fail(ErrorCode::InvalidLimits);
        if (executing_)
        {
            error_ = VmError{ErrorCode::ReentrantExecution, 0, 0};
            return Status::fail(ErrorCode::ReentrantExecution);
        }
        if (program.entry >= program.functions.size())
        {
            error_ = VmError{ErrorCode::CorruptBytecode, 0, 0};
            return Status::fail(ErrorCode::CorruptBytecode);
        }
        Status verified = verify(program, (u32)natives_.size(), limits_, nullptr);
        if (!verified.ok())
        {
            error_ = VmError{verified.code, 0, 0};
            return verified;
        }
        executing_ = true;
        ExecGuard guard{&executing_};

        // verify() bounds code size; this bounds how long a loop may run.
        // 0 means unlimited, so start the counter high enough never to trip.
        u64 budget = limits_.instruction_budget ? limits_.instruction_budget : ~(u64)0;

        Fiber& fiber = main_;
        Value* regs = fiber.regs;
        const u32 reg_count_total = fiber.reg_count;

        const Prototype* entry_proto = &program.functions[program.entry];
        if (entry_proto->register_count > reg_count_total)
        {
            error_ = VmError{ErrorCode::StackOverflow, 0, 0};
            return Status::fail(ErrorCode::StackOverflow);
        }
        fiber.frames[0] = CallFrame{entry_proto, 0, 0};
        fiber.frame_depth = 1;

        const Prototype* proto = nullptr;
        const Chunk* chunk = nullptr;
        const Instruction* code = nullptr;
        u32 count = 0;
        u32 base = 0;
        VM_RELOAD_FRAME();

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
            RA = Value::nil();
            VM_NEXT;
        }

        VM_CASE(LOAD_BOOL)
        {
            RA = Value::boolean(decode_b(ins) != 0);
            VM_NEXT;
        }

        VM_CASE(LOAD_INT16)
        {
            RA = Value::integer((i64)decode_sbx(ins));
            VM_NEXT;
        }

        VM_CASE(LOAD_CONST)
        {
            RA = chunk->constants[decode_bx(ins)];
            VM_NEXT;
        }

        VM_CASE(MOVE)
        {
            RA = RB;
            VM_NEXT;
        }

        VM_CASE(ADD_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            // unsigned wrap keeps signed overflow defined
            RA = Value::integer((i64)((u64)regs[base + b].as.i + (u64)regs[base + c].as.i));
            VM_NEXT;
        }

        VM_CASE(SUB_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            RA = Value::integer((i64)((u64)regs[base + b].as.i - (u64)regs[base + c].as.i));
            VM_NEXT;
        }

        VM_CASE(MUL_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            RA = Value::integer((i64)((u64)regs[base + b].as.i * (u64)regs[base + c].as.i));
            VM_NEXT;
        }

        VM_CASE(DIV_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            i64 lhs = regs[base + b].as.i, rhs = regs[base + c].as.i;
            if (rhs == 0)
                return fail(*chunk, ErrorCode::DivisionByZero, cur);
            // INT64_MIN / -1 would trap on x86; wrap instead
            i64 result = (rhs == -1) ? (i64)(~(u64)lhs + 1) : lhs / rhs;
            RA = Value::integer(result);
            VM_NEXT;
        }

        VM_CASE(ADD_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            RA = Value::floating(regs[base + b].as.f + regs[base + c].as.f);
            VM_NEXT;
        }

        VM_CASE(SUB_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            RA = Value::floating(regs[base + b].as.f - regs[base + c].as.f);
            VM_NEXT;
        }

        VM_CASE(MUL_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            RA = Value::floating(regs[base + b].as.f * regs[base + c].as.f);
            VM_NEXT;
        }

        VM_CASE(DIV_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            // IEEE: x/0.0 is inf/nan, not an error
            RA = Value::floating(regs[base + b].as.f / regs[base + c].as.f);
            VM_NEXT;
        }

        VM_CASE(NEG_INT)
        {
            u8 b = decode_b(ins);
            if (regs[base + b].type != ValueType::Int)
                return fail(*chunk, ErrorCode::TypeError, cur);
            RA = Value::integer((i64)(~(u64)regs[base + b].as.i + 1));
            VM_NEXT;
        }

        VM_CASE(NEG_FLOAT)
        {
            u8 b = decode_b(ins);
            if (regs[base + b].type != ValueType::Float)
                return fail(*chunk, ErrorCode::TypeError, cur);
            RA = Value::floating(-regs[base + b].as.f);
            VM_NEXT;
        }

        VM_CASE(EQUAL)
        {
            RA = Value::boolean(RB.equals(RC));
            VM_NEXT;
        }

        VM_CASE(LESS_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            RA = Value::boolean(regs[base + b].as.i < regs[base + c].as.i);
            VM_NEXT;
        }

        VM_CASE(LESS_EQUAL_INT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_INT2(b, c);
            RA = Value::boolean(regs[base + b].as.i <= regs[base + c].as.i);
            VM_NEXT;
        }

        VM_CASE(LESS_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            RA = Value::boolean(regs[base + b].as.f < regs[base + c].as.f);
            VM_NEXT;
        }

        VM_CASE(LESS_EQUAL_FLOAT)
        {
            u8 b = decode_b(ins), c = decode_c(ins);
            CHECK_FLOAT2(b, c);
            RA = Value::boolean(regs[base + b].as.f <= regs[base + c].as.f);
            VM_NEXT;
        }

        VM_CASE(NOT)
        {
            u8 b = decode_b(ins);
            CHECK_BOOL(b);
            RA = Value::boolean(!regs[base + b].as.b);
            VM_NEXT;
        }

        VM_CASE(JUMP)
        {
            pc = (u32)((i64)pc + decode_sbx(ins));
            VM_NEXT;
        }

        VM_CASE(JUMP_IF_TRUE)
        {
            u8 a = decode_a(ins);
            CHECK_BOOL(a);
            if (regs[base + a].as.b)
            {
                pc = (u32)((i64)pc + decode_sbx(ins));
            }
            VM_NEXT;
        }

        VM_CASE(JUMP_IF_FALSE)
        {
            u8 a = decode_a(ins);
            CHECK_BOOL(a);
            if (!regs[base + a].as.b)
            {
                pc = (u32)((i64)pc + decode_sbx(ins));
            }
            VM_NEXT;
        }

        VM_CASE(CALL)
        {
            const Prototype* callee = &program.functions[decode_bx(ins)];
            u32 new_base = base + decode_a(ins);
            if (fiber.frame_depth >= fiber.frame_capacity)
                return fail(*chunk, ErrorCode::StackOverflow, cur);
            if (new_base + callee->register_count > reg_count_total)
                return fail(*chunk, ErrorCode::StackOverflow, cur);

            fiber.frames[fiber.frame_depth - 1].pc = pc;
            fiber.frames[fiber.frame_depth++] = CallFrame{callee, new_base, 0};
            VM_RELOAD_FRAME();
            pc = 0;
            VM_NEXT;
        }

        VM_CASE(CALL_NATIVE)
        {
            const NativeDescriptor& native = natives_[decode_bx(ins)];
            u32 window = base + decode_a(ins);
            u8 span =
                native.param_count > native.result_count ? native.param_count : native.result_count;
            if (window + span > reg_count_total)
                return fail(*chunk, ErrorCode::StackOverflow, cur);

            if (native.can_fail)
            {
                Status status = native.fn.checked(*this, native.ud, &regs[window]);
                if (!status.ok())
                    return fail(*chunk, status.code, cur);
            }
            else
            {
                native.fn.simple(*this, native.ud, &regs[window]);
            }
            VM_NEXT;
        }

        VM_CASE(RETURN)
        {
            u8 a = decode_a(ins);
            u8 n = proto->result_count;
            // results land at the window start, exactly where the caller expects
            for (u8 i = 0; i < n; i++)
                regs[base + i] = regs[base + a + i];

            if (fiber.frame_depth == 1)
                return Status::success();
            fiber.frame_depth--;
            VM_RELOAD_FRAME();
            pc = fiber.frames[fiber.frame_depth - 1].pc;
            VM_NEXT;
        }

        VM_CASE(HALT) { return Status::success(); }

#if !LIGHT_COMPUTED_GOTO
    }
}
#endif
}

} // namespace light
