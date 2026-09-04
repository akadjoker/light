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

        // Allocates through the host's VmConfig::alloc, never ct::HeapAlloc:
        // ct calls fatal() on failure, which a host embedding the VM cannot
        // recover from. Returns OutOfMemory instead, leaving the fiber empty.
        Status init(const VmConfig& config, u32 regs_n, u32 frames_n);
        void release(const VmConfig& config);
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
        // The VM allocates its register and frame stacks up front, so
        // construction can fail. Without exceptions a constructor cannot
        // report that, hence the factory: `out` is left untouched on failure.
        //   InvalidLimits  the limits do not validate
        //   OutOfMemory    config.alloc refused one of the two stacks
        static Status create(const VmConfig& config, const VmLimits& limits, Vm* out);

        // An empty VM, only useful as the destination for create(). Executing
        // on one returns InvalidLimits: it owns no register stack.
        Vm();

        Vm(Vm&& other);
        Vm& operator=(Vm&& other);
        ~Vm();
        Vm(const Vm&) = delete;
        Vm& operator=(const Vm&) = delete;

        // Every execute() verifies the program first, so natives registered
        // between runs are always reflected in the CALL_NATIVE bounds check.
        Status execute(const Program& program);
        Status execute(const Chunk& chunk); // single-chunk convenience, entry has full window

        // Registration happens at setup, before any script runs, and grows a
        // ct::Vector whose allocator calls fatal() if the host is out of
        // memory. The execution stacks deliberately do not share that fate:
        // see Fiber::init. Moving natives_ onto VmConfig::alloc as well is
        // left for when the GC lands and every allocation goes through it.
        u32 register_native(const char* name, NativeFn fn, void* ud, u8 param_count,
                            u8 result_count);
        u32 register_native(const char* name, NativeFnSimple fn, void* ud, u8 param_count,
                            u8 result_count);

        const VmError& last_error() const { return error_; }
        Value reg(u32 i) const { return main_.regs[i]; }

      private:
        Vm(const VmConfig& config, const VmLimits& limits);
        Status fail(const Chunk& chunk, ErrorCode code, u32 at);

        VmConfig config_;
        VmLimits limits_;
        bool executing_ = false; // natives must not call execute() re-entrantly
        Fiber main_;
        ct::Vector<NativeDescriptor> natives_;
        VmError error_{ErrorCode::Ok, 0, 0};
    };

} // namespace light
