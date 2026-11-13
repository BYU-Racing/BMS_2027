/* Task integration for the imported BMS bring-up tests. */
#include "FreeRTOS.h"
#include "task.h"
#include "bench_app.h"
#include "bench_gpio.h"
#include "hw.h"
#include "led.h"
#include "fault.h"
#include "board_check.h"
#include "cell_test.h"
#include "fast_poll.h"
#include "isospi_test.h"
#include <stdio.h>

static void heartbeat_task(void *argument)
{
    (void)argument;
    TickType_t last = xTaskGetTickCount();
    for (;;)
    {
        gpio_toggle(HW_LED_PORT, HW_LED0_PIN);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(500));
    }
}

void bench_app_init(void)
{
    /* Reports come from one task; the heartbeat never writes to stdout. */
    setvbuf(stdout, NULL, _IONBF, 0);
#if BMS_TEST_TASK == 0
    TaskFunction_t test = cell_test_task;
#elif BMS_TEST_TASK == 1
    TaskFunction_t test = fast_poll_task;
#elif BMS_TEST_TASK == 2
    TaskFunction_t test = isospi_test_task;
#else
    TaskFunction_t test = board_check_task;
#endif
    if (xTaskCreate(test, "BMS test", 768, NULL, 1, NULL) != pdPASS ||
        xTaskCreate(heartbeat_task, "Heartbeat", 128, NULL, 2, NULL) != pdPASS)
    {
        fault_halt();
    }
}

void vApplicationMallocFailedHook(void)
{
    fault_halt();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    fault_halt();
}
