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
    // later is just another Fiber, and yield/resume swaps the active one
    struct Fiber
    {
        ct::Vector<Value> regs;
        ct::Vector<CallFrame> frames;
    };

    struct VmError
    {
        ErrorCode code;
        u32 pc;
        u32 line;
    };

    class Vm
    {
      public:
        Vm(const VmConfig& config, const VmLimits& limits);

        Status execute(const Program& program);
        Status execute(const Chunk& chunk); // single-chunk convenience, entry has full window

        const VmError& last_error() const { return error_; }
        Value reg(u32 i) const { return main_.regs[i]; }

      private:
        Status fail(const Chunk& chunk, ErrorCode code, u32 at);

        VmConfig config_;
        VmLimits limits_;
        bool limits_valid_;
        Fiber main_;
        VmError error_{ErrorCode::Ok, 0, 0};
    };

} // namespace light
