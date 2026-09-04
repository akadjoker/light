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

static void test_simple_call()
{
    // add(a, b): r2 = a + b; return r2
    Chunk add_chunk;
    add_chunk.write(encode_abc(Op::ADD_INT, 2, 0, 1), 1);
    add_chunk.write(encode_abc(Op::RETURN, 2, 0, 0), 1);

    // main: r0 = 7, r1 = 3, call add at window 0, halt
    Chunk main_chunk;
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 0, 7), 1);
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 1, 3), 1);
    main_chunk.write(encode_abx(Op::CALL, 0, 1), 2);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 8});
    prog.functions.push_back(Prototype{&add_chunk, 2, 1, 3});
    prog.entry = 0;

    Vm vm = make_vm();
    CHECK(vm.execute(prog).ok());
    CHECK(vm.reg(0).as_int() == 10);
}

static void test_call_window_offset()
{
    // main keeps r0 intact, calls add with window at r5
    Chunk add_chunk;
    add_chunk.write(encode_abc(Op::ADD_INT, 2, 0, 1), 1);
    add_chunk.write(encode_abc(Op::RETURN, 2, 0, 0), 1);

    Chunk main_chunk;
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 0, 99), 1);
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 5, 20), 2);
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 6, 22), 2);
    main_chunk.write(encode_abx(Op::CALL, 5, 1), 3);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 4);

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 16});
    prog.functions.push_back(Prototype{&add_chunk, 2, 1, 3});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).ok());
    CHECK(vm.reg(0).as_int() == 99); // caller register below window untouched
    CHECK(vm.reg(5).as_int() == 42); // result replaced the window start
}

static void test_multiple_results()
{
    // swap(a, b) -> (b, a)
    Chunk swap_chunk;
    swap_chunk.write(encode_abc(Op::MOVE, 2, 1, 0), 1);
    swap_chunk.write(encode_abc(Op::MOVE, 3, 0, 0), 1);
    swap_chunk.write(encode_abc(Op::RETURN, 2, 0, 0), 1);

    Chunk main_chunk;
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 0, 1), 1);
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 1, 2), 1);
    main_chunk.write(encode_abx(Op::CALL, 0, 1), 2);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 8});
    prog.functions.push_back(Prototype{&swap_chunk, 2, 2, 4});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).ok());
    CHECK(vm.reg(0).as_int() == 2);
    CHECK(vm.reg(1).as_int() == 1);
}

static void test_recursive_fib()
{
    // fib(n): if n < 2 return n; return fib(n-1) + fib(n-2)
    Chunk fib;
    fib.write(encode_asbx(Op::LOAD_INT16, 1, 2), 1);
    fib.write(encode_abc(Op::LESS_INT, 2, 0, 1), 1);
    fib.write(encode_asbx(Op::JUMP_IF_FALSE, 2, 1), 1);
    fib.write(encode_abc(Op::RETURN, 0, 0, 0), 1);
    fib.write(encode_asbx(Op::LOAD_INT16, 3, 1), 2);
    fib.write(encode_abc(Op::SUB_INT, 4, 0, 3), 2);
    fib.write(encode_abx(Op::CALL, 4, 1), 2); // r4 = fib(n-1)
    fib.write(encode_asbx(Op::LOAD_INT16, 5, 2), 3);
    fib.write(encode_abc(Op::SUB_INT, 6, 0, 5), 3);
    fib.write(encode_abx(Op::CALL, 6, 1), 3); // r6 = fib(n-2)
    fib.write(encode_abc(Op::ADD_INT, 7, 4, 6), 4);
    fib.write(encode_abc(Op::RETURN, 7, 0, 0), 4);

    Chunk main_chunk;
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 0, 10), 1);
    main_chunk.write(encode_abx(Op::CALL, 0, 1), 1);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 8});
    prog.functions.push_back(Prototype{&fib, 1, 1, 8});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).ok());
    CHECK(vm.reg(0).as_int() == 55);
}

static void test_void_call()
{
    Chunk noop_chunk;
    noop_chunk.write(encode_abc(Op::RETURN, 0, 0, 0), 1);

    Chunk main_chunk;
    main_chunk.write(encode_asbx(Op::LOAD_INT16, 0, 7), 1);
    main_chunk.write(encode_abx(Op::CALL, 3, 1), 2);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 3);

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 8});
    prog.functions.push_back(Prototype{&noop_chunk, 0, 0, 1});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).ok());
    CHECK(vm.reg(0).as_int() == 7);
}

static void test_infinite_recursion_overflows()
{
    Chunk self;
    self.write(encode_abx(Op::CALL, 0, 0), 1);
    self.write(encode_abc(Op::RETURN, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&self, 0, 0, 4});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).code == ErrorCode::StackOverflow);
    CHECK(vm.last_error().pc == 0);
}

static void test_deep_window_overflows_registers()
{
    // window advances 200 regs per call: register stack runs out before frames
    Chunk self;
    self.write(encode_abx(Op::CALL, 200, 0), 1);
    self.write(encode_abc(Op::RETURN, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&self, 0, 0, 256});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).code == ErrorCode::StackOverflow);
}

static void test_return_outside_own_window_rejected()
{
    // register_count 4, but RETURN claims the result lives at r200
    Chunk fn;
    fn.write(encode_abc(Op::RETURN, 200, 0, 0), 1);

    Chunk main_chunk;
    main_chunk.write(encode_abx(Op::CALL, 0, 1), 1);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 2);

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 8});
    prog.functions.push_back(Prototype{&fn, 0, 1, 4});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).code == ErrorCode::CorruptBytecode);
}

static void test_bad_function_index()
{
    Chunk main_chunk;
    main_chunk.write(encode_abx(Op::CALL, 0, 99), 1);
    main_chunk.write(encode_abc(Op::HALT, 0, 0, 0), 1);

    Program prog;
    prog.functions.push_back(Prototype{&main_chunk, 0, 0, 8});

    Vm vm = make_vm();
    CHECK(vm.execute(prog).code == ErrorCode::CorruptBytecode);
}

static void test_bad_entry_index()
{
    Program prog;
    prog.entry = 5;
    Vm vm = make_vm();
    CHECK(vm.execute(prog).code == ErrorCode::CorruptBytecode);
}

int main()
{
    test_simple_call();
    test_call_window_offset();
    test_multiple_results();
    test_recursive_fib();
    test_void_call();
    test_infinite_recursion_overflows();
    test_deep_window_overflows_registers();
    test_return_outside_own_window_rejected();
    test_bad_function_index();
    test_bad_entry_index();

    if (failures == 0)
    {
        printf("calls: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "calls: %d failure(s)\n", failures);
    return 1;
}
