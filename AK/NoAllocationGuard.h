/*
 * Copyright (c) 2022, kleines Filmröllchen <malu.bertsch@gmail.com>.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Forward.h>
#include <AK/Noncopyable.h>

#if defined(KERNEL)
#    include <Kernel/Arch/Processor.h>
#    include <Kernel/Heap/kmalloc.h>
#else
#    include <LibC/mallocdefs.h>
#endif

namespace AK {

class NoAllocationGuard {
    AK_MAKE_NONCOPYABLE(NoAllocationGuard);
    AK_MAKE_NONMOVABLE(NoAllocationGuard);

public:
    NoAllocationGuard()
        : m_allocation_enabled_previously(get_thread_allocation_state())
    {
        set_thread_allocation_state(false);
    }

    ~NoAllocationGuard()
    {
        set_thread_allocation_state(m_allocation_enabled_previously);
    }

private:
    static bool get_thread_allocation_state()
    {
#if defined(KERNEL)
        return Processor::current_thread()->get_allocation_enabled();
#else
        return s_allocation_enabled;
#endif
    }

    static void set_thread_allocation_state(bool value)
    {
#if defined(KERNEL)
        Processor::current_thread()->set_allocation_enabled(value);
#else
        s_allocation_enabled = value;
#endif
    }

    bool m_allocation_enabled_previously;
};

}

using AK::NoAllocationGuard;
