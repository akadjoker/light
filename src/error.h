#pragma once

#include "light_types.h"

namespace light
{

    enum class ErrorCode : u16
    {
#define ERR(name) name,
#include "light_errors.def"
#undef ERR
        COUNT
    };

    constexpr u32 ERROR_CODE_COUNT = (u32)ErrorCode::COUNT;

    inline const char* error_code_name(ErrorCode code)
    {
        static const char* const names[] = {
#define ERR(name) #name,
#include "light_errors.def"
#undef ERR
        };
        return (u32)code < ERROR_CODE_COUNT ? names[(u32)code] : "INVALID";
    }

    struct Status
    {
        ErrorCode code;

        bool ok() const { return code == ErrorCode::Ok; }

        static Status success() { return Status{ErrorCode::Ok}; }
        static Status fail(ErrorCode c) { return Status{c}; }
    };

} // namespace light
