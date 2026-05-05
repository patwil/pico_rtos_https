#include <stdint.h>

volatile const char *g_assert_file;
volatile int g_assert_line;

__attribute__((noreturn))
void vAssertCalled(const char *file, int line)
{
    g_assert_file = file;
    g_assert_line = line;

    // Disable interrupts (Cortex-M0+)
    __asm volatile ("cpsid i");

    // Break into the debugger
    __asm volatile ("bkpt #0");

    // Park forever
    for (;;)
        __asm volatile ("wfi");
}
