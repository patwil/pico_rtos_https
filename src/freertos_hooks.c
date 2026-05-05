#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    printf("STACK OVERFLOW: %s\n", pcTaskName ? pcTaskName : "(null)");
    taskDISABLE_INTERRUPTS();
    __breakpoint();
    for(;;);
}

void vApplicationMallocFailedHook(void) {
    printf("MALLOC FAILED!\n");
    taskDISABLE_INTERRUPTS();
    __breakpoint();
    for(;;);
}
