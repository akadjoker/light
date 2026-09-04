#include <stdio.h>

#include "value.h"

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

static void test_construction()
{
    CHECK(Value::nil().is_nil());
    CHECK(Value::boolean(true).is_bool() && Value::boolean(true).as_bool());
    CHECK(Value::boolean(false).is_bool() && !Value::boolean(false).as_bool());
    CHECK(Value::integer(-42).is_int() && Value::integer(-42).as_int() == -42);
    CHECK(Value::floating(2.5).is_float() && Value::floating(2.5).as_float() == 2.5);
    CHECK(Value::object(nullptr).is_object());

    Value max = Value::integer(INT64_MAX);
    Value min = Value::integer(INT64_MIN);
    CHECK(max.as_int() == INT64_MAX && min.as_int() == INT64_MIN);

    u8 small = 0xFF;
    CHECK(Value::integer((i64)(u64)small).as_int() == 255);
    i8 neg = -1;
    CHECK(Value::integer((i64)neg).as_int() == -1);
}

static void test_equals()
{
    CHECK(Value::nil().equals(Value::nil()));
    CHECK(Value::boolean(true).equals(Value::boolean(true)));
    CHECK(!Value::boolean(true).equals(Value::boolean(false)));
    CHECK(Value::integer(7).equals(Value::integer(7)));
    CHECK(!Value::integer(7).equals(Value::integer(8)));
    CHECK(Value::floating(1.5).equals(Value::floating(1.5)));

    // no coercion across tags
    CHECK(!Value::integer(1).equals(Value::floating(1.0)));
    CHECK(!Value::integer(0).equals(Value::nil()));
    CHECK(!Value::boolean(false).equals(Value::nil()));

    // IEEE: NaN != NaN, 0.0 == -0.0
    f64 nan = 0.0 / 0.0;
    CHECK(!Value::floating(nan).equals(Value::floating(nan)));
    CHECK(Value::floating(0.0).equals(Value::floating(-0.0)));
}

static void test_layout()
{
    static_assert(sizeof(Value) == 16, "Value must be 16 bytes");
    Value v = Value::integer(-1);
    v = Value::floating(0.5);
    CHECK(v.is_float() && !v.is_int());
}

int main()
{
    test_construction();
    test_equals();
    test_layout();

    if (failures == 0)
    {
        printf("value: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "value: %d failure(s)\n", failures);
    return 1;
}
