#pragma once

namespace ZX {
    inline void disable_interrupts() { __asm__ volatile("di"); }
    inline void enable_interrupts() { __asm__ volatile("ei"); }

    struct ScopedDisableInterrupts {
        ScopedDisableInterrupts() {
            disable_interrupts();
        }

        ScopedDisableInterrupts(ScopedDisableInterrupts&) = delete;
        ScopedDisableInterrupts& operator=(ScopedDisableInterrupts&) = delete;

        ~ScopedDisableInterrupts() {
            enable_interrupts();
        }
    };
} // namespace ZX

