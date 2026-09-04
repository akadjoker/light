#include <stdio.h>

#include "bytecode.h"

using namespace light;

static int failures = 0;

#define CHECK(cond)                                                                \
    do                                                                             \
    {                                                                              \
        if (!(cond))                                                               \
        {                                                                          \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static void test_abc_roundtrip()
{
    const u8 edge[] = {0, 1, 127, 128, 255};
    for (u8 a : edge)
        for (u8 b : edge)
            for (u8 c : edge)
            {
                Instruction i = encode_abc(Op::ADD_INT, a, b, c);
                CHECK(decode_op(i) == Op::ADD_INT);
                CHECK(decode_a(i) == a);
                CHECK(decode_b(i) == b);
                CHECK(decode_c(i) == c);
            }
}

static void test_abx_roundtrip()
{
    const u16 edge[] = {0, 1, 255, 256, 32767, 32768, 65535};
    for (u16 bx : edge)
    {
        Instruction i = encode_abx(Op::LOAD_CONST, 42, bx);
        CHECK(decode_op(i) == Op::LOAD_CONST);
        CHECK(decode_a(i) == 42);
        CHECK(decode_bx(i) == bx);
    }
}

static void test_asbx_roundtrip()
{
    const i16 edge[] = {-32768, -32767, -1, 0, 1, 255, 32767};
    for (i16 sbx : edge)
    {
        Instruction i = encode_asbx(Op::JUMP, 0, sbx);
        CHECK(decode_op(i) == Op::JUMP);
        CHECK(decode_sbx(i) == sbx);
    }
}

static void test_op_metadata()
{
    CHECK(OP_COUNT > 0 && OP_COUNT <= 256);
    CHECK(op_format(Op::ADD_INT) == OpFormat::ABC);
    CHECK(op_format(Op::LOAD_CONST) == OpFormat::ABx);
    CHECK(op_format(Op::JUMP) == OpFormat::AsBx);
    CHECK(op_format(Op::LOAD_INT16) == OpFormat::AsBx);

    for (u32 i = 0; i < OP_COUNT; i++)
    {
        const char* name = op_name((Op)i);
        CHECK(name != nullptr && name[0] != '\0');
    }
    CHECK(op_name((Op)255) != nullptr);
}

static void test_instruction_is_32bit()
{
    static_assert(sizeof(Instruction) == 4, "instruction must be 32-bit");
    Instruction i = encode_abc(Op::HALT, 0xFF, 0xFF, 0xFF);
    CHECK((i & 0xFF) == (u32)Op::HALT);
}

int main()
{
    test_abc_roundtrip();
    test_abx_roundtrip();
    test_asbx_roundtrip();
    test_op_metadata();
    test_instruction_is_32bit();

    if (failures == 0)
    {
        printf("bytecode: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "bytecode: %d failure(s)\n", failures);
    return 1;
}
