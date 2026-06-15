#include "stm32f411xe.h"
#include "FreeRTOS.h"
#include "task.h"
#include "clock.h"



int main(void) {
    SystemClock_Config();
    vTaskStartScheduler();
    for (;;) {}
}
