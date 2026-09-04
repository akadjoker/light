#pragma once

#include "vm.h"

#include <stdlib.h>

namespace light
{
    namespace testing
    {

        // lua_Alloc contract: new_size == 0 frees, ptr == nullptr allocates
        inline void* test_alloc(void*, void* ptr, usize, usize new_size)
        {
            if (new_size == 0)
            {
                free(ptr);
                return nullptr;
            }
            return realloc(ptr, new_size);
        }

        inline VmConfig test_config()
        {
            VmConfig config = {};
            config.alloc = test_alloc;
            return config;
        }

        // Construction can fail (bad limits, OOM), but the tests that use this
        // pass valid limits and expect it to succeed; a test covering failure
        // calls Vm::create directly and inspects the Status.
        inline Vm make_vm(VmLimits limits = VmLimits{})
        {
            Vm vm;
            Status s = Vm::create(test_config(), limits, &vm);
            (void)s;
            return vm;
        }

    } // namespace testing
} // namespace light
