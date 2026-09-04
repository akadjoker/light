#pragma once

#include <light/light_api.h>
#include <light/limits.h>
#include <ct/vector.hpp>

#include "chunk.h"

namespace light
{

    // arity and result count are static (checked by the compiler), so CALL
    // carries no argc: the callee's signature is the contract
    struct Prototype
    {
        const Chunk* chunk;
        u8 param_count;
        u8 result_count;
        u16 register_count;
    };

    struct Program
    {
        ct::Vector<Prototype> functions;
        u32 entry = 0;
    };

    struct CallFrame
    {
        const Prototype* proto;
        u32 base; // window into the fiber's register stack
        u32 pc;   // resume point while a callee runs
    };

    // all execution state lives here, never on the C++ stack: a coroutine
    // later is just another Fiber, and yield/resume swaps the active one.
    // Both stacks are allocated once at fiber creation and never grow.
    struct Fiber
    {
        Value* regs = nullptr;
        CallFrame* frames = nullptr;
        u32 reg_count = 0;
        u32 frame_capacity = 0;
        u32 frame_depth = 0;

        void init(u32 regs_n, u32 frames_n);
        void release();
    };

    struct VmError
    {
        ErrorCode code;
        u32 pc;
        u32 line;
    };

    class Vm;

    // args arrive in slots[0..param_count), results are written to
    // slots[0..result_count) — same window convention as script calls
    using NativeFn = Status (*)(Vm& vm, void* ud, Value* slots);     // fallible
    using NativeFnSimple = void (*)(Vm& vm, void* ud, Value* slots); // cannot fail

    struct NativeDescriptor
    {
        union
        {
            NativeFn checked;
            NativeFnSimple simple;
        } fn;
        void* ud;
        const char* name; // diagnostics only, never looked up in the hot path
        u8 param_count;
        u8 result_count;
        bool can_fail;
    };

    class Vm
    {
      public:
        Vm(const VmConfig& config, const VmLimits& limits);
        Vm(Vm&& other);
        ~Vm();
        Vm(const Vm&) = delete;
        Vm& operator=(const Vm&) = delete;
        Vm& operator=(Vm&&) = delete;

        Status execute(const Program& program);
        Status execute(const Chunk& chunk); // single-chunk convenience, entry has full window

        u32 register_native(const char* name, NativeFn fn, void* ud, u8 param_count,
                            u8 result_count);
        u32 register_native(const char* name, NativeFnSimple fn, void* ud, u8 param_count,
                            u8 result_count);

        const VmError& last_error() const { return error_; }
        Value reg(u32 i) const { return main_.regs[i]; }

      private:
        Status fail(const Chunk& chunk, ErrorCode code, u32 at);

        VmConfig config_;
        VmLimits limits_;
        bool limits_valid_;
        Fiber main_;
        ct::Vector<NativeDescriptor> natives_;
        VmError error_{ErrorCode::Ok, 0, 0};
    };

} // namespace light
