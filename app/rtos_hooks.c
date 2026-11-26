/*
 * FreeRTOS hooks: stack overflow, malloc failed, assert.
 * They print one line on the console and stop, so the fault is visible and the
 * watchdog (step 17) can reset the board.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "uart_console.h"

static void halt(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    console_panic("stack overflow: ");
    console_panic(pcTaskName);
    console_panic("\r\n");
    halt();
}

void vApplicationMallocFailedHook(void)
{
    console_panic("malloc failed\r\n");
    halt();
}

void vAssertCalled(const char *file, int line)
{
    char num[12];
    int i = (int)sizeof num - 1;
    unsigned v = (line < 0) ? 0U : (unsigned)line;

    num[i] = '\0';
    do {
        num[--i] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U && i > 0);

    console_panic("assert: ");
    console_panic(file);
    console_panic(":");
    console_panic(&num[i]);
    console_panic("\r\n");
    halt();
}
