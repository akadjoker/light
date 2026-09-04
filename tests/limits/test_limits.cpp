#include <stdio.h>

#include <light/limits.h>

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

int main()
{
    VmLimits defaults;
    CHECK(defaults.validate().ok());

    VmLimits small = defaults;
    small.register_stack_slots = REGISTERS_PER_FUNCTION;
    CHECK(small.validate().ok());
    small.register_stack_slots = REGISTERS_PER_FUNCTION - 1;
    CHECK(small.validate().code == ErrorCode::InvalidLimits);

    VmLimits consts = defaults;
    consts.max_constants = 65536;
    CHECK(consts.validate().ok());
    consts.max_constants = 65537;
    CHECK(consts.validate().code == ErrorCode::InvalidLimits);
    consts.max_constants = 0;
    CHECK(consts.validate().code == ErrorCode::InvalidLimits);

    VmLimits zeroed = defaults;
    zeroed.max_frames = 0;
    CHECK(!zeroed.validate().ok());
    zeroed = defaults;
    zeroed.max_parse_depth = 0;
    CHECK(!zeroed.validate().ok());
    zeroed = defaults;
    zeroed.max_diagnostics = 0;
    CHECK(!zeroed.validate().ok());

    VmLimits unlimited_mem = defaults;
    unlimited_mem.memory_limit = 0;
    CHECK(unlimited_mem.validate().ok());

    if (failures == 0)
    {
        printf("limits: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "limits: %d failure(s)\n", failures);
    return 1;
}
