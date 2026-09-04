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

static void test_int_arithmetic()
{
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 7), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 1, 3), 1);
    c.write(encode_abc(Op::ADD_INT, 2, 0, 1), 2);
    c.write(encode_abc(Op::SUB_INT, 3, 0, 1), 2);
    c.write(encode_abc(Op::MUL_INT, 4, 0, 1), 2);
    c.write(encode_abc(Op::DIV_INT, 5, 0, 1), 2);
    c.write(encode_abc(Op::NEG_INT, 6, 0, 0), 3);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 4);

    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(2).as_int() == 10);
    CHECK(vm.reg(3).as_int() == 4);
    CHECK(vm.reg(4).as_int() == 21);
    CHECK(vm.reg(5).as_int() == 2);
    CHECK(vm.reg(6).as_int() == -7);
}

static void test_float_arithmetic()
{
    Chunk c;
    u16 ka = (u16)c.add_constant(Value::floating(1.5));
    u16 kb = (u16)c.add_constant(Value::floating(0.5));
    c.write(encode_abx(Op::LOAD_CONST, 0, ka), 1);
    c.write(encode_abx(Op::LOAD_CONST, 1, kb), 1);
    c.write(encode_abc(Op::ADD_FLOAT, 2, 0, 1), 2);
    c.write(encode_abc(Op::MUL_FLOAT, 3, 0, 1), 2);
    c.write(encode_abc(Op::DIV_FLOAT, 4, 0, 1), 2);
    c.write(encode_abc(Op::NEG_FLOAT, 5, 0, 0), 2);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(2).as_float() == 2.0);
    CHECK(vm.reg(3).as_float() == 0.75);
    CHECK(vm.reg(4).as_float() == 3.0);
    CHECK(vm.reg(5).as_float() == -1.5);
}

static void test_float_div_by_zero_is_ieee()
{
    Chunk c;
    u16 ka = (u16)c.add_constant(Value::floating(1.0));
    u16 kz = (u16)c.add_constant(Value::floating(0.0));
    c.write(encode_abx(Op::LOAD_CONST, 0, ka), 1);
    c.write(encode_abx(Op::LOAD_CONST, 1, kz), 1);
    c.write(encode_abc(Op::DIV_FLOAT, 2, 0, 1), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    f64 inf = vm.reg(2).as_float();
    CHECK(inf > 0 && inf == inf * 2); // +inf
}

static void test_int_div_by_zero_fails()
{
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 1), 10);
    c.write(encode_asbx(Op::LOAD_INT16, 1, 0), 11);
    c.write(encode_abc(Op::DIV_INT, 2, 0, 1), 12);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 13);

    Vm vm = make_vm();
    CHECK(vm.execute(c).code == ErrorCode::DivisionByZero);
    CHECK(vm.last_error().code == ErrorCode::DivisionByZero);
    CHECK(vm.last_error().pc == 2);
    CHECK(vm.last_error().line == 12);
}

static void test_comparisons_and_bool()
{
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 5), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 1, 9), 1);
    c.write(encode_abc(Op::LESS_INT, 2, 0, 1), 2);       // true
    c.write(encode_abc(Op::LESS_EQUAL_INT, 3, 1, 0), 2); // false
    c.write(encode_abc(Op::EQUAL, 4, 0, 0), 2);          // true
    c.write(encode_abc(Op::NOT, 5, 2, 0), 3);            // false
    c.write(encode_abc(Op::HALT, 0, 0, 0), 4);

    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(2).is_bool() && vm.reg(2).as_bool());
    CHECK(vm.reg(3).is_bool() && !vm.reg(3).as_bool());
    CHECK(vm.reg(4).as_bool());
    CHECK(!vm.reg(5).as_bool());
}

static void test_equal_no_coercion()
{
    Chunk c;
    u16 kf = (u16)c.add_constant(Value::floating(1.0));
    c.write(encode_asbx(Op::LOAD_INT16, 0, 1), 1);
    c.write(encode_abx(Op::LOAD_CONST, 1, kf), 1);
    c.write(encode_abc(Op::EQUAL, 2, 0, 1), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    CHECK(!vm.reg(2).as_bool());
}

static void test_loop_sums_1_to_10()
{
    // r0 = sum, r1 = i, r2 = 10, r3 = 1, r4 = cond
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 0), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 1, 1), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 2, 10), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 3, 1), 1);
    c.write(encode_abc(Op::LESS_EQUAL_INT, 4, 1, 2), 2); // pc 4
    c.write(encode_asbx(Op::JUMP_IF_FALSE, 4, 3), 2);
    c.write(encode_abc(Op::ADD_INT, 0, 0, 1), 3);
    c.write(encode_abc(Op::ADD_INT, 1, 1, 3), 3);
    c.write(encode_asbx(Op::JUMP, 0, -5), 3); // back to pc 4
    c.write(encode_abc(Op::HALT, 0, 0, 0), 4);

    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(0).as_int() == 55);
    CHECK(vm.reg(1).as_int() == 11);
}

static void test_type_error()
{
    Chunk c;
    u16 kf = (u16)c.add_constant(Value::floating(1.0));
    c.write(encode_abx(Op::LOAD_CONST, 0, kf), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 1, 1), 1);
    c.write(encode_abc(Op::ADD_INT, 2, 0, 1), 2);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    Vm vm = make_vm();
    CHECK(vm.execute(c).code == ErrorCode::TypeError);
    CHECK(vm.last_error().pc == 2);
}

static void test_rejects_bad_constant_index()
{
    Chunk c;
    c.write(encode_abx(Op::LOAD_CONST, 0, 99), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Vm vm = make_vm();
    CHECK(vm.execute(c).code == ErrorCode::CorruptBytecode);
}

static void test_rejects_invalid_opcode()
{
    Chunk c;
    c.write((Instruction)250, 1); // opcode byte 250 does not exist
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Vm vm = make_vm();
    CHECK(vm.execute(c).code == ErrorCode::CorruptBytecode);
    CHECK(vm.last_error().pc == 0);
}

static void test_rejects_missing_halt()
{
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 1), 1);

    Vm vm = make_vm();
    CHECK(vm.execute(c).code == ErrorCode::CorruptBytecode);
}

static void test_rejects_jump_out_of_bounds()
{
    Chunk c;
    c.write(encode_asbx(Op::JUMP, 0, -10), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Vm vm = make_vm();
    CHECK(vm.execute(c).code == ErrorCode::CorruptBytecode);

    Chunk c2;
    c2.write(encode_asbx(Op::JUMP, 0, 100), 1);
    c2.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Vm vm2 = make_vm();
    CHECK(vm2.execute(c2).code == ErrorCode::CorruptBytecode);
}

static void test_return_ends_execution()
{
    Chunk c;
    c.write(encode_asbx(Op::LOAD_INT16, 0, 42), 1);
    c.write(encode_abc(Op::RETURN, 0, 1, 0), 2);
    c.write(encode_asbx(Op::LOAD_INT16, 0, 99), 3); // must not run

    Vm vm = make_vm();
    CHECK(vm.execute(c).ok());
    CHECK(vm.reg(0).as_int() == 42);
}

static void test_invalid_limits_rejected()
{
    VmConfig config = {};
    VmLimits limits;
    limits.max_frames = 0;
    Vm vm(config, limits);
    Chunk c;
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);
    CHECK(vm.execute(c).code == ErrorCode::InvalidLimits);
}

int main()
{
    test_int_arithmetic();
    test_float_arithmetic();
    test_float_div_by_zero_is_ieee();
    test_int_div_by_zero_fails();
    test_comparisons_and_bool();
    test_equal_no_coercion();
    test_loop_sums_1_to_10();
    test_type_error();
    test_rejects_bad_constant_index();
    test_rejects_invalid_opcode();
    test_rejects_missing_halt();
    test_rejects_jump_out_of_bounds();
    test_return_ends_execution();
    test_invalid_limits_rejected();

    if (failures == 0)
    {
        printf("vm: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "vm: %d failure(s)\n", failures);
    return 1;
}
