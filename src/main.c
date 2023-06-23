#include "driver/gpio.h"
#include "soc/gpio_periph.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdio.h"

#include "main.h"
#include "comms.h"
#include "PID.h"
#include "storage_flash.h"
#include "stepper.h"

/* Incluyo componentes */
#include "../components/MPU6050/include/MPU6050.h"
#include "../components/TFMINI/include/tfMini.h"
#include "../components/CAN_COMM/include/CAN_COMMS.h"
#include "../components/BT_CLASSIC/include/BT_CLASSIC.h"

#define GRAPH_ARDUINO_PLOTTER   true

#define MAX_VELOCITY            1000
#define VEL_MAX_CONTROL         5
#define CENTER_ANGLE_MOUNTED    84.00      

extern QueueSetHandle_t newAnglesQueue;                 // Recibo nuevos angulos obtenidos del MPU
QueueHandle_t queueNewPidParams;                        // Recibo nuevos parametros relacionados al pid
QueueSetHandle_t outputMotorQueue;                      // Envio nuevos valores de salida para el control de motores

QueueHandle_t queueReceiveControl;

status_robot_t statusToSend;                            // Estructura que contiene todos los parametros de status a enviar a la app

output_motors_t attitudeControlMotor;

static void imuControlHandler(void *pvParameters){
    float newAngles[3];
    float outputPid;
    output_motors_t outputMotors;

    outputMotorQueue = xQueueCreate(5,sizeof(output_motors_t));

    while(1){

        if(xQueueReceive(newAnglesQueue,&newAngles,10)){

            gpio_set_level(13,1);
            statusToSend.pitch = newAngles[AXIS_ANGLE_X];
            statusToSend.roll = newAngles[AXIS_ANGLE_Y];
            statusToSend.yaw = newAngles[AXIS_ANGLE_Z];

            outputPid = pidCalculate(newAngles[AXIS_ANGLE_Y]) *-1; 
            outputMotors.motorL = outputPid * MAX_VELOCITY;
            outputMotors.motorR=outputMotors.motorL;

            // xQueueSend(outputMotorQueue,(void*) &outputMotors,0);                           // TODO: falta recibir los datos de esta cola y enviarlos a los motores
            setVelMotors(outputMotors.motorL,outputMotors.motorR);

            statusToSend.speedL = outputMotors.motorL;
            statusToSend.speedR = outputMotors.motorR;

            // Envio data a los motores
            // sendMotorData(outputMotors.motorR,outputMotors.motorL,0x00,0x00);            // TODO: controlar el enable

            if(!getEnablePid()){ 
                if((( newAngles[AXIS_ANGLE_Y] > (CENTER_ANGLE_MOUNTED-1)) && ( newAngles[AXIS_ANGLE_Y] < (CENTER_ANGLE_MOUNTED+1) )) ){ 
                    enableMotors();
                    setEnablePid();                                                          
                }
                else{
                    disableMotors();
                }
            }
            else{ 
                if((( newAngles[AXIS_ANGLE_Y] < (CENTER_ANGLE_MOUNTED-40)) || ( newAngles[AXIS_ANGLE_Y] > (CENTER_ANGLE_MOUNTED+40) )) ){ 
                    disableMotors();
                    setDisablePid();
                }
            }

            if(GRAPH_ARDUINO_PLOTTER){
                //Envio log para graficar en arduino serial plotter
                // printf("angle_x:%f,set_point: %f,output_pid: %f, output_motor:%d\n",newAngles[AXIS_ANGLE_Y],CENTER_ANGLE_MOUNTED,outputPid,outputMotors.motorL);
            }
            gpio_set_level(13,0);
        }
    }
}

static void updateParams(void *pvParameters){

    pid_params_t newPidParams;

    queueNewPidParams = xQueueCreate(1,sizeof(pid_params_t));

    while (1){
        
        if(xQueueReceive(queueNewPidParams,&newPidParams,0)){
            // printf("Nuevos parametros recibidos: %f\n",newPidParams.kp);
            newPidParams.centerAngle += CENTER_ANGLE_MOUNTED;
            pidSetConstants(newPidParams.kp,newPidParams.ki,newPidParams.kd, newPidParams.centerAngle);
            
            storageWritePidParams(newPidParams);                                    
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void attitudeControl(void *pvParameters){

    queueReceiveControl = xQueueCreate(1, sizeof(control_app_t));

    control_app_t newControlVal;

    while(true){
        if( xQueueReceive(queueReceiveControl,
                         &newControlVal,
                         ( TickType_t ) 1 ) == pdPASS ){

            attitudeControlMotor.motorL = newControlVal.axis_x * -1 * VEL_MAX_CONTROL + newControlVal.axis_y * -1 * VEL_MAX_CONTROL;
            attitudeControlMotor.motorR = newControlVal.axis_x *  VEL_MAX_CONTROL + newControlVal.axis_y * -1 * VEL_MAX_CONTROL;
            
            setVelMotors(attitudeControlMotor.motorL,attitudeControlMotor.motorR);

            if( !attitudeControlMotor.motorL && !attitudeControlMotor.motorR){
                disableMotors();
            }
            else{
                enableMotors();
            }

            // printf("CONTROL RECIBIDO: X: %ld, Y: %ld, motorL: %d ,motorR: %d \n",newControlVal.axis_x,newControlVal.axis_y,attitudeControlMotor.motorL,attitudeControlMotor.motorR);
        } 
    }
}

void app_main() {
    // uint16_t distance = 0;
    uint8_t cont1=0,cont2=0,cont3=0;
    pid_params_t readParams={0};

    gpio_set_direction(PIN_LED , GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LED, 0);

    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[PIN_OSCILO], PIN_FUNC_GPIO);
    gpio_set_direction(PIN_OSCILO , GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_OSCILO, 0);


    storageInit();

    bt_init();
    mpu_init();
    readParams = storageReadPidParams();
    printf("center: %f kp: %f , ki: %f , kd: %f\n",readParams.centerAngle,readParams.kp,readParams.ki,readParams.kd);
    pidInit(readParams);

    motorsInit();

    // tfMiniInit();
    // canInit(GPIO_CAN_TX,GPIO_CAN_RX,UART_PORT_CAN);

    xTaskCreatePinnedToCore(imuControlHandler,"Imu Control Task",4096,NULL,IMU_HANDLER_PRIORITY,NULL,IMU_HANDLER_CORE);
    xTaskCreate(updateParams,"Update Params Task",2048,NULL,3,NULL);
    xTaskCreate(attitudeControl,"attitude control Task",2048,NULL,4,NULL);


    setVelMotors(0,0);

    while(1){

    //    printf("angleX: %f , angleY: %f\n",getAngle(AXIS_ANGLE_X),getAngle(AXIS_ANGLE_Y));

        // distance = tfMiniGetDist();
        // printf("angleX: %f  , distance: %d\n",angleX,distance);
         
        // float vNewAngles[3];
        // if(xQueueReceive(newAnglesQueue,&vNewAngles,0)){
        //     printf("angleX: %f \n",vNewAngles[0]);
        // }
        
        if(btIsConnected()){
            cont1++;
            if(cont1>100){
                cont1 =0;
            }

            cont2++;
            if(cont2>4){
                cont2 =0;
            }

            cont3++;
            if(cont3>200){
                cont3 =0;
            }

            // statusToSend={
            //         .header = HEADER_COMMS,
            //         .bat_voltage = 10,//cont1*10,
            //         .bat_percent = cont3,
            //         .batTemp = 100-cont1,
            //         .temp_uc_control = cont1,
            //         .temp_uc_main = 123-cont1,
            //         .speedR = 100,
            //         .speedL = -100,
            //         .pitch = getAngle(AXIS_ANGLE_X),
            //         .roll = getAngle(AXIS_ANGLE_Y),
            //         .yaw = getAngle(AXIS_ANGLE_Z),
            //         .centerAngle = 0,
            //         .P = 10,
            //         .I = 20,
            //         .D = 30,   
            //         .orden_code = 15,
            //         .error_code = cont2,//ERROR_CODE_INIT,
            //         .checksum = 0
            //     };

            statusToSend.header = HEADER_COMMS;
            statusToSend.bat_voltage = 10;
            statusToSend.bat_percent = cont3;
            statusToSend.batTemp = 100-cont1;
            statusToSend.temp_uc_control = cont1;
            statusToSend.temp_uc_main = 123-cont1;
            statusToSend.P = 10;
            statusToSend.I = 20;
            statusToSend.D = 30;   
            sendStatus(statusToSend);
        }
        gpio_set_level(PIN_LED,1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(PIN_LED,0);
        vTaskDelay(pdMS_TO_TICKS(50));
        
    }
}