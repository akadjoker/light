#include <stdio.h>
#include <string.h>

#include "disasm.h"

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

#define CHECK_STR(actual, expected)                                                                \
    do                                                                                             \
    {                                                                                              \
        if (strcmp((actual), (expected)) != 0)                                                     \
        {                                                                                          \
            fprintf(stderr, "FAILED %s:%d:\n  expected: %s\n  actual:   %s\n", __FILE__, __LINE__, \
                    (expected), (actual));                                                         \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_chunk_write()
{
    Chunk c;
    CHECK(c.write(encode_abc(Op::NOP, 0, 0, 0), 1) == 0);
    CHECK(c.write(encode_abc(Op::HALT, 0, 0, 0), 2) == 1);
    CHECK(c.code.size() == 2 && c.lines.size() == 2);
    CHECK(c.lines[0] == 1 && c.lines[1] == 2);

    CHECK(c.add_constant(Value::integer(42)) == 0);
    CHECK(c.add_constant(Value::floating(2.5)) == 1);
    CHECK(c.constants.size() == 2);
}

static ct::String disasm_one(const Chunk& c, u32 offset)
{
    ct::String s;
    disassemble_instruction(c, offset, s);
    return s;
}

static void test_disasm_golden()
{
    Chunk c;
    u32 k_int = c.add_constant(Value::integer(1234));
    u32 k_float = c.add_constant(Value::floating(0.5));
    c.write(encode_asbx(Op::LOAD_INT16, 0, 42), 1);
    c.write(encode_asbx(Op::LOAD_INT16, 1, -7), 1);
    c.write(encode_abc(Op::ADD_INT, 2, 0, 1), 2);
    c.write(encode_abx(Op::LOAD_CONST, 3, (u16)k_int), 3);
    c.write(encode_abx(Op::LOAD_CONST, 4, (u16)k_float), 3);
    c.write(encode_asbx(Op::JUMP, 0, -3), 4);
    c.write(encode_abc(Op::RETURN, 2, 1, 0), 5);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 5);

    CHECK_STR(disasm_one(c, 0).c_str(), "0000  1  LOAD_INT16       0 42");
    CHECK_STR(disasm_one(c, 1).c_str(), "0001  1  LOAD_INT16       1 -7");
    CHECK_STR(disasm_one(c, 2).c_str(), "0002  2  ADD_INT          2 0 1");
    CHECK_STR(disasm_one(c, 3).c_str(), "0003  3  LOAD_CONST       3 0 ; 1234");
    CHECK_STR(disasm_one(c, 4).c_str(), "0004  3  LOAD_CONST       4 1 ; float");
    CHECK_STR(disasm_one(c, 5).c_str(), "0005  4  JUMP             0 -3");
    CHECK_STR(disasm_one(c, 6).c_str(), "0006  5  RETURN           2 1 0");
    CHECK_STR(disasm_one(c, 7).c_str(), "0007  5  HALT             0 0 0");
}

struct Capture
{
    int lines = 0;
    ct::String all;
};

static void capture_print(void* ud, LogLevel level, const char* bytes, u32 length)
{
    (void)level;
    Capture* cap = (Capture*)ud;
    cap->lines++;
    for (u32 i = 0; i < length; i++)
    {
        char one[2] = {bytes[i], '\0'};
        cap->all += one;
    }
    cap->all += "\n";
}

static void test_disasm_chunk_via_print_callback()
{
    Chunk c;
    c.write(encode_abc(Op::NOP, 0, 0, 0), 1);
    c.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Capture cap;
    VmConfig config = {};
    config.print = capture_print;
    config.print_ud = &cap;

    disassemble_chunk(c, "test", config);
    CHECK(cap.lines == 3); // header + 2 instructions
    CHECK(strstr(cap.all.c_str(), "== test ==") != nullptr);
    CHECK(strstr(cap.all.c_str(), "NOP") != nullptr);
    CHECK(strstr(cap.all.c_str(), "HALT") != nullptr);

    VmConfig no_print = {};
    disassemble_chunk(c, "test", no_print);
}

int main()
{
    test_chunk_write();
    test_disasm_golden();
    test_disasm_chunk_via_print_callback();

    if (failures == 0)
    {
        printf("chunk: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "chunk: %d failure(s)\n", failures);
    return 1;
}
