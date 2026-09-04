#pragma once

#include <light/light_types.h>

namespace light
{

    // CRC-32 (IEEE 802.3, poly 0xEDB88320), the standard zlib/PNG variant
    inline u32 crc32(const u8* bytes, usize length)
    {
        static u32 table[256];
        static bool table_ready = false;
        if (!table_ready)
        {
            for (u32 n = 0; n < 256; n++)
            {
                u32 c = n;
                for (int k = 0; k < 8; k++)
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[n] = c;
            }
            table_ready = true;
        }

        u32 crc = 0xFFFFFFFFu;
        for (usize i = 0; i < length; i++)
            crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }

} // namespace light
