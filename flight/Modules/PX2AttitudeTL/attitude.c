/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup Attitude Copter Control Attitude Estimation
 * @brief Acquires sensor data and computes attitude estimate 
 * Specifically updates the the @ref AttitudeActual "AttitudeActual" and @ref AttitudeRaw "AttitudeRaw" settings objects
 * @{
 *
 * @file       attitude.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      Module to handle all comms to the AHRS on a periodic basis.
 *
 * @see        The GNU Public License (GPL) Version 3
 *
 ******************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/**
 * Input objects: None, takes sensor data via pios
 * Output objects: @ref AttitudeRaw @ref AttitudeActual
 *
 * This module computes an attitude estimate from the sensor data
 *
 * The module executes in its own thread.
 *
 * UAVObjects are automatically generated by the UAVObjectGenerator from
 * the object definition XML file.
 *
 * Modules have no API, all communication to other modules is done through UAVObjects.
 * However modules may use the API exposed by shared libraries.
 * See the OpenPilot wiki for more details.
 * http://www.openpilot.org/OpenPilot_Application_Architecture
 *
 */

#include "pios.h"
#include "attitude.h"
#include "attituderaw.h"
#include "attitudeactual.h"
#include "attitudematrix.h"
#include "attitudesettings.h"
#include "flightstatus.h"
#include "CoordinateConversions.h"
//#include "attitude_observer.h"
#include "attitude_tobi_laurens.h"

#include "pios_i2c_esc.h"
#include "mavlink_debug.h"

// Measurement range setup
#define GYRO_RANGE_500DPS

// Private constants
#define STACK_SIZE_BYTES			4096						// XXX re-evaluate
#define STACK_SIZE_SENSOR_BYTES		1024
#define STACK_SIZE_MAG_BYTES		512
#define ATTITUDE_TASK_PRIORITY	(tskIDLE_PRIORITY + 3)	// high
#define SENSOR_TASK_PRIORITY	(tskIDLE_PRIORITY + configMAX_PRIORITIES - 1)	// must be higher than attitude_task
#define MAG_TASK_PRIORITY	(tskIDLE_PRIORITY + configMAX_PRIORITIES - 2)	    // must be higher than attitude_task, but lower than sensors

// update/polling rates
// expressed in microseconds to evade float calculations in
// in C-preprocessor
// 5000 = 5 ms = 5000 us
#define UPDATE_INTERVAL_TICKS		(5 / portTICK_RATE_MS)		  // update every 5ms / 200 Hz
#define SENSOR_POLL_INTERVAL_TICKS	(1 / portTICK_RATE_MS)	      // poll sensors every 1ms / 1000 Hz (we get heavy problems if faster!!! XXX FIXME TODO)
#define MAG_POLL_INTERVAL_TICKS	(5 / portTICK_RATE_MS)			  // poll sensors every 5ms / 200 Hz (we get heavy problems if faster!!! XXX FIXME TODO)


// allow 100% extra sample space to allow the attitude update to run a bit late
#define MAX_SAMPLES_PER_UPDATE		(2 * (UPDATE_INTERVAL_TICKS / SENSOR_POLL_INTERVAL_TICKS))

// Private types
struct sample_buffer {
	struct pios_lis331_data		accel[MAX_SAMPLES_PER_UPDATE];
	int							accel_count;
	struct pios_l3g4200_data	gyro[MAX_SAMPLES_PER_UPDATE];
	int							gyro_count;
	struct pios_hmc5883_data	mag[MAX_SAMPLES_PER_UPDATE];
	int							mag_count;
};

// Private variables
static xTaskHandle attitudeTaskHandle;
static xTaskHandle sensorTaskHandle;
static xTaskHandle magTaskHandle;
static volatile struct sample_buffer sampleBuffer[2];
static struct pios_hmc5883_data savedMagData;
static volatile int activeSample = 0;
//static float accelKi = 0;
//static float accelKp = 0;
//static float yawBiasRate = 0;
//static bool zero_during_arming = false;

// Private functions
static void attitudeTask(void *parameters);
static void sensorTask(void *parameters);
static void magTask(void *parameters);
static void updateSensors(AttitudeRawData *attitudeRaw);
static void updateAttitude(AttitudeRawData *attitudeRaw);
//static void settingsUpdatedCb(UAVObjEvent * objEv);

int32_t PX2AttitudeTLStart()
{
	// Start the attitude task
	xTaskCreate(attitudeTask, (signed char *)"Attitude", STACK_SIZE_BYTES/4, NULL, ATTITUDE_TASK_PRIORITY, &attitudeTaskHandle);
	TaskMonitorAdd(TASKINFO_RUNNING_ATTITUDE, attitudeTaskHandle);
	PIOS_WDG_RegisterFlag(PIOS_WDG_ATTITUDE);
	// Kick off the sensor task now that the sensors are ready
	xTaskCreate(sensorTask, (signed char *)"AttSPISensors", STACK_SIZE_SENSOR_BYTES / 4, NULL, SENSOR_TASK_PRIORITY, &sensorTaskHandle);
	TaskMonitorAdd(TASKINFO_RUNNING_AHRSCOMMS, sensorTaskHandle);	// XXX really should get our own taskinfo

	xTaskCreate(magTask, (signed char *)"AttMagSensor", STACK_SIZE_MAG_BYTES / 4, NULL, MAG_TASK_PRIORITY, &magTaskHandle);

	// The attitude task is running, clear the alarm that would complain otherwise
	AlarmsClear(SYSTEMALARMS_ALARM_ATTITUDE);
	// Pose as AHRS comms in the sensor task
	AlarmsClear(SYSTEMALARMS_ALARM_AHRSCOMMS);

	return 0;
}

/**
 * Initialise the module, called on startup
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t PX2AttitudeTLInitialize(void)
{
	AttitudeActualInitialize();
	AttitudeMatrixInitialize();
	AttitudeRawInitialize();
//	AttitudeSettingsInitialize();

	// Initialize quaternion
	AttitudeActualData attitude;
	AttitudeActualGet(&attitude);
	attitude.q1 = 1;
	attitude.q2 = 0;
	attitude.q3 = 0;
	attitude.q4 = 0;
	AttitudeActualSet(&attitude);

	AttitudeMatrixData attitudeMatrix;
	AttitudeMatrixGet(&attitudeMatrix);
//TODO make identity matrix

	AttitudeMatrixSet(&attitudeMatrix);
	return 0;
}
MODULE_INITCALL(PX2AttitudeTLInitialize, PX2AttitudeTLStart)

/**
 * Module thread, should not return.
 */
static void attitudeTask(void *parameters)
{
	portTickType lastSysTime;
	//bool init = false;

	// Configure accel
	PIOS_LIS331_Init();
	PIOS_LIS331_SelectRate(LIS331_RATE_1000Hz);
	PIOS_LIS331_SetRange(LIS331_RANGE_8G);

	// Configure gyro
	PIOS_L3G4200_Init();
	PIOS_L3G4200_SelectRate(L3G4200_RATE_800Hz);
#ifdef GYRO_RANGE_500DPS
	PIOS_L3G4200_SetRange(L3G4200_RANGE_500dps);
#else
	PIOS_L3G4200_SetRange(L3G4200_RANGE_2000dps);
#endif

	// Configure magnetometer
	PIOS_HMC5883_Init();

	vTaskDelay(1);

	attitude_tobi_laurens_init();

	// Do one-time gyro/accel calibration here (?)
	// Load saved bias values, etc (?)



	PIOS_COM_SendString(PIOS_COM_DEBUG, "Attitude task running\r\n");

	// Record the time at which we have started
	lastSysTime = xTaskGetTickCount();

	// XXX THIS SHOULD NOT BE HERE
	//PIOS_I2C_ESC_Config();

	// Main task loop
	while (1) {
		AttitudeRawData attitudeRaw;

		// calm the watchdog
		PIOS_WDG_UpdateFlag(PIOS_WDG_ATTITUDE);
		
		// perform an attitude update
		updateSensors(&attitudeRaw);
		updateAttitude(&attitudeRaw);
		AttitudeRawSet(&attitudeRaw);

		// Delay until it is time to read the next sample
		vTaskDelayUntil(&lastSysTime, UPDATE_INTERVAL_TICKS);
	}
}

/**
 * Poll the accelerometer and gyro sensors for their most recent readings.
 *
 * For this to work well, the FreeRTOS timers should be running at maximum
 * priority, and the tick rate should be sufficiently high that this will be
 * as fast as or faster than the sensor sample rates.
 *
 * For PX2 this is nominally the case (1kHz tick rate, timers at max priority).
 * Alternatively, this would need to hijack a hardware timer and run at
 * interrupt context.
 */
static void sensorTask(void *parameters)
{
	volatile struct sample_buffer *sb;
	int ac;	// local copy to avoid aliasing rules
	int gc;	// local copy to avoid aliasing rules

	vTaskDelay(1);

	portTickType lastSysTime;

	lastSysTime = xTaskGetTickCount();
	for (;;) {
		sb = &sampleBuffer[activeSample];

		ac = sb->accel_count;	// local copy to avoid aliasing rules
		gc = sb->gyro_count;	// local copy to avoid aliasing rules

		// accumulate accel reading if available
		if (ac < MAX_SAMPLES_PER_UPDATE) {
			if (PIOS_LIS331_Read((struct pios_lis331_data *)&sb->accel[ac])) {
				sb->accel_count = ac + 1;
			}
		}

		// accumulate gyro reading if available
		if (gc < MAX_SAMPLES_PER_UPDATE) {
			if (PIOS_L3G4200_Read((struct pios_l3g4200_data *)&sb->gyro[gc])) {
				sb->gyro_count = gc + 1;
			}
		}

		// Pause until we are ready to poll again.
		//
		// Don't waste time trying to adjust the deadline based on the
		// difference between scheduled time and actual time - if we have been
		// delayed it's because the system already can't keep up, trying to run
		// sooner isn't going to help.
		vTaskDelayUntil(&lastSysTime, SENSOR_POLL_INTERVAL_TICKS);
	}
}

/**
 * Poll the accelerometer and gyro sensors for their most recent readings.
 *
 * For this to work well, the FreeRTOS timers should be running at maximum
 * priority, and the tick rate should be sufficiently high that this will be
 * as fast as or faster than the sensor sample rates.
 *
 * For PX2 this is nominally the case (1kHz tick rate, timers at max priority).
 * Alternatively, this would need to hijack a hardware timer and run at
 * interrupt context.
 */
static void magTask(void *parameters)
{
	volatile struct sample_buffer *sb;
	int mc;	// local copy to avoid aliasing rules


	vTaskDelay(1);

	portTickType lastSysTime;

	lastSysTime = xTaskGetTickCount();
	for (;;) {
		sb = &sampleBuffer[activeSample];
		mc = sb->mag_count; 	// local copy to avoid aliasing rules

		// accumulate mag reading if available
		if ((mc < MAX_SAMPLES_PER_UPDATE) && PIOS_HMC5883_NewDataAvailable()) {
			PIOS_HMC5883_ReadMag((struct pios_hmc5883_data *)&sb->mag[mc]);
			sb->mag_count = mc + 1;
		}

		// Pause until we are ready to poll again.
		//
		// Don't waste time trying to adjust the deadline based on the
		// difference between scheduled time and actual time - if we have been
		// delayed it's because the system already can't keep up, trying to run
		// sooner isn't going to help.
		vTaskDelayUntil(&lastSysTime, MAG_POLL_INTERVAL_TICKS);
	}
}


static void updateSensors(AttitudeRawData * attitudeRaw)
{
	volatile struct sample_buffer *sb;
	int other_sample;

	// prepare the unused sample buffer for capture
	other_sample = activeSample ^ 1;
	sb = &sampleBuffer[other_sample];
	sb->accel_count = 0;
	sb->gyro_count = 0;
	sb->mag_count = 0;

	// exchange sample buffers
	activeSample = other_sample;

	// and now address the fresh sample buffer, containing the last polling period's data
	other_sample = activeSample ^ 1;
	sb = &sampleBuffer[other_sample];

	// it's possible that we're polling faster than the magnetometer can update,
	// so if we have no data from it, use the saved values from the last loop
	if (sb->mag_count < 1) {
		sb->mag[0] = savedMagData;
		sb->mag_count = 1;
	} else {
		// save the last magnetometer reading for next time
		savedMagData = sb->mag[sb->mag_count - 1];
	}

	// if we have no accel/gyro data, we've spent an entire polling period without running a single timer
	// callout - that's worthy of an alarm
	if (!sb->accel_count || !sb->gyro_count) {
		AlarmsSet(SYSTEMALARMS_ALARM_ATTITUDE, SYSTEMALARMS_ALARM_WARNING);
		return;
	} else {
		AlarmsClear(SYSTEMALARMS_ALARM_ATTITUDE);
	}



	// if we have maxed the sample buffer, we are lagging ... is there an alarm we want here?
	if ((sb->accel_count == MAX_SAMPLES_PER_UPDATE) || (sb->gyro_count == MAX_SAMPLES_PER_UPDATE) || (sb->mag_count == MAX_SAMPLES_PER_UPDATE)) {
		// XXX what to do here, if anything?
	}

#if 1
	// Accumulate measurements (oversampling)
	{
//		int32_t ax;
//		int32_t ay;
//		int32_t az;
//
//		ax = ay = az = 0;
//		for (int i = 0; i < sb->gyro_count; i++) {
//			ax += sb->gyro[i].x;
//			ay += sb->gyro[i].y;
//			az += sb->gyro[i].z;
//		}
#if 1
		//		axs = 0.93f*axs+0.07f*(ax / sb->gyro_count);
		//		ays = 0.93f*ays+0.07f*(ay / sb->gyro_count);
		//		azs = 0.93f*azs+0.07f*(az / sb->gyro_count);


		static int32_t axs = 1;
		static int32_t ays = 1;
		static int32_t azs = 1;

		// Currently 15 Hz cut-off / 0.09
		const float t = 0.09f;//0.25f; //0.36

		for (int i = 0; i < sb->gyro_count; ++i)
		{
			axs = (1.0f-t)*axs+t*sb->gyro[i].x;
			ays = (1.0f-t)*ays+t*sb->gyro[i].y;
			azs = (1.0f-t)*azs+t*sb->gyro[i].z;
		}

		attitudeRaw->gyros[ATTITUDERAW_GYROS_X] = axs;
		attitudeRaw->gyros[ATTITUDERAW_GYROS_Y] = ays;
		attitudeRaw->gyros[ATTITUDERAW_GYROS_Z] = azs;
#else
		attitudeRaw->gyros[ATTITUDERAW_GYROS_X] = ax / sb->gyro_count;
		attitudeRaw->gyros[ATTITUDERAW_GYROS_Y] = ay / sb->gyro_count;
		attitudeRaw->gyros[ATTITUDERAW_GYROS_Z] = az / sb->gyro_count;
#endif

//		ax = ay = az = 0;
//		for (int i = 0; i < sb->accel_count; i++) {
//			ax += sb->accel[i].x;
//			ay += sb->accel[i].y;
//			az += sb->accel[i].z;
//		}
//		attitudeRaw->accels[ATTITUDERAW_ACCELS_X] = ax / sb->accel_count;
//		attitudeRaw->accels[ATTITUDERAW_ACCELS_Y] = ay / sb->accel_count;
//		attitudeRaw->accels[ATTITUDERAW_ACCELS_Z] = az / sb->accel_count;

		static int32_t accxs = 1;
		static int32_t accys = 1;
		static int32_t acczs = 1;

		for (int i = 0; i < sb->accel_count; ++i)
		{
			accxs = (1.0f-t)*accxs+t*sb->accel[i].x;
			accys = (1.0f-t)*accys+t*sb->accel[i].y;
			acczs = (1.0f-t)*acczs+t*sb->accel[i].z;
		}

		attitudeRaw->accels[ATTITUDERAW_ACCELS_X] = accxs;
		attitudeRaw->accels[ATTITUDERAW_ACCELS_Y] = accys;
		attitudeRaw->accels[ATTITUDERAW_ACCELS_Z] = acczs;

//
//		ax = ay = az = 0;
//		for (int i = 0; i < sb->mag_count; i++) {
//			ax += sb->mag[i].x;
//			ay += sb->mag[i].y;
//			az += sb->mag[i].z;
//		}
//		attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_X] = ax / sb->mag_count;
//		attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_Y] = ay / sb->mag_count;
//		attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_Z] = az / sb->mag_count;

		static int32_t magxs = 1;
		static int32_t magys = 1;
		static int32_t magzs = 1;

		for (int i = 0; i < sb->mag_count; ++i)
		{
			magxs = (1.0f-t)*magxs+t*sb->mag[i].x;
			magys = (1.0f-t)*magys+t*sb->mag[i].y;
			magzs = (1.0f-t)*magzs+t*sb->mag[i].z;
		}

		attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_X] = magxs;
		attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_X] = magys;
		attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_X] = magzs;
	}
#else
	attitudeRaw->gyros[ATTITUDERAW_GYROS_X] = sb->gyro->x;
	attitudeRaw->gyros[ATTITUDERAW_GYROS_Y] = sb->gyro->y;
	attitudeRaw->gyros[ATTITUDERAW_GYROS_Z] = sb->gyro->z;

	attitudeRaw->accels[ATTITUDERAW_ACCELS_X] = sb->accel->x;
	attitudeRaw->accels[ATTITUDERAW_ACCELS_Y] = sb->accel->y;
	attitudeRaw->accels[ATTITUDERAW_ACCELS_Z] = sb->accel->z;

	attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_X] = sb->mag->x;
	attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_Y] = sb->mag->y;
	attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_Z] = sb->mag->z;
#endif
}

#define MAG_OFFSET_X 0
#define MAG_OFFSET_Y (-200)
#define MAG_OFFSET_Z 0

#define MAG_SCALE_X (0.4f*1090.0f)
#define MAG_SCALE_Y (0.4f*1090.0f)
#define MAG_SCALE_Z (0.4f*1090.0f)

static void updateAttitude(AttitudeRawData * attitudeRaw)
{
	//all measurement vectors need to be turn into the body frame
	//z negative; x and y exchanged.
	float_vect3 gyro; //rad/s
#ifdef GYRO_RANGE_500DPS
	gyro.x = attitudeRaw->gyros[ATTITUDERAW_GYROS_Y] * 0.00026631611 /* = gyro * (500.0f / 180.0f * pi / 32768.0f ) */;
	gyro.y = attitudeRaw->gyros[ATTITUDERAW_GYROS_X] * 0.00026631611 /* = gyro * (500.0f / 180.0f * pi / 32768.0f ) */;
	gyro.z = - attitudeRaw->gyros[ATTITUDERAW_GYROS_Z] * 0.00026631611 /* = gyro * (500.0f / 180.0f * pi / 32768.0f ) */;
#else
	gyro.x = attitudeRaw->gyros[ATTITUDERAW_GYROS_Y] * 0.00106526444f /* = gyro * (2000.0f / 180.0f * pi / 32768.0f ) */;
	gyro.y = attitudeRaw->gyros[ATTITUDERAW_GYROS_X] * 0.00106526444f /* = gyro * (2000.0f / 180.0f * pi / 32768.0f ) */;
	gyro.z = - attitudeRaw->gyros[ATTITUDERAW_GYROS_Z] * 0.00106526444f /* = gyro * (2000.0f / 180.0f * pi / 32768.0f ) */;
#endif

	float_vect3 accel; //length 1 = / 4096
	accel.x = attitudeRaw->accels[ATTITUDERAW_ACCELS_Y] * 0.000244140625f; // = accel * (1 / 32768.0f / 8.0f * 9.81f);
	accel.y = attitudeRaw->accels[ATTITUDERAW_ACCELS_X] * 0.000244140625f; // = accel * (1 / 32768.0f / 8.0f * 9.81f);
	accel.z = - attitudeRaw->accels[ATTITUDERAW_ACCELS_Z] * 0.000244140625f; // = accel * (1 / 32768.0f / 8.0f * 9.81f);

	float_vect3 mag; //length 1
	mag.x = (attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_Y] - MAG_OFFSET_Y) / MAG_SCALE_Y;
	mag.y = (attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_X] - MAG_OFFSET_X) / MAG_SCALE_X;
	mag.z = - (attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_Z] - MAG_OFFSET_Z) / MAG_SCALE_Z;


	attitude_tobi_laurens(&accel, &mag, &gyro);


//	attitude_observer_correct_accel(accel, 1/200.0f);
//
//#if 1
//	attitude_observer_correct_magnet(mag, 1/200.0f);
//#endif
//
//	attitude_observer_correct_gyro(gyro);

//	float_vect3 tmp;//, angularRates;
//	attitude_tobi_laurens_get_euler(&tmp);
//	attitude_observer_get_angles(&angles, &angularRates);
	AttitudeMatrixData attitudeMatrix;

	attitude_tobi_laurens_get_all((float_vect3 *) &(attitudeMatrix.Roll), (float_vect3 *)&(attitudeMatrix.AngularRates[0]), (float_vect3 *)&(attitudeMatrix.RotationMatrix[0]), (float_vect3 *)&(attitudeMatrix.RotationMatrix[3]), (float_vect3 *)&(attitudeMatrix.RotationMatrix[6]));
	AttitudeMatrixSet(&attitudeMatrix);

//	debug_vect("ang", (float_vect3 *) &(attitudeMatrix.Roll));
//	debug_vect("x_n_b", (float_vect3 *)

//	attitudeMatrix.Roll=tmp.x;
//	attitudeMatrix.Pitch=tmp.y;
//	attitudeMatrix.Yaw=tmp.z;
//	debug_vect("rates",attitudeMatrix.AngularRates[0],attitudeMatrix.AngularRates[1],attitudeMatrix.AngularRates[2]);

	AttitudeActualData attitudeActual;
	attitudeActual.Roll  = attitudeMatrix.Roll * 57.2957795f;
	attitudeActual.Pitch = attitudeMatrix.Pitch * 57.2957795f;
	attitudeActual.Yaw   = attitudeMatrix.Yaw* 57.2957795f;

	attitudeActual.RollRate  = attitudeMatrix.AngularRates[ATTITUDEMATRIX_ANGULARRATES_X] * 57.2957795f;
	attitudeActual.PitchRate = attitudeMatrix.AngularRates[ATTITUDEMATRIX_ANGULARRATES_Y] * 57.2957795f;
	attitudeActual.YawRate   = attitudeMatrix.AngularRates[ATTITUDEMATRIX_ANGULARRATES_Z] * 57.2957795f;

	AttitudeActualSet(&attitudeActual);


	//attitude_observer_predict(1/200.0f);
}


/**
  * @}
  * @}
  */
