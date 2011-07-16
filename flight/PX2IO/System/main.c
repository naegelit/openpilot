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

#include <pios.h>


/* Task Priorities */
#define PROTOCOL_TASK_PRIORITY	(tskIDLE_PRIORITY + 2)
#define FAILSAFE_TASK_PRIORITY	(tskIDLE_PRIORITY + 3)

/* Global Variables */

/* Local Variables */
static xTaskHandle protocolTaskHandle;
static xTaskHandle failsafeTaskHandle;
#define PROTOCOL_TASK_STACK		(128 / 4)
#define FAILSAFE_TASK_STACK		(128 / 4)

/* Function Prototypes */
static void protocolTask(void *parameters);
static void failsafeTask(void *parameters);

/* Prototype of PIOS_Board_Init() function */
extern void PIOS_Board_Init(void);

int main()
{
	/* NOTE: Do NOT modify the following start-up sequence */
	/* Any new initialization functions should be added in OpenPilotInit() */

	/* Brings up System using CMSIS functions, enables the LEDs. */
	PIOS_SYS_Init();
	
	/* Do board init */
	PIOS_Board_Init();

	//static uint16_t speeds[] = {50, 50, 50, 50, 50, 50, 50, 50};
	//PIOS_Servo_SetHz(speeds, 8);

	//PIOS_Servo_Set(1, 1500);
	//PIOS_Servo_Set(2, 1500);
	//PIOS_Servo_Set(3, 1500);
	//PIOS_Servo_Set(4, 1500);
	//PIOS_Servo_Set(5, 1500);
	//PIOS_Servo_Set(6, 1500);
	//PIOS_Servo_Set(7, 1500);
	//PIOS_Servo_Set(0, 1500);

	/* start tasks */
	xTaskCreate(protocolTask, (const signed char *)"protocol", PROTOCOL_TASK_STACK, NULL, PROTOCOL_TASK_PRIORITY, &protocolTaskHandle);
	//TaskMonitorAdd(TASKINFO_RUNNING_PROTOCOL, protocolTaskHandle);
	//PIOS_WDG_RegisterFlag(PIOS_WDG_PROTOCOL);

	xTaskCreate(failsafeTask, (const signed char *)"failsafe", FAILSAFE_TASK_STACK, NULL, FAILSAFE_TASK_PRIORITY, &failsafeTaskHandle);
	//TaskMonitorAdd(TASKINFO_RUNNING_PROTOCOL, failsafeTaskHandle);
	//PIOS_WDG_RegisterFlag(PIOS_WDG_FAILSAFE);

	/* Start the FreeRTOS scheduler */
	vTaskStartScheduler();

	/* either we failed to start the scheduler, or it has returned unexpectedly */
	/* XXX might actually want to reboot here and hope the failure was transient? */
	PIOS_LED_Off(LED1);
	PIOS_LED_On(LED2);
	for(;;) {
		PIOS_LED_Toggle(LED1);
		PIOS_LED_Toggle(LED2);
		PIOS_DELAY_WaitmS(100);
	}

	return 0;
}

static void
protocolTask(void *parameters)
{
	PIOS_LED_Toggle(LED1);
	vTaskDelay(500 / portTICK_RATE_MS);
}

static void
failsafeTask(void *parameters)
{
	PIOS_LED_Toggle(LED2);
	vTaskDelay(100 / portTICK_RATE_MS);
}

/**
 * @}
 * @}
 */

