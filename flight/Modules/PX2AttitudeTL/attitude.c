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
#include "attitudesettings.h"
#include "flightstatus.h"
#include "CoordinateConversions.h"
//#include "attitude_observer.h"
#include "attitude_tobi_laurens.h"

#include "pios_i2c_esc.h"

// Private constants
#define STACK_SIZE_BYTES		5120						// XXX re-evaluate
#define ATTITUDE_TASK_PRIORITY	(tskIDLE_PRIORITY + 3)	// high
#define SENSOR_TASK_PRIORITY	(tskIDLE_PRIORITY + configMAX_PRIORITIES - 1)	// must be higher than attitude_task

// update/polling rates
#define UPDATE_INTERVAL_TICKS		(5 / portTICK_RATE_MS)			// update every 5ms
#define SENSOR_POLL_INTERVAL_TICKS	(1  / portTICK_RATE_MS)			// poll sensors every 2ms

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
static volatile struct sample_buffer sampleBuffer[2];
static struct pios_hmc5883_data savedMagData;
static volatile int activeSample;
//static float accelKi = 0;
//static float accelKp = 0;
//static float yawBiasRate = 0;
//static bool zero_during_arming = false;

// Private functions
static void attitudeTask(void *parameters);
static void sensorTask(void *parameters);
static void updateSensors(AttitudeRawData *attitudeRaw);
static void updateAttitude(AttitudeRawData *attitudeRaw);
//static void settingsUpdatedCb(UAVObjEvent * objEv);

int32_t PX2AttitudeTLStart()
{
	// Start the attitude task
	xTaskCreate(attitudeTask, (signed char *)"Attitude", STACK_SIZE_BYTES/4, NULL, ATTITUDE_TASK_PRIORITY, &attitudeTaskHandle);
	TaskMonitorAdd(TASKINFO_RUNNING_ATTITUDE, attitudeTaskHandle);
	PIOS_WDG_RegisterFlag(PIOS_WDG_ATTITUDE);

	return 0;
}

/**
 * Initialise the module, called on startup
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t PX2AttitudeTLInitialize(void)
{
	AttitudeActualInitialize();
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
	PIOS_LIS331_SelectRate(LIS331_RATE_400Hz);
	PIOS_LIS331_SetRange(LIS331_RANGE_8G);

	// Configure gyro
	PIOS_L3G4200_Init();
	PIOS_L3G4200_SelectRate(L3G4200_RATE_400Hz);
	PIOS_L3G4200_SetRange(L3G4200_RANGE_2000dps);

	// Configure magnetometer
	PIOS_HMC5883_Init();
	vTaskDelay(1);

	// initialize observer
//	float_vect3 accel_init = {0,0,9.81};
//	float_vect3 mag_init = {0,0,0};
//	attitude_observer_init(accel_init,mag_init);
	attitude_tobi_laurens_init();

	// Do one-time gyro/accel calibration here (?)
	// Load saved bias values, etc (?)

	// Kick off the sensor task now that the sensors are ready
	xTaskCreate(sensorTask, (signed char *)"AttitudeSensors", configMINIMAL_STACK_SIZE / 4, NULL, SENSOR_TASK_PRIORITY, &sensorTaskHandle);
	TaskMonitorAdd(TASKINFO_RUNNING_AHRSCOMMS, sensorTaskHandle);	// XXX really should get our own taskinfo

	// The attitude task is running, clear the alarm that would complain otherwise
	AlarmsClear(SYSTEMALARMS_ALARM_ATTITUDE);

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
		AttitudeRawGet(&attitudeRaw);
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
	int mc;	// local copy to avoid aliasing rules

	portTickType lastSysTime;

	lastSysTime = xTaskGetTickCount();
	for (;;) {
		sb = &sampleBuffer[activeSample];
		ac = sb->accel_count;	// local copy to avoid aliasing rules
		gc = sb->gyro_count;	// local copy to avoid aliasing rules
		mc = sb->mag_count; 	// local copy to avoid aliasing rules

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

		// accumulate mag reading if available
		if (/*PIOS_HMC5883_NewDataAvailable() && */(mc <= MAX_SAMPLES_PER_UPDATE)) {
			PIOS_HMC5883_ReadMag((struct pios_hmc5883_data *)&sb->mag[mc]);
			sb->mag_count = mc + 1;
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
		//AlarmsSet(SYSTEMALARMS_ALARM_ATTITUDE, SYSTEMALARMS_ALARM_ERROR);
		return;
	}

	// if we have maxed the sample buffer, we are lagging ... is there an alarm we want here?
	if ((sb->accel_count == MAX_SAMPLES_PER_UPDATE) || (sb->gyro_count == MAX_SAMPLES_PER_UPDATE) || (sb->mag_count == MAX_SAMPLES_PER_UPDATE)) {
		// XXX what to do here, if anything?
	}

#if 0
	// Accumulate measurements (oversampling)
	{
		int32_t ax;
		int32_t ay;
		int32_t az;

		ax = ay = az = 0;
		for (int i = 0; i < sb->gyro_count; i++) {
			ax += sb->gyro[i].x;
			ay += sb->gyro[i].y;
			az += sb->gyro[i].z;
		}
		attitudeRaw->gyros[ATTITUDERAW_GYROS_X] = ax / sb->gyro_count;
		attitudeRaw->gyros[ATTITUDERAW_GYROS_Y] = ay / sb->gyro_count;
		attitudeRaw->gyros[ATTITUDERAW_GYROS_Z] = az / sb->gyro_count;

		ax = ay = az = 0;
		for (int i = 0; i < sb->accel_count; i++) {
			ax += sb->accel[i].x;
			ay += sb->accel[i].y;
			az += sb->accel[i].z;
		}
		attitudeRaw->accels[ATTITUDERAW_ACCELS_X] = ax / sb->accel_count;
		attitudeRaw->accels[ATTITUDERAW_ACCELS_Y] = ay / sb->accel_count;
		attitudeRaw->accels[ATTITUDERAW_ACCELS_Z] = az / sb->accel_count;

		ax = ay = az = 0;
		for (int i = 0; i < sb->mag_count; i++) {
			ax += sb->mag[i].x;
			ay += sb->mag[i].y;
			az += sb->mag[i].z;
		}
		attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_X] = ax / sb->mag_count;
		attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_Y] = ay / sb->mag_count;
		attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_Z] = az / sb->mag_count;
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
#define MAG_OFFSET_Y 0
#define MAG_OFFSET_Z 0

#define MAG_SCALE_X 100.0f
#define MAG_SCALE_Y 100.0f
#define MAG_SCALE_Z 100.0f

static void updateAttitude(AttitudeRawData * attitudeRaw)
{
	float_vect3 gyro;
	gyro.x = attitudeRaw->gyros[ATTITUDERAW_GYROS_X] * 0.00106526444f /* = gyro * (2000.0f / 180.0f * pi / 32768.0f ) */;
	gyro.y = attitudeRaw->gyros[ATTITUDERAW_GYROS_Y] * 0.00106526444f /* = gyro * (2000.0f / 180.0f * pi / 32768.0f ) */;
	gyro.z = attitudeRaw->gyros[ATTITUDERAW_GYROS_Z] * 0.00106526444f /* = gyro * (2000.0f / 180.0f * pi / 32768.0f ) */;

	float_vect3 accel;
	accel.x = attitudeRaw->accels[ATTITUDERAW_ACCELS_X] * 0.000244140625f; // = accel * (1 / 32768.0f / 8.0f * 9.81f);
	accel.y = attitudeRaw->accels[ATTITUDERAW_ACCELS_Y] * 0.000244140625f; // = accel * (1 / 32768.0f / 8.0f * 9.81f);
	accel.z = attitudeRaw->accels[ATTITUDERAW_ACCELS_Z] * 0.000244140625f; // = accel * (1 / 32768.0f / 8.0f * 9.81f);

#if 1
	float_vect3 mag;
	mag.x = (attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_X] - MAG_OFFSET_X) / MAG_SCALE_X;
	mag.y = (attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_Y] - MAG_OFFSET_Y) / MAG_SCALE_Y;
	mag.z = (attitudeRaw->magnetometers[ATTITUDERAW_MAGNETOMETERS_Z] - MAG_OFFSET_Z) / MAG_SCALE_Z;
#endif

	attitude_tobi_laurens(&accel, &mag, &gyro);


//	attitude_observer_correct_accel(accel, 1/200.0f);
//
//#if 1
//	attitude_observer_correct_magnet(mag, 1/200.0f);
//#endif
//
//	attitude_observer_correct_gyro(gyro);

	float_vect3 angles;//, angularRates;
	attitude_tobi_laurens_get_euler(&angles);
//	attitude_observer_get_angles(&angles, &angularRates);


	AttitudeActualData attitudeActual;
	attitudeActual.Roll  = angles.x * 57.2957795f;
	attitudeActual.Pitch = angles.y * 57.2957795f;
	attitudeActual.Yaw   = angles.z * 57.2957795f;

	//attitudeActual.RollSpeed  = angularRates.x * 57.2957795f;
	//attitudeActual.PitchSpeed = angularRates.y * 57.2957795f;
	//attitudeActual.YawSpeed   = angularRates.z * 57.2957795f;

	AttitudeActualSet(&attitudeActual);

	//attitude_observer_predict(1/200.0f);
}

//// Filter states
//static float q[4] = {1,0,0,0};
//static float gyro_correct_int[3] = {0,0,0};
//
//static void updateAttitude(AttitudeRawData * attitudeRaw)
//{
//	static portTickType lastSysTime = 0;
//	static portTickType thisSysTime;
//
//	static float dT = 0;
//
//	thisSysTime = xTaskGetTickCount();
//	if(thisSysTime > lastSysTime) // reuse dt in case of wraparound
//		dT = (thisSysTime - lastSysTime) / portTICK_RATE_MS / 1000.0f;
//	lastSysTime = thisSysTime;
//
//	// Bad practice to assume structure order, but saves memory
//
//	// TODO FIXME scaling to SI units is here
//	float gyro[3];
//	gyro[0] = attitudeRaw->gyros[0] * 0.0610351562f /* = gyro / (32768.0f * 2000.0f) */ + gyro_correct_int[0];
//	gyro[1] = attitudeRaw->gyros[1] * 0.0610351562f /* = gyro / (32768.0f * 2000.0f) */ + gyro_correct_int[1];
//	gyro[2] = attitudeRaw->gyros[2] * 0.0610351562f /* = gyro / (32768.0f * 2000.0f) */ + gyro_correct_int[2];
//	gyro_correct_int[2] -= yawBiasRate * gyro[2];
//
//	{
//		float accels[3];
//
//		accels[0] = attitudeRaw->accels[ATTITUDERAW_ACCELS_X] * 0.00239501953f; // = accel / (32768.0f * 8.0f * 9.81f);
//		accels[1] = attitudeRaw->accels[ATTITUDERAW_ACCELS_Y] * 0.00239501953f; // = accel / (32768.0f * 8.0f * 9.81f);
//		accels[2] = attitudeRaw->accels[ATTITUDERAW_ACCELS_Z] * 0.00239501953f; // = accel / (32768.0f * 8.0f * 9.81f);
//		float grot[3];
//		float accel_err[3];
//
//		// Rotate gravity to body frame and cross with accels
//		grot[0] = -(2 * (q[1] * q[3] - q[0] * q[2]));
//		grot[1] = -(2 * (q[2] * q[3] + q[0] * q[1]));
//		grot[2] = -(q[0] * q[0] - q[1]*q[1] - q[2]*q[2] + q[3]*q[3]);
//		CrossProduct((const float *) accels, (const float *) grot, accel_err);
//
//		// Account for accel magnitude
//		float accel_mag = sqrt(accels[0]*accels[0] + accels[1]*accels[1] + accels[2]*accels[2]);
//		accel_err[0] /= accel_mag;
//		accel_err[1] /= accel_mag;
//		accel_err[2] /= accel_mag;
//
//		// Accumulate integral of error.  Scale here so that units are (rad/s) but Ki has units of s
//		gyro_correct_int[0] += accel_err[0] * accelKi;
//		gyro_correct_int[1] += accel_err[1] * accelKi;
//		//gyro_correct_int[2] += accel_err[2] * settings.AccelKI * dT;
//
//		// Correct rates based on error, integral component dealt with in updateSensors
//		gyro[0] += accel_err[0] * accelKp / dT;
//		gyro[1] += accel_err[1] * accelKp / dT;
//		gyro[2] += accel_err[2] * accelKp / dT;
//	}
//
//	{ // scoping variables to save memory
//		// Work out time derivative from INSAlgo writeup
//		// Also accounts for the fact that gyros are in deg/s
//		float qdot[4];
//		qdot[0] = (-q[1] * gyro[0] - q[2] * gyro[1] - q[3] * gyro[2]) * dT * M_PI / 180.0f / 2.0f;
//		qdot[1] = (q[0] * gyro[0] - q[3] * gyro[1] + q[2] * gyro[2]) * dT * M_PI / 180.0f / 2.0f;
//		qdot[2] = (q[3] * gyro[0] + q[0] * gyro[1] - q[1] * gyro[2]) * dT * M_PI / 180.0f / 2.0f;
//		qdot[3] = (-q[2] * gyro[0] + q[1] * gyro[1] + q[0] * gyro[2]) * dT * M_PI / 180.0f / 2.0f;
//
//		// Take a time step
//		q[0] = q[0] + qdot[0];
//		q[1] = q[1] + qdot[1];
//		q[2] = q[2] + qdot[2];
//		q[3] = q[3] + qdot[3];
//	}
//
//	// Renomalize
//	float qmag = sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
//	q[0] = q[0] / qmag;
//	q[1] = q[1] / qmag;
//	q[2] = q[2] / qmag;
//	q[3] = q[3] / qmag;
//
//	AttitudeActualData attitudeActual;
//	AttitudeActualGet(&attitudeActual);
//
//	quat_copy(q, &attitudeActual.q1);
//
//	// Convert into eueler degrees (makes assumptions about RPY order)
//	Quaternion2RPY(&attitudeActual.q1,&attitudeActual.Roll);
//
//	AttitudeActualSet(&attitudeActual);
//}

//static void settingsUpdatedCb(UAVObjEvent * objEv) {
//	AttitudeSettingsData attitudeSettings;
//	AttitudeSettingsGet(&attitudeSettings);
//
//
//	accelKp = attitudeSettings.AccelKp;
//	accelKi = attitudeSettings.AccelKi;
//	yawBiasRate = attitudeSettings.YawBiasRate;
//	gyroGain = attitudeSettings.GyroGain;
//
//	zero_during_arming = attitudeSettings.ZeroDuringArming == ATTITUDESETTINGS_ZERODURINGARMING_TRUE;
//
//	accelbias[0] = attitudeSettings.AccelBias[ATTITUDESETTINGS_ACCELBIAS_X];
//	accelbias[1] = attitudeSettings.AccelBias[ATTITUDESETTINGS_ACCELBIAS_Y];
//	accelbias[2] = attitudeSettings.AccelBias[ATTITUDESETTINGS_ACCELBIAS_Z];
//
//	// Indicates not to expend cycles on rotation
//	if(attitudeSettings.BoardRotation[0] == 0 && attitudeSettings.BoardRotation[1] == 0 &&
//	   attitudeSettings.BoardRotation[2] == 0) {
//		rotate = 0;
//
//		// Shouldn't be used but to be safe
//		float rotationQuat[4] = {1,0,0,0};
//		Quaternion2R(rotationQuat, R);
//	} else {
//		float rotationQuat[4];
//		const float rpy[3] = {attitudeSettings.BoardRotation[ATTITUDESETTINGS_BOARDROTATION_ROLL],
//			attitudeSettings.BoardRotation[ATTITUDESETTINGS_BOARDROTATION_PITCH],
//			attitudeSettings.BoardRotation[ATTITUDESETTINGS_BOARDROTATION_YAW]};
//		RPY2Quaternion(rpy, rotationQuat);
//		Quaternion2R(rotationQuat, R);
//		rotate = 1;
//	}
//}


/**
  * @}
  * @}
  */
