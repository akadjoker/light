#include <stdio.h>
#include <string.h>

#include <light/error.h>

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
    CHECK((u32)ErrorCode::Ok == 0);
    CHECK(ERROR_CODE_COUNT > 1 && ERROR_CODE_COUNT <= 65535);

    for (u32 i = 0; i < ERROR_CODE_COUNT; i++)
    {
        const char* name = error_code_name((ErrorCode)i);
        CHECK(name != nullptr && name[0] != '\0');
    }
    CHECK(strcmp(error_code_name(ErrorCode::Ok), "Ok") == 0);
    CHECK(strcmp(error_code_name(ErrorCode::DivisionByZero), "DivisionByZero") == 0);
    CHECK(strcmp(error_code_name((ErrorCode)60000), "INVALID") == 0);

    CHECK(Status::success().ok());
    CHECK(!Status::fail(ErrorCode::BadMagic).ok());
    CHECK(Status::fail(ErrorCode::BadMagic).code == ErrorCode::BadMagic);

    if (failures == 0)
    {
        printf("error: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "error: %d failure(s)\n", failures);
    return 1;
}
