#include "verify.h"
#include "vm.h"

#include "test_vm_support.h"

#include <stdio.h>

using namespace light;
using light::testing::make_vm;

static int failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

// ---- register operands must stay inside the frame's own window ----

// regression: a function declaring 4 registers could still write R[200],
// past the end of the register stack once its base was high enough
static void test_register_operand_beyond_window_rejected()
{
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 200, 1), 1); // A = 200, register_count = 4
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).code == ErrorCode::CorruptBytecode);
}

// the same, reached through a call so the write lands past the stack end
static void test_callee_register_operand_beyond_stack_rejected()
{
    Chunk callee;
    callee.write(encode_asbx(Op::LOAD_INT16, 255, 1), 1); // register_count says 4
    callee.write(encode_abc(Op::RETURN, 0, 0, 0), 2);

    Chunk main_chunk;
    main_chunk.write(encode_abx(Op::CALL, 10, 1), 1);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    VmLimits limits;
    limits.register_stack_slots = 256;

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 16});
    prog.functions.push_back(Prototype{&callee, 0, 0, 4});

    Vm vm = make_vm(limits);
    CHECK(vm.execute(prog).code == ErrorCode::CorruptBytecode);
}

static void test_b_and_c_operands_checked()
{
    Chunk c;
    c.write(encode_abc(Op::ADD_INT, 0, 99, 1), 1); // B = 99, register_count = 8
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 8});
    CHECK(make_vm().execute(prog).code == ErrorCode::CorruptBytecode);

    Chunk c2;
    c2.write(encode_abc(Op::ADD_INT, 0, 1, 99), 1); // C = 99
    c2.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog2;
    prog2.functions.push_back(Prototype{&c2, 0, 0, 8});
    CHECK(make_vm().execute(prog2).code == ErrorCode::CorruptBytecode);
}

// A at exactly register_count - 1 is the last legal register
static void test_last_register_accepted()
{
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 7, 42), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 8});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).ok());
    CHECK(vm.reg(7).as_int() == 42);
}

// LOAD_BOOL's B is an immediate, not a register: B = 1 must stay legal
// even when register_count is 1
static void test_immediate_operands_not_treated_as_registers()
{
    Chunk c;
    c.write(encode_abc(Op::LOAD_BOOL, 0, 1, 0), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 1});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).ok());
    CHECK(vm.reg(0).as_bool());
}

// ---- jumps ----

static void test_jump_out_of_range_rejected()
{
    Chunk c;
    c.write(encode_asbx(Op::JUMP, 0, 1000), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm().execute(prog).code == ErrorCode::CorruptBytecode);

    Chunk c2;
    c2.write(encode_asbx(Op::JUMP, 0, -100), 1);
    c2.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog2;
    prog2.functions.push_back(Prototype{&c2, 0, 0, 4});
    CHECK(make_vm().execute(prog2).code == ErrorCode::CorruptBytecode);
}

// a backward jump to itself is well-formed; only the budget stops it
static void test_self_jump_is_valid_bytecode()
{
    Chunk c;
    c.write(encode_asbx(Op::JUMP, 0, -1), 1);

    VmLimits limits;
    limits.instruction_budget = 1000;

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm(limits).execute(prog).code == ErrorCode::InstructionLimit);
}

// ---- table indices ----

static void test_bad_constant_index_rejected()
{
    Chunk c;
    c.add_constant(Value::integer(1));
    c.write(encode_abx(Op::LOAD_CONST, 0, 5), 1); // only 1 constant exists
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm().execute(prog).code == ErrorCode::CorruptBytecode);
}

static void test_bad_native_index_rejected()
{
    Chunk c;
    c.write(encode_abx(Op::CALL_NATIVE, 0, 3), 1); // no natives registered
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm().execute(prog).code == ErrorCode::CorruptBytecode);
}

static void test_unknown_opcode_rejected()
{
    Chunk c;
    c.write((Instruction)0xFF, 1); // opcode 0xFF does not exist
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm().execute(prog).code == ErrorCode::CorruptBytecode);
}

// ---- structural requirements ----

static void test_empty_chunk_rejected()
{
    Chunk c;
    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm().execute(prog).code == ErrorCode::CorruptBytecode);
}

static void test_null_chunk_rejected()
{
    Program prog;
    prog.functions.push_back(Prototype{nullptr, 0, 0, 4});
    CHECK(make_vm().execute(prog).code == ErrorCode::CorruptBytecode);
}

// every function is verified, not just the entry point
static void test_unreachable_function_still_verified()
{
    Chunk main_chunk;
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Chunk never_called;
    never_called.write(encode_asbx(Op::LOAD_INT16, 200, 1), 1); // A beyond window
    never_called.write(encode_abc(Op::RETURN, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 4});
    prog.functions.push_back(Prototype{&never_called, 0, 0, 4});

    CHECK(make_vm().execute(prog).code == ErrorCode::CorruptBytecode);
}

// dead code after RETURN is legal compiler output and must verify
static void test_dead_code_after_return_accepted()
{
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 42), 1);
    c.write(encode_abc(Op::RETURN, 0, 0, 0), 2);
    c.write(encode_asbx(Op::LOAD_INT16, 0, 99), 3); // never reached

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).ok());
    CHECK(vm.reg(0).as_int() == 42);
}

// lines is read when reporting a runtime error, so it must match code 1:1
static void test_mismatched_line_table_rejected()
{
    Chunk c;
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);
    c.lines.push_back(2); // one line too many

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm().execute(prog).code == ErrorCode::CorruptBytecode);
}

// ---- limits enforced at verify time ----

static void test_too_many_constants_rejected()
{
    Chunk c;
    VmLimits limits;
    limits.max_constants = 4;
    for (u32 i = 0; i < 5; i++)
        c.add_constant(Value::integer((i64)i));
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm(limits).execute(prog).code == ErrorCode::TooManyConstants);
}

static void test_bytecode_too_large_rejected()
{
    Chunk c;
    VmLimits limits;
    limits.max_instructions = 4;
    for (u32 i = 0; i < 5; i++)
        c.write(encode_abc(Op::NOP, 0, 0, 0), 1);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm(limits).execute(prog).code == ErrorCode::BytecodeTooLarge);
}

// result_count must fit the window RETURN copies results into
static void test_result_count_beyond_window_rejected()
{
    Chunk c;
    c.write(encode_abc(Op::RETURN, 0, 0, 0), 1);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 8, 4}); // 8 results, 4 registers
    CHECK(make_vm().execute(prog).code == ErrorCode::CorruptBytecode);
}

// ---- instruction budget ----

static void test_budget_counts_across_calls()
{
    // callee runs 2 instructions per call; a tight budget must trip inside it
    Chunk callee;
    callee.write(encode_abc(Op::NOP, 0, 0, 0), 1);
    callee.write(encode_abc(Op::RETURN, 0, 0, 0), 2);

    Chunk main_chunk;
    for (u32 i = 0; i < 20; i++)
        main_chunk.write(encode_abx(Op::CALL, 0, 1), 1);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    VmLimits limits;
    limits.instruction_budget = 10;

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 8});
    prog.functions.push_back(Prototype{&callee, 0, 0, 8});

    CHECK(make_vm(limits).execute(prog).code == ErrorCode::InstructionLimit);
}

// the budget is per execute() call, not shared across runs
static void test_budget_resets_between_runs()
{
    Chunk c;
    for (u32 i = 0; i < 5; i++)
        c.write(encode_abc(Op::NOP, 0, 0, 0), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    VmLimits limits;
    limits.instruction_budget = 10;

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});

    Vm vm = make_vm(limits);
    CHECK(vm.execute(prog).ok());
    CHECK(vm.execute(prog).ok()); // would fail if the budget carried over
}

// max_instructions bounds code size, instruction_budget bounds run time:
// a chunk larger than the budget is legal as long as it does not loop
static void test_budget_and_size_are_independent()
{
    Chunk c;
    for (u32 i = 0; i < 50; i++)
        c.write(encode_abc(Op::NOP, 0, 0, 0), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    VmLimits limits;
    limits.max_instructions = 100;   // chunk of 51 fits
    limits.instruction_budget = 500; // and 51 executed instructions fit too

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm(limits).execute(prog).ok());

    // shrinking only the budget stops the same chunk
    limits.instruction_budget = 10;
    CHECK(make_vm(limits).execute(prog).code == ErrorCode::InstructionLimit);
}

// the default configuration runs unbounded, so existing hosts are unaffected
static void test_default_budget_is_unlimited()
{
    Chunk c;
    for (u32 i = 0; i < 1000; i++)
        c.write(encode_abc(Op::NOP, 0, 0, 0), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 4});
    CHECK(make_vm().execute(prog).ok());
}

// ---- allocation failure is reported, never fatal ----

namespace
{
    struct FailingAlloc
    {
        u32 allow;    // succeed this many times, then refuse
        u32 live;     // outstanding allocations, to prove nothing leaks
        static void* fn(void* ud, void* ptr, usize, usize new_size)
        {
            FailingAlloc* self = (FailingAlloc*)ud;
            if (new_size == 0)
            {
                if (ptr)
                    self->live--;
                free(ptr);
                return nullptr;
            }
            if (self->allow == 0)
                return nullptr;
            self->allow--;
            void* p = realloc(ptr, new_size);
            if (p)
                self->live++;
            return p;
        }
    };
} // namespace

// the register stack is the first allocation: refusing it must not be fatal
static void test_oom_on_register_stack_reported()
{
    FailingAlloc alloc{0, 0};
    VmConfig config = {};
    config.alloc = FailingAlloc::fn;
    config.alloc_ud = &alloc;

    Vm vm;
    CHECK(Vm::create(config, VmLimits{}, &vm).code == ErrorCode::OutOfMemory);
    CHECK(alloc.live == 0);
}

// the frame stack is the second: the register stack must be handed back
static void test_oom_on_frame_stack_releases_registers()
{
    FailingAlloc alloc{1, 0}; // first allocation succeeds, second fails
    VmConfig config = {};
    config.alloc = FailingAlloc::fn;
    config.alloc_ud = &alloc;

    Vm vm;
    CHECK(Vm::create(config, VmLimits{}, &vm).code == ErrorCode::OutOfMemory);
    CHECK(alloc.live == 0); // the register stack was freed on the way out
}

// the host's allocator is the one actually used, and everything comes back
static void test_vm_uses_host_allocator()
{
    FailingAlloc alloc{16, 0};
    VmConfig config = {};
    config.alloc = FailingAlloc::fn;
    config.alloc_ud = &alloc;

    {
        Vm vm;
        CHECK(Vm::create(config, VmLimits{}, &vm).ok());
        CHECK(alloc.live == 2); // register stack + frame stack

        Chunk c;
        c.write(encode_asbx(Op::LOAD_INT16, 0, 42), 1);
        c.write(encode_abc(Op::HALT, 0, 0, 0), 2);
        Program prog;
        prog.functions.push_back(Prototype{&c, 0, 0, 4});
        CHECK(vm.execute(prog).ok());
        CHECK(vm.reg(0).as_int() == 42);
    }
    CHECK(alloc.live == 0); // the destructor returned both stacks
}

int main()
{
    test_register_operand_beyond_window_rejected();
    test_callee_register_operand_beyond_stack_rejected();
    test_b_and_c_operands_checked();
    test_last_register_accepted();
    test_immediate_operands_not_treated_as_registers();

    test_jump_out_of_range_rejected();
    test_self_jump_is_valid_bytecode();

    test_bad_constant_index_rejected();
    test_bad_native_index_rejected();
    test_unknown_opcode_rejected();

    test_empty_chunk_rejected();
    test_null_chunk_rejected();
    test_unreachable_function_still_verified();
    test_dead_code_after_return_accepted();
    test_mismatched_line_table_rejected();

    test_too_many_constants_rejected();
    test_bytecode_too_large_rejected();
    test_result_count_beyond_window_rejected();

    test_oom_on_register_stack_reported();
    test_oom_on_frame_stack_releases_registers();
    test_vm_uses_host_allocator();

    test_budget_counts_across_calls();
    test_budget_and_size_are_independent();
    test_default_budget_is_unlimited();
    test_budget_resets_between_runs();

    printf("verify: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
