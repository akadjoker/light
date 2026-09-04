#pragma once

#include <string.h>

#include <light/light_types.h>
#include <ct/string.hpp>

namespace light
{
    namespace fmt
    {

        inline void append_u32(ct::String& s, u32 v)
        {
            char buf[11];
            char* p = buf + 10;
            *p = '\0';
            do
            {
                *--p = (char)('0' + v % 10);
                v /= 10;
            } while (v);
            s += p;
        }

        inline void append_u32_zero_padded(ct::String& s, u32 v, u32 width)
        {
            char buf[11];
            char* p = buf + 10;
            *p = '\0';
            u32 digits = 0;
            do
            {
                *--p = (char)('0' + v % 10);
                v /= 10;
                digits++;
            } while (v);
            for (; digits < width; digits++)
                s += "0";
            s += p;
        }

        inline void append_i64(ct::String& s, i64 v)
        {
            if (v < 0)
            {
                s += "-";
                // negate via u64 to survive INT64_MIN
                u64 mag = ~(u64)v + 1;
                char buf[21];
                char* p = buf + 20;
                *p = '\0';
                do
                {
                    *--p = (char)('0' + mag % 10);
                    mag /= 10;
                } while (mag);
                s += p;
                return;
            }
            char buf[21];
            char* p = buf + 20;
            *p = '\0';
            u64 mag = (u64)v;
            do
            {
                *--p = (char)('0' + mag % 10);
                mag /= 10;
            } while (mag);
            s += p;
        }

        inline void append_padded(ct::String& s, const char* text, usize width)
        {
            s += text;
            usize len = strlen(text);
            for (; len < width; len++)
                s += " ";
        }

    } // namespace fmt
} // namespace light
