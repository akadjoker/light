#pragma once

#include <light/light_types.h>

namespace light
{

    struct GcObject;

    enum class ValueType : u8
    {
        Nil,
        Bool,
        Int,
        Float,
        Object
    };

    // Dynamic register/slot value: tag + payload, 16 bytes on 64-bit.
    // Static types die in the compiler; integers live here as i64, floats as f64.
    struct Value
    {
        ValueType type;
        union
        {
            bool b;
            i64 i;
            f64 f;
            GcObject* obj;
        } as;

        static Value nil()
        {
            Value v;
            v.type = ValueType::Nil;
            v.as.i = 0;
            return v;
        }
        static Value boolean(bool b)
        {
            Value v;
            v.type = ValueType::Bool;
            v.as.i = 0;
            v.as.b = b;
            return v;
        }
        static Value integer(i64 i)
        {
            Value v;
            v.type = ValueType::Int;
            v.as.i = i;
            return v;
        }
        static Value floating(f64 f)
        {
            Value v;
            v.type = ValueType::Float;
            v.as.f = f;
            return v;
        }
        static Value object(GcObject* obj)
        {
            Value v;
            v.type = ValueType::Object;
            v.as.obj = obj;
            return v;
        }

        bool is_nil() const { return type == ValueType::Nil; }
        bool is_bool() const { return type == ValueType::Bool; }
        bool is_int() const { return type == ValueType::Int; }
        bool is_float() const { return type == ValueType::Float; }
        bool is_object() const { return type == ValueType::Object; }

        bool as_bool() const { return as.b; }
        i64 as_int() const { return as.i; }
        f64 as_float() const { return as.f; }
        GcObject* as_object() const { return as.obj; }

        // raw equality: same tag + same payload; NaN != NaN (IEEE), no coercion
        bool equals(Value o) const
        {
            if (type != o.type)
                return false;
            switch (type)
            {
            case ValueType::Nil:
                return true;
            case ValueType::Bool:
                return as.b == o.as.b;
            case ValueType::Int:
                return as.i == o.as.i;
            case ValueType::Float:
                return as.f == o.as.f;
            case ValueType::Object:
                return as.obj == o.as.obj;
            }
            return false;
        }
    };

    static_assert(sizeof(Value) == 16, "Value must be 16 bytes on 64-bit");

} // namespace light
