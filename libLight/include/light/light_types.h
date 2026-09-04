#pragma once

#include <stdint.h>
#include <stddef.h>

namespace light
{

    using u8 = uint8_t;
    using i8 = int8_t;
    using u16 = uint16_t;
    using i16 = int16_t;
    using u32 = uint32_t;
    using i32 = int32_t;
    using u64 = uint64_t;
    using i64 = int64_t;
    using f32 = float;
    using f64 = double;

    // usize is for in-memory sizes/indices only, never in the .lbc format
    using usize = size_t;

    static_assert(sizeof(f32) == 4, "f32 must be 4 bytes");
    static_assert(sizeof(f64) == 8, "f64 must be 8 bytes");

} // namespace light
