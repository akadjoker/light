#pragma once

#include <light/light_api.h>
#include <light/limits.h>
#include <ct/vector.hpp>

#include "chunk.h"

namespace light
{

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

        Status execute(const Chunk& chunk);

        const VmError& last_error() const { return error_; }
        Value reg(u32 i) const { return regs_[i]; }

      private:
        Status fail(const Chunk& chunk, ErrorCode code, u32 at);

        VmConfig config_;
        VmLimits limits_;
        bool limits_valid_;
        ct::Vector<Value> regs_;
        VmError error_{ErrorCode::Ok, 0, 0};
    };

} // namespace light
