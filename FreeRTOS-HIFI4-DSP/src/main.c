#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xtensa/config/core-matmap.h>
#include <xtensa/config/core.h>
#include <xtensa/core-macros.h>
#include <xtensa/hal.h>
#include <xtensa/tie/xt_externalregisters.h>
#include <xtensa/xtruntime.h>
#include <xtensa_api.h>

#include "FreeRTOS.h"
#include "platform.h"
#include "task.h"

void vTaskMain(void *pvParameters) {
    (void) pvParameters;
    rpmsg_service_run();
}

/*
 *  Main function
 */
int main(void) {
    xTaskHandle xHandleTaskMain;

    xTaskCreate(vTaskMain, "Task Main", 4096, NULL, 1, &xHandleTaskMain);
    vTaskStartScheduler();

    printf("DSP Boot Failed!\n");
    while(1){};
}
