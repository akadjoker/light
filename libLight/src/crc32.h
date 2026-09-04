#pragma once

#include <light/light_types.h>

namespace light
{

    // CRC-32 (IEEE 802.3, poly 0xEDB88320), the standard zlib/PNG variant.
    // Bitwise on purpose: no static table, so no init data race; this only
    // runs in the cold .lbc load path.
    inline u32 crc32(const u8* bytes, usize length)
    {
        u32 crc = 0xFFFFFFFFu;
        for (usize i = 0; i < length; i++)
        {
            crc ^= bytes[i];
            for (int k = 0; k < 8; k++)
                crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        }
        return crc ^ 0xFFFFFFFFu;
    }

} // namespace light
