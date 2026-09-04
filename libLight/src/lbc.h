#pragma once

#include <light/light_types.h>
#include <light/error.h>
#include "crc32.h"

namespace light
{

    // .lbc file: 20-byte header + payload, little-endian, read field by field
    //   magic[4] "LBC\0" | major u16 | minor u16 | flags u32 | payload_size u32 | checksum u32
    constexpr u32 LBC_HEADER_SIZE = 20;
    constexpr u16 LBC_MAJOR = 0;
    constexpr u16 LBC_MINOR = 1;

    struct LbcHeader
    {
        u16 major;
        u16 minor;
        u32 flags;
        u32 payload_size;
        u32 checksum;
    };

    namespace detail
    {
        inline u16 read_u16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
        inline u32 read_u32(const u8* p)
        {
            return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
        }
        inline void write_u16(u8* p, u16 v)
        {
            p[0] = (u8)v;
            p[1] = (u8)(v >> 8);
        }
        inline void write_u32(u8* p, u32 v)
        {
            p[0] = (u8)v;
            p[1] = (u8)(v >> 8);
            p[2] = (u8)(v >> 16);
            p[3] = (u8)(v >> 24);
        }
    } // namespace detail

    // validates size, magic, version, payload bounds and checksum
    inline Status lbc_read_header(const u8* bytes, u32 length, LbcHeader* out)
    {
        if (!bytes || length < LBC_HEADER_SIZE)
            return Status::fail(ErrorCode::CorruptBytecode);
        if (bytes[0] != 'L' || bytes[1] != 'B' || bytes[2] != 'C' || bytes[3] != '\0')
            return Status::fail(ErrorCode::BadMagic);

        LbcHeader h;
        h.major = detail::read_u16(bytes + 4);
        h.minor = detail::read_u16(bytes + 6);
        h.flags = detail::read_u32(bytes + 8);
        h.payload_size = detail::read_u32(bytes + 12);
        h.checksum = detail::read_u32(bytes + 16);

        if (h.major != LBC_MAJOR)
            return Status::fail(ErrorCode::UnsupportedVersion);
        if (h.payload_size != length - LBC_HEADER_SIZE)
            return Status::fail(ErrorCode::CorruptBytecode);
        if (h.checksum != crc32(bytes + LBC_HEADER_SIZE, h.payload_size))
            return Status::fail(ErrorCode::ChecksumMismatch);

        if (out)
            *out = h;
        return Status::success();
    }

    inline void lbc_write_header(u8* out, u32 flags, const u8* payload, u32 payload_size)
    {
        out[0] = 'L';
        out[1] = 'B';
        out[2] = 'C';
        out[3] = '\0';
        detail::write_u16(out + 4, LBC_MAJOR);
        detail::write_u16(out + 6, LBC_MINOR);
        detail::write_u32(out + 8, flags);
        detail::write_u32(out + 12, payload_size);
        detail::write_u32(out + 16, crc32(payload, payload_size));
    }

} // namespace light
