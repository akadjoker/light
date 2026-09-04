#include <stdio.h>

#include "vm.h"

using namespace light;

static int failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static Vm make_vm()
{
    VmConfig config = {};
    VmLimits limits;
    return Vm(config, limits);
}

// ---- integer extremes: every wrap must be defined and exact ----

static void test_int_overflow_wraps()
{
    Chunk c;
    u16 kmax = (u16)c.add_constant(Value::integer(INT64_MAX));
    u16 kmin = (u16)c.add_constant(Value::integer(INT64_MIN));
    c.write(encode_abx(Op::LOAD_CONST, 0, kmax), 1);
    c.write(encode_abx(Op::LOAD_CONST, 1, kmin), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 2, 1), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 3, 2), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 4, -1), 1);
    c.write(encode_abc(Op::ADD_INT, 5, 0, 2), 2); // MAX + 1 -> MIN
    c.write(encode_abc(Op::SUB_INT, 6, 1, 2), 2); // MIN - 1 -> MAX
    c.write(encode_abc(Op::MUL_INT, 7, 0, 3), 2); // MAX * 2 -> -2
    c.write(encode_abc(Op::DIV_INT, 8, 1, 4), 2); // MIN / -1 -> MIN (wrap, no trap)
    c.write(encode_abc(Op::NEG_INT, 9, 1, 0), 2); // -MIN -> MIN (wrap)
    c.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(5).as_int() == INT64_MIN);
    CHECK(vm.reg(6).as_int() == INT64_MAX);
    CHECK(vm.reg(7).as_int() == -2);
    CHECK(vm.reg(8).as_int() == INT64_MIN);
    CHECK(vm.reg(9).as_int() == INT64_MIN);
}

// ---- float specials: NaN, infinities, signed zero ----

static void test_float_specials()
{
    f64 inf_val = 1.0;
    inf_val /= 0.0;
    f64 nan_val = 0.0;
    nan_val /= 0.0;

    Chunk c;
    u16 knan = (u16)c.add_constant(Value::floating(nan_val));
    u16 kinf = (u16)c.add_constant(Value::floating(inf_val));
    u16 kone = (u16)c.add_constant(Value::floating(1.0));
    c.write(encode_abx(Op::LOAD_CONST, 0, knan), 1);
    c.write(encode_abx(Op::LOAD_CONST, 1, kinf), 1);
    c.write(encode_abx(Op::LOAD_CONST, 2, kone), 1);
    c.write(encode_abc(Op::LESS_FLOAT, 3, 0, 2), 2);       // NaN < 1 -> false
    c.write(encode_abc(Op::LESS_FLOAT, 4, 2, 0), 2);       // 1 < NaN -> false
    c.write(encode_abc(Op::LESS_EQUAL_FLOAT, 5, 0, 0), 2); // NaN <= NaN -> false
    c.write(encode_abc(Op::EQUAL, 6, 0, 0), 2);            // NaN == NaN -> false
    c.write(encode_abc(Op::SUB_FLOAT, 7, 1, 1), 3);        // inf - inf -> NaN
    c.write(encode_abc(Op::EQUAL, 8, 7, 7), 3);            // NaN != NaN
    c.write(encode_abc(Op::LESS_FLOAT, 9, 2, 1), 3);       // 1 < inf -> true
    c.write(encode_abc(Op::HALT, 0, 0, 0), 4);

    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    CHECK(!vm.reg(3).as_bool());
    CHECK(!vm.reg(4).as_bool());
    CHECK(!vm.reg(5).as_bool());
    CHECK(!vm.reg(6).as_bool());
    CHECK(!vm.reg(8).as_bool());
    CHECK(vm.reg(9).as_bool());
}

// ---- exact boundaries: registers, frames, windows, constants ----

static void test_register_255_usable()
{
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 255, 21), 1);
    c.write(encode_abc(Op::ADD_INT, 255, 255, 255), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(255).as_int() == 42);
}

static void test_window_at_exact_stack_end()
{
    // stack 512: window at 256 with register_count 256 fits exactly; 257 does not
    VmConfig config = {};
    VmLimits limits;
    limits.register_stack_slots = 512;

    Chunk callee;
    callee.write(encode_abc(Op::RETURN, 0, 0, 0), 1);

    Chunk main_ok;
    main_ok.write(encode_abx(Op::CALL, 255, 1), 1);
    main_ok.write(encode_abx(Op::CALL, 255, 2), 1); // window 255+... second callee 257 regs
    main_ok.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Program prog;
    prog.functions.push_back(Prototype{&main_ok, 0, 0, 256});
    prog.functions.push_back(Prototype{&callee, 0, 0, 256}); // 255 + 256 = 511 <= 512 ok
    prog.functions.push_back(Prototype{&callee, 0, 0, 258}); // 255 + 258 = 513 > 512 fails

    Vm vm(config, limits);
    CHECK(vm.execute(prog).code == ErrorCode::StackOverflow);
    CHECK(vm.last_error().pc == 1); // first call succeeded, second overflowed
}

static void test_frame_depth_exact_limit()
{
    // chain: fn[i] calls fn[i+1]; last one returns. With max_frames = 4,
    // a chain of depth exactly 4 succeeds and depth 5 fails.
    VmConfig config = {};
    VmLimits limits;
    limits.max_frames = 4;

    Chunk leaf;
    leaf.write(encode_abc(Op::RETURN, 0, 0, 0), 1);

    Chunk call_next[4];
    for (u32 i = 0; i < 4; i++)
    {
        call_next[i].write(encode_abx(Op::CALL, 0, (u16)(i + 1)), 1);
        call_next[i].write(encode_abc(Op::RETURN, 0, 0, 0), 1);
    }

    // depth 4: chain0 -> chain1 -> chain2 -> leaf
    {
        Program prog;
        prog.functions.push_back(Prototype{&call_next[0], 0, 0, 4});
        prog.functions.push_back(Prototype{&call_next[1], 0, 0, 4});
        prog.functions.push_back(Prototype{&call_next[2], 0, 0, 4});
        prog.functions.push_back(Prototype{&leaf, 0, 0, 4});
        Vm vm(config, limits);
        CHECK(vm.execute(prog).ok());
    }

    // depth 5: one more link fails with StackOverflow
    {
        Program prog;
        prog.functions.push_back(Prototype{&call_next[0], 0, 0, 4});
        prog.functions.push_back(Prototype{&call_next[1], 0, 0, 4});
        prog.functions.push_back(Prototype{&call_next[2], 0, 0, 4});
        prog.functions.push_back(Prototype{&call_next[3], 0, 0, 4});
        prog.functions.push_back(Prototype{&leaf, 0, 0, 4});
        Vm vm(config, limits);
        CHECK(vm.execute(prog).code == ErrorCode::StackOverflow);
    }
}

static void test_constant_index_boundaries()
{
    Chunk c;
    for (u32 i = 0; i < 300; i++)
        c.add_constant(Value::integer((i64)i));
    c.write(encode_abx(Op::LOAD_CONST, 0, 299), 1); // last valid
    c.write(encode_abx(Op::LOAD_CONST, 1, 300), 1); // first invalid
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Vm vm = make_vm();
    CHECK(vm.execute(c).code == ErrorCode::CorruptBytecode);
    CHECK(vm.reg(0).as_int() == 299); // first load happened
    CHECK(vm.last_error().pc == 1);
}

// ---- adversarial control flow ----

static void test_jump_to_exact_end_is_missing_halt()
{
    Chunk c;
    c.write(encode_asbx(Op::JUMP, 0, 1), 1); // lands exactly at count
    c.write(encode_abc(Op::NOP, 0, 0, 0), 1);

    Vm vm = make_vm();
    CHECK(vm.execute(c).code == ErrorCode::CorruptBytecode);
}

static void test_typed_ops_on_nil_registers()
{
    // registers start nil: every typed op must TypeError, never crash
    Op typed_ops[] = {Op::ADD_INT,   Op::SUB_INT,  Op::MUL_INT,    Op::DIV_INT,
                      Op::ADD_FLOAT, Op::LESS_INT, Op::LESS_FLOAT, Op::NOT};
    for (Op op : typed_ops)
    {
        Chunk c;
        c.write(encode_abc(op, 0, 1, 2), 1);
        c.write(encode_abc(Op::HALT, 0, 0, 0), 1);
        Vm vm = make_vm();
        CHECK(vm.execute(c).code == ErrorCode::TypeError);
    }

    // but EQUAL and MOVE on nil are fine
    Chunk c;
    c.write(encode_abc(Op::MOVE, 3, 4, 0), 1);
    c.write(encode_abc(Op::EQUAL, 5, 3, 4), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);
    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(5).as_bool()); // nil == nil
}

static void test_first_invalid_opcode_value()
{
    Chunk c;
    c.write((Instruction)OP_COUNT, 1); // first byte just past the last valid op
    Vm vm = make_vm();
    CHECK(vm.execute(c).code == ErrorCode::CorruptBytecode);
}

// ---- VM reuse: usable after errors, state behaves predictably ----

static void test_vm_usable_after_error()
{
    Vm vm = make_vm();

    Chunk bad;
    bad.write(encode_asbx(Op::LOAD_INT16, 0, 1), 1);
    bad.write(encode_asbx(Op::LOAD_INT16, 1, 0), 1);
    bad.write(encode_abc(Op::DIV_INT, 2, 0, 1), 1);
    bad.write(encode_abc(Op::HALT, 0, 0, 0), 1);
    CHECK(vm.execute(bad).code == ErrorCode::DivisionByZero);

    Chunk good;
    good.write(encode_asbx(Op::LOAD_INT16, 0, 42), 1);
    good.write(encode_abc(Op::HALT, 0, 0, 0), 1);
    CHECK(vm.execute(good).ok());
    CHECK(vm.reg(0).as_int() == 42);

    // deep recursion overflow, then reuse again
    Chunk self;
    self.write(encode_abx(Op::CALL, 0, 0), 1);
    self.write(encode_abc(Op::RETURN, 0, 0, 0), 1);
    Program p;
    p.functions.push_back(Prototype{&self, 0, 0, 4});
    CHECK(vm.execute(p).code == ErrorCode::StackOverflow);
    CHECK(vm.execute(good).ok());
    CHECK(vm.reg(0).as_int() == 42);
}

static void test_deep_call_chain_result_integrity()
{
    // f(n): if n == 0 return 0; r2 = f(n-1); return r2 + n  => sum 1..n
    Chunk f;
    f.write(encode_asbx(Op::LOAD_INT16, 1, 0), 1);
    f.write(encode_abc(Op::EQUAL, 2, 0, 1), 1);
    f.write(encode_asbx(Op::JUMP_IF_FALSE, 2, 2), 1);
    f.write(encode_asbx(Op::LOAD_INT16, 3, 0), 2);
    f.write(encode_abc(Op::RETURN, 3, 0, 0), 2);
    f.write(encode_asbx(Op::LOAD_INT16, 3, 1), 3);
    f.write(encode_abc(Op::SUB_INT, 4, 0, 3), 3);
    f.write(encode_abx(Op::CALL, 4, 0), 3);
    f.write(encode_abc(Op::ADD_INT, 5, 4, 0), 4);
    f.write(encode_abc(Op::RETURN, 5, 0, 0), 4);

    Chunk main_chunk;
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 0, 200), 1);
    main_chunk.write(encode_abx(Op::CALL, 0, 0), 1);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Program prog;
    prog.functions.push_back(Prototype{&f, 1, 1, 8});
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 8});
    prog.entry = 1;

    Vm vm = make_vm();
    CHECK(vm.execute(prog).ok());
    CHECK(vm.reg(0).as_int() == 200 * 201 / 2);
}

// ---- deterministic bytecode fuzz: must never crash or hang ----

struct Lcg
{
    u64 state;
    u32 next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return (u32)(state >> 32);
    }
};

static void test_fuzz_random_bytecode()
{
    Lcg rng{0xC0FFEEull};
    u32 executed_ok = 0, errored = 0;

    for (u32 prog_i = 0; prog_i < 2000; prog_i++)
    {
        Chunk c;
        u16 kint = (u16)c.add_constant(Value::integer(7));
        u16 kfloat = (u16)c.add_constant(Value::floating(1.5));
        (void)kint;
        (void)kfloat;

        u32 len = 4 + (rng.next() % 60);
        for (u32 i = 0; i < len; i++)
        {
            u32 raw = rng.next();
            u8 op = (u8)(raw % OP_COUNT);
            Instruction ins = (raw & 0xFFFFFF00u) | op;
            // forward-only jumps guarantee termination
            if (op == (u8)Op::JUMP || op == (u8)Op::JUMP_IF_TRUE || op == (u8)Op::JUMP_IF_FALSE)
                ins = encode_asbx((Op)op, decode_a(ins), (i16)(rng.next() % 16));
            c.write(ins, i + 1);
        }
        c.write(encode_abc(Op::HALT, 0, 0, 0), len + 1);

        Vm vm = make_vm();
        Status s = vm.execute(c);
        if (s.ok())
            executed_ok++;
        else
        {
            errored++;
            CHECK(vm.last_error().code == s.code);
            CHECK(vm.last_error().pc <= len); // error location always inside the chunk
        }
    }

    // sanity: the generator produces both outcomes, so both paths are exercised
    CHECK(executed_ok > 0);
    CHECK(errored > 0);
    printf("fuzz: %u ok, %u errored, 0 crashes\n", executed_ok, errored);
}

int main()
{
    test_int_overflow_wraps();
    test_float_specials();
    test_register_255_usable();
    test_window_at_exact_stack_end();
    test_frame_depth_exact_limit();
    test_constant_index_boundaries();
    test_jump_to_exact_end_is_missing_halt();
    test_typed_ops_on_nil_registers();
    test_first_invalid_opcode_value();
    test_vm_usable_after_error();
    test_deep_call_chain_result_integrity();
    test_fuzz_random_bytecode();

    if (failures == 0)
    {
        printf("torture: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "torture: %d failure(s)\n", failures);
    return 1;
}
