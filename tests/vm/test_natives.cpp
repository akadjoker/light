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

static void test_bad_native_index()
{
    Vm vm = make_vm();

    Chunk c;
    c.write(encode_abx(Op::CALL_NATIVE, 0, 99), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    CHECK(vm.execute(c).code == ErrorCode::CorruptBytecode);
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
    test_bad_native_index();

    if (failures == 0)
    {
        printf("natives: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "natives: %d failure(s)\n", failures);
    return 1;
}
