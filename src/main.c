#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdio.h"

/* Incluyo componentes */
#include "../components/MPU6050/include/MPU6050.h"
#include "../components/TFMINI/include/tfMini.h"
#include "../components/CAN_COMM/include/CAN_COMMS.h"


#define GPIO_CAN_TX     14
#define GPIO_CAN_RX     12
#define UART_PORT_CAN   UART_NUM_1

void app_main() {
    float angleX = 0.00;
    uint16_t distance = 0;

    mpu_init();
    tfMiniInit();
    canInit(GPIO_CAN_TX,GPIO_CAN_RX,UART_PORT_CAN);

    while(1){

        angleX = getAngle(AXIS_ANGLE_X);
        distance = tfMiniGetDist();

        printf("angleX: %f  , distance: %d",angleX,distance);
        vTaskDelay(pdMS_TO_TICKS(100));
    
    }
}