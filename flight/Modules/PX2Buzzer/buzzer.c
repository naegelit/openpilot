/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup BatteryModule Battery Module
 * @brief Measures battery voltage and current
 * Updates the FlightBatteryState object
 * @{
 *
 * @file       battery.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      Module to read the battery Voltage and Current periodically and set alarms appropriately.
 *
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
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
 * Output object: FlightBatteryState
 *
 * This module will periodically generate information on the battery state.
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

#include "openpilot.h"

#ifdef PIOS_INCLUDE_BUZZER

//
// Configuration
//
#define STACK_SIZE_BYTES		200
#define BUZZER_TASK_PRIORITY	(tskIDLE_PRIORITY + 0)
#define CYCLE_LENGTH			40
#define MAX_MELODY_LENGTH		100

//#define ENABLE_DEBUG_MSG

#ifdef ENABLE_DEBUG_MSG
#define DEBUG_PORT			PIOS_COM_GPS
#define DEBUG_MSG(format, ...) PIOS_COM_SendFormattedString(DEBUG_PORT, format, ## __VA_ARGS__)
#else
#define DEBUG_MSG(format, ...)
#endif

// Private types
typedef struct _buzzer_tone {
	uint8_t duty_cycles;
	uint8_t pause_cycles;
	uint8_t note;
} buzzer_tone;

// Private variables
static xTaskHandle buzzerTaskHandle;
static uint8_t melody_play = 1;
static uint8_t melody_index = 0;

//Tetris Theme
static const uint8_t melody_len = 37;
static const buzzer_tone current_melody[MAX_MELODY_LENGTH] = {
		   {8, 1, 40}, {4, 1, 35}, {4, 1, 36}, {8, 1, 38}, {4, 1, 36}, {4, 1, 35}, {8, 1, 33}, {4, 1, 33},
		   {4, 1, 36}, {8, 1, 40}, {4, 1, 38}, {4, 1, 36}, {12, 1, 35}, {4, 1, 36}, {8, 1, 38}, {8, 1, 40}, {8, 1, 36}, {8, 1, 33}, {12, 1, 33},	//19

		   {8, 1, 38}, {4, 1, 41}, {8, 1, 45}, {4, 1, 43}, {4, 1, 41}, {12, 1, 40}, {4, 1, 36}, {8, 1, 40}, {4, 1, 38}, {4, 1, 36}, {8, 1, 35},
		   {4, 1, 35}, {4, 1, 36}, {8, 1, 38}, {8, 1, 40}, {8, 1, 36}, {8, 1, 33}, {12, 1, 33} };	//18
#if 0
	// For Elise...
	//const uint8_t melody_len = 54;
	//uint8_t melody_index = 0;
	//uint8_t melody[54] = { 52, 51, 52, 51, 52, 47, 50, 48, 45,
	//					   21, 28, 33, 36, 40, 45, 47,
	//					   16, 28, 32, 40, 44, 47, 48,
	//					   21, 28, 33, 40,
	//
	//					   52, 51, 52, 51, 52, 47, 50, 48, 45,
	//					   21, 28, 33, 36, 40, 45, 47,
	//					   16, 28, 32, 38, 48, 47, 45,
	//					   45, 45, 45, 45 };
#endif

// Private functions
static void buzzerTask(void *parameters);

/**
 * Initialize the module, called on startup
 * \returns 0 on success or -1 if initialization failed
 */

int32_t BuzzerInitialize(void)
{
	xTaskCreate(buzzerTask, (signed char *)"Buzzer", STACK_SIZE_BYTES/4, NULL, BUZZER_TASK_PRIORITY, &buzzerTaskHandle);
	return 0;
}

MODULE_INITCALL(BuzzerInitialize, 0)

/**
 * Module thread, should not return.
 */
static void buzzerTask(void *parameters)
{
	portTickType lastSysTime  = xTaskGetTickCount();
	while(1)
	{
		if (melody_play)
		{
			//Set Buzzer PWM frequency
			PIOS_Buzzer_SetNote(current_melody[melody_index].note);
			//activate buzzer timer (PWM signal starts here)
			PIOS_Buzzer_Ctrl(1);
			//delay duty
			vTaskDelayUntil(&lastSysTime, CYCLE_LENGTH*current_melody[melody_index].duty_cycles);
			//turn off PWM signal
			PIOS_Buzzer_Ctrl(0);
			//delay rest of note length
			vTaskDelayUntil(&lastSysTime, CYCLE_LENGTH*current_melody[melody_index].pause_cycles);

			melody_index = (melody_index+1) % melody_len;
			if (melody_index == 0) melody_play = 0;
		}
		else
		{
			vTaskDelayUntil(&lastSysTime, 500);
		}
	}
}
#endif
