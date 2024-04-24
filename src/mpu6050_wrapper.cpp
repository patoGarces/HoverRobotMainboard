#include "../components/MPU6050/MPU6050.h"
#include "../components/MPU6050/MPU6050_6Axis_MotionApps20.h"

#include "mpu6050_wrapper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

QueueSetHandle_t newAnglesQueue;   // Envio angulos
static MPU6050 mpu;

#define PIN_SDA        32
#define PIN_CLK        33

quaternion_wrapper_t q;             // [w, x, y, z]         quaternion container
vector_float_wrapper_t gravity;     // [x, y, z]            gravity vector
float ypr[3];                       // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector
uint16_t packetSize = 42;           // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;                 // count of all bytes currently in FIFO
uint8_t fifoBuffer[64];             // FIFO storage buffer
uint8_t mpuIntStatus;               // holds actual interrupt status byte from MPU

void mpu6050Handler(void*){

    newAnglesQueue = xQueueCreate(1,sizeof(ypr));
	mpu6050_dmpInitialize();    // retorna 0 si la inicializacion fue exitosa

	// This need to be setup individually
	// mpu.setXGyroOffset(220);
	// mpu.setYGyroOffset(76);
	// mpu.setZGyroOffset(-85);
	// mpu.setZAccelOffset(1788);
    // mpu6050_calibrateAccel(6);
    // mpu6050_calibrateGyro(6);

	mpu6050_setDMPEnabled(true);

	while(1){
	    mpuIntStatus = mpu6050_getIntStatus();
		// get current FIFO count
		fifoCount = mpu6050_getFIFOCount();

	    if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
	        // reset so we can continue cleanly
	        mpu6050_resetFIFO();

	    // otherwise, check for DMP data ready interrupt frequently)
	    } else if (mpuIntStatus & 0x02) {
	        // wait for correct available data length, should be a VERY short wait
	        while (fifoCount < packetSize) fifoCount = mpu6050_getFIFOCount();

	        mpu6050_getFIFOBytes(fifoBuffer, packetSize);
            mpu6050_dmpGetQuaternion(&q,fifoBuffer);
            mpu6050_dmpGetGravity(&gravity,&q);
            mpu6050_dmpGetYawPitchRoll(ypr,&q,&gravity);

			// printf("YAW: %3.1f, ", ypr[0] * 180/M_PI);
			// printf("PITCH: %3.1f, ", ypr[1] * 180/M_PI);
			// printf("ROLL: %3.1f \n", ypr[2] * 180/M_PI);

			for(uint8_t i=0;i<3;i++){
				ypr[i] = ypr[i] * 180 / M_PI;
			}

            xQueueSend(newAnglesQueue,( void * ) &ypr, 1);
	    }

	    //Best result is to match with DMP refresh rate
	    // Its last value in components/MPU6050/MPU6050_6Axis_MotionApps20.h file line 310
	    // Now its 0x13, which means DMP is refreshed with 10Hz rate
		// vTaskDelay(5/portTICK_PERIOD_MS);
	}

	vTaskDelete(NULL);
}

void mpu6050_initialize() {
    
    i2c_config_t conf;
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = (gpio_num_t)PIN_SDA;
	conf.scl_io_num = (gpio_num_t)PIN_CLK;
	conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	conf.master.clk_speed = 400000;
	ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
	ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    mpu.initialize();

    xTaskCreate(mpu6050Handler,"mpu6050_handler_wrapper",4096,NULL,5,NULL);
}

int mpu6050_testConnection() {
    return mpu.testConnection() ? 1 : 0;
}

int mpu6050_dmpInitialize() {
    return mpu.dmpInitialize();
}

void mpu6050_calibrateAccel(int loops) {
    mpu.CalibrateAccel(loops);
}

void mpu6050_calibrateGyro(int loops) {
    mpu.CalibrateGyro(loops);
}

void mpu6050_setDMPEnabled(bool enable) {
    mpu.setDMPEnabled(enable);
}

uint8_t mpu6050_getIntStatus() {
    return mpu.getIntStatus();
}

uint16_t mpu6050_getFIFOCount() {
    return mpu.getFIFOCount();
}

void mpu6050_resetFIFO() {
    mpu.resetFIFO();
}

void mpu6050_getFIFOBytes(uint8_t *data, uint8_t length) {
    mpu.getFIFOBytes(data,length);
}

void mpu6050_dmpGetQuaternion(quaternion_wrapper_t *q, const uint8_t* packet) {
    mpu.dmpGetQuaternion(reinterpret_cast<Quaternion*>(q), packet);
}

uint8_t mpu6050_dmpGetGravity(vector_float_wrapper_t *v, const quaternion_wrapper_t *q) {
    // Copia el cuaternión a uno no constante
    quaternion_wrapper_t q_non_const = *q;
    uint8_t status = mpu.dmpGetGravity(reinterpret_cast<VectorFloat*>(v), reinterpret_cast<Quaternion*>(&q_non_const));
    return status;
}

void mpu6050_dmpGetYawPitchRoll(float *ypr, const quaternion_wrapper_t *q, const vector_float_wrapper_t *gravity) {
    quaternion_wrapper_t q_non_const = *q;
    // Accede al objeto VectorFloat desreferenciando directamente el puntero gravity
    const VectorFloat gravityVector = *reinterpret_cast<const VectorFloat*>(gravity);
    mpu.dmpGetYawPitchRoll(ypr, reinterpret_cast<Quaternion*>(&q_non_const), const_cast<VectorFloat*>(&gravityVector));
}

void mpu6050_getAcceleration(int16_t* ax, int16_t* ay, int16_t* az) {
    mpu.getAcceleration(ax, ay, az);
}

void mpu6050_getRotation(int16_t* gx, int16_t* gy, int16_t* gz) {
    mpu.getRotation(gx, gy, gz);
}

int16_t mpu6050_getTemperature() {
    return mpu.getTemperature();
}

void mpu6050_setAccelRange(uint8_t range) {
    mpu.setFullScaleAccelRange(range);
}

void mpu6050_setGyroRange(uint8_t range) {
    mpu.setFullScaleGyroRange(range);
}

void mpu6050_getMotion6(int16_t* ax, int16_t* ay, int16_t* az, int16_t* gx, int16_t* gy, int16_t* gz) {
    mpu.getMotion6(ax, ay, az, gx, gy, gz);
}

#ifdef __cplusplus
}
#endif
