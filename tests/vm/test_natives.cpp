#include "test_vm_support.h"

#include <stdio.h>

#include "vm.h"

using namespace light;
using light::testing::make_vm;

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

static Status native_add(Vm& vm, void* ud, Value* slots)
{
    (void)vm;
    (void)ud;
    if (!slots[0].is_int() || !slots[1].is_int())
        return Status::fail(ErrorCode::TypeError);
    slots[0] = Value::integer(slots[0].as_int() + slots[1].as_int());
    return Status::success();
}

static Status native_min_max(Vm& vm, void* ud, Value* slots)
{
    (void)vm;
    (void)ud;
    i64 a = slots[0].as_int(), b = slots[1].as_int();
    slots[0] = Value::integer(a < b ? a : b);
    slots[1] = Value::integer(a < b ? b : a);
    return Status::success();
}

static Status native_accumulate(Vm& vm, void* ud, Value* slots)
{
    (void)vm;
    i64* total = (i64*)ud;
    *total += slots[0].as_int();
    return Status::success();
}

static Status native_always_fails(Vm& vm, void* ud, Value* slots)
{
    (void)vm;
    (void)ud;
    (void)slots;
    return Status::fail(ErrorCode::InvalidHandle);
}

static void test_native_add()
{
    Vm vm = make_vm();
    u32 add_idx = vm.register_native("add", native_add, nullptr, 2, 1);
    CHECK(add_idx == 0);

    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 30), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 1, 12), 1);
    c.write(encode_abx(Op::CALL_NATIVE, 0, (u16)add_idx), 2);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(0).as_int() == 42);
}

static void test_native_multiple_results()
{
    Vm vm = make_vm();
    u32 idx = vm.register_native("min_max", native_min_max, nullptr, 2, 2);

    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 2, 9), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 3, 4), 1);
    c.write(encode_abx(Op::CALL_NATIVE, 2, (u16)idx), 2);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(2).as_int() == 4);
    CHECK(vm.reg(3).as_int() == 9);
}

static void test_native_userdata_context()
{
    i64 total = 0;
    Vm vm = make_vm();
    u32 idx = vm.register_native("accumulate", native_accumulate, &total, 1, 0);

    // loop i = 1..5: accumulate(i)
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 1), 1); // i
    c.write(encode_asbx(Op::LOAD_INT16, 1, 5), 1); // limit
    c.write(encode_asbx(Op::LOAD_INT16, 2, 1), 1); // step
    c.write(encode_abc(Op::LESS_EQUAL_INT, 3, 0, 1), 2);
    c.write(encode_asbx(Op::JUMP_IF_FALSE, 3, 4), 2);
    c.write(encode_abc(Op::MOVE, 4, 0, 0), 3);
    c.write(encode_abx(Op::CALL_NATIVE, 4, (u16)idx), 3);
    c.write(encode_abc(Op::ADD_INT, 0, 0, 2), 4);
    c.write(encode_asbx(Op::JUMP, 0, -6), 4);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 5);

    CHECK(vm.execute(c).ok());
    CHECK(total == 15);
}

static void test_native_error_propagates()
{
    Vm vm = make_vm();
    u32 idx = vm.register_native("fails", native_always_fails, nullptr, 0, 0);

    Chunk c;
    c.write(encode_abc(Op::NOP, 0, 0, 0), 1);
    c.write(encode_abx(Op::CALL_NATIVE, 0, (u16)idx), 7);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 8);

    CHECK(vm.execute(c).code == ErrorCode::InvalidHandle);
    CHECK(vm.last_error().pc == 1);
    CHECK(vm.last_error().line == 7);
}

static void test_native_called_from_function()
{
    Vm vm = make_vm();
    u32 add_idx = vm.register_native("add", native_add, nullptr, 2, 1);

    // double_sum(a, b): r2 = add(a, b) via native, return r2 + r2
    Chunk fn;
    fn.write(encode_abc(Op::MOVE, 2, 0, 0), 1);
    fn.write(encode_abc(Op::MOVE, 3, 1, 0), 1);
    fn.write(encode_abx(Op::CALL_NATIVE, 2, (u16)add_idx), 2);
    fn.write(encode_abc(Op::ADD_INT, 4, 2, 2), 3);
    fn.write(encode_abc(Op::RETURN, 4, 0, 0), 3);

    Chunk main_chunk;
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 0, 10), 1);
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 1, 11), 1);
    main_chunk.write(encode_abx(Op::CALL, 0, 1), 2);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 8});
    prog.functions.push_back(Prototype{&fn, 2, 1, 8});

    CHECK(vm.execute(prog).ok());
    CHECK(vm.reg(0).as_int() == 42);
}

static void native_simple_scale(Vm& vm, void* ud, Value* slots)
{
    (void)vm;
    i64 factor = *(i64*)ud;
    slots[0] = Value::integer(slots[0].as_int() * factor);
}

static void test_simple_native_no_status()
{
    i64 factor = 3;
    Vm vm = make_vm();
    u32 idx = vm.register_native("scale", native_simple_scale, &factor, 1, 1);

    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 14), 1);
    c.write(encode_abx(Op::CALL_NATIVE, 0, (u16)idx), 2);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(0).as_int() == 42);
}

static void test_mixed_native_kinds()
{
    i64 factor = 2;
    Vm vm = make_vm();
    u32 add_idx = vm.register_native("add", native_add, nullptr, 2, 1);
    u32 scale_idx = vm.register_native("scale", native_simple_scale, &factor, 1, 1);

    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 20), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 1, 1), 1);
    c.write(encode_abx(Op::CALL_NATIVE, 0, (u16)add_idx), 2);
    c.write(encode_abx(Op::CALL_NATIVE, 0, (u16)scale_idx), 3);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 4);

    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(0).as_int() == 42);
}

static Status native_tries_reentry(Vm& vm, void* ud, Value* slots)
{
    (void)slots;
    Chunk* inner = (Chunk*)ud;
    Status s = vm.execute(*inner);
    if (s.code != ErrorCode::ReentrantExecution)
        return Status::fail(ErrorCode::TypeError); // wrong outcome, fail the test via VM
    return Status::success();
}

static void test_reentrant_execute_rejected()
{
    Chunk inner;
    inner.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Vm vm = make_vm();
    u32 idx = vm.register_native("reenter", native_tries_reentry, &inner, 0, 0);

    Chunk outer;
    outer.write(encode_abx(Op::CALL_NATIVE, 0, (u16)idx), 1);
    outer.write(encode_asbx(Op::LOAD_INT16, 0, 42), 2);
    outer.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    // inner execute fails cleanly, outer execution continues unharmed
    CHECK(vm.execute(outer).ok());
    CHECK(vm.reg(0).as_int() == 42);
}

static void test_bad_native_index()
{
    Vm vm = make_vm();

    Chunk c;
    c.write(encode_abx(Op::CALL_NATIVE, 0, 99), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    CHECK(vm.execute(c).code == ErrorCode::CorruptBytecode);
}

// ---- documented contracts ----

// param_count / result_count are trusted, not enforced: this pins the
// behaviour the header describes so a future change to it is deliberate
static Status native_writes_past_results(Vm&, void*, Value* slots)
{
    for (u32 i = 0; i < 4; i++)
        slots[i] = Value::integer(777);
    return Status::success();
}

static void test_native_result_count_is_trusted_not_enforced()
{
    Chunk c;
    c.write(encode_abx(Op::CALL_NATIVE, 0, 0), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Vm vm = make_vm();
    vm.register_native("over", native_writes_past_results, nullptr, 1, 1);

    Program prog;
    prog.functions.push_back(Prototype{&c, 0, 0, 8});
    CHECK(vm.execute(prog).ok());
    // the native wrote 4 slots while declaring 1; the VM did not stop it
    CHECK(vm.reg(3).as_int() == 777);
}

// registers carry over between runs, which is how a host reads results back
static void test_registers_persist_across_runs()
{
    Chunk first;
    first.write(encode_asbx(Op::LOAD_INT16, 3, 1234), 1);
    first.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Chunk second;
    second.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Program prog_first;
    prog_first.functions.push_back(Prototype{&first, 0, 0, 8});
    Program prog_second;
    prog_second.functions.push_back(Prototype{&second, 0, 0, 8});

    Vm vm = make_vm();
    CHECK(vm.execute(prog_first).ok());
    CHECK(vm.execute(prog_second).ok());
    CHECK(vm.reg(3).as_int() == 1234); // untouched by the second run
}

int main()
{
    test_native_add();
    test_native_multiple_results();
    test_native_userdata_context();
    test_native_error_propagates();
    test_native_called_from_function();
    test_simple_native_no_status();
    test_mixed_native_kinds();
    test_reentrant_execute_rejected();
    test_bad_native_index();
    test_native_result_count_is_trusted_not_enforced();
    test_registers_persist_across_runs();

    if (failures == 0)
    {
        printf("natives: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "natives: %d failure(s)\n", failures);
    return 1;
}
