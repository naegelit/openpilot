/**
 ******************************************************************************
 * @addtogroup OpenPilotBL OpenPilot BootLoader
 * @brief These files contain the code to the OpenPilot MB Bootloader.
 *
 * @{
 * @file       main.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      This is the file with the main function of the OpenPilot BootLoader
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
/* Bootloader Includes */
#include <pios.h>
#include <pios_board_info.h>
#include "pios_opahrs.h"
#include "stopwatch.h"
#include "op_dfu.h"
//#include "usb_lib.h"
#include "pios_iap.h"
#include "ssp.h"
#include "fifo_buffer.h"
#include "dcc_stdio.h"
/* Prototype of PIOS_Board_Init() function */
extern void PIOS_Board_Init(void);
extern void FLASH_Download();
#define BSL_HOLD_STATE ((PIOS_USB_DETECT_GPIO_PORT->IDR & PIOS_USB_DETECT_GPIO_PIN) ? 0 : 1)

static void	do_jump(uint32_t stacktop, uint32_t entrypoint) __attribute__((noreturn));

/// LEDs PWM
uint32_t period1 = 50; // *100 uS -> 5 mS
uint32_t sweep_steps1 = 100; // * 5 mS -> 500 mS
uint32_t period2 = 50; // *100 uS -> 5 mS
uint32_t sweep_steps2 = 100; // * 5 mS -> 500 mS


////////////////////////////////////////
uint8_t tempcount = 0;

/// SSP SECTION
/// SSP TIME SOURCE
#define SSP_TIMER	TIM7
uint32_t ssp_time = 0;
#define MAX_PACKET_DATA_LEN	255
#define MAX_PACKET_BUF_SIZE	(1+1+MAX_PACKET_DATA_LEN+2)
#define UART_BUFFER_SIZE 1024
uint8_t rx_buffer[UART_BUFFER_SIZE] __attribute__ ((aligned(4)));
// align to 32-bit to try and provide speed improvement;
// master buffers...
uint8_t SSP_TxBuf[MAX_PACKET_BUF_SIZE];
uint8_t SSP_RxBuf[MAX_PACKET_BUF_SIZE];
void SSP_CallBack(uint8_t *buf, uint16_t len);
int16_t SSP_SerialRead(void);
void SSP_SerialWrite( uint8_t);
uint32_t SSP_GetTime(void);
PortConfig_t SSP_PortConfig = { .rxBuf = SSP_RxBuf,
		.rxBufSize = MAX_PACKET_DATA_LEN, .txBuf = SSP_TxBuf,
		.txBufSize = MAX_PACKET_DATA_LEN, .max_retry = 10, .timeoutLen = 1000,
		.pfCallBack = SSP_CallBack, .pfSerialRead = SSP_SerialRead,
		.pfSerialWrite = SSP_SerialWrite, .pfGetTime = SSP_GetTime, };
Port_t ssp_port;
t_fifo_buffer ssp_buffer;

/* Extern variables ----------------------------------------------------------*/
DFUStates DeviceState;
DFUPort ProgPort;
int16_t status = 0;
uint8_t JumpToApp = FALSE;
uint8_t GO_dfu = FALSE;
static uint8_t mReceive_Buffer[64];
/* Private function prototypes -----------------------------------------------*/
uint32_t LedPWM(uint32_t pwm_period, uint32_t pwm_sweep_steps, uint32_t count);
uint8_t processRX();
void jump_to_app();
uint32_t sspTimeSource();

#define BLUE LED1
#define RED	LED2
#define LED_PWM_TIMER	TIM6
int main() {
	/* NOTE: Do NOT modify the following start-up sequence */
	/* Any new initialization functions should be added in OpenPilotInit() */

	/* do basic PiOS init */
	PIOS_SYS_Init();

	/* init the IAP helper and check for a boot-to-DFU request */
	PIOS_IAP_Init();
	if (PIOS_IAP_CheckRequest() == TRUE) {
		GO_dfu = TRUE;
		PIOS_IAP_ClearRequest();
	}

	/* if DFU not forced, try to jump to the app */
	if (GO_dfu == FALSE)
		jump_to_app();

	/* if we get here, either jumping to the app failed or the app requested DFU mode */

	/* configure the board for bootloader use */
	PIOS_Board_Init();
	PIOS_COM_SendString(PIOS_COM_DEBUG, "FMU Bootloader\r\n");

	/* check whether USB us connected */
	PIOS_DELAY_WaitmS(10);	/* let the pin settle */

	/* configure for DFU */
	ProgPort = GPIO_ReadInputDataBit(PIOS_USB_DETECT_GPIO_PORT, PIOS_USB_DETECT_GPIO_PIN) ? Usb : Serial;

	/* set the initial state */
	DeviceState = DFUidle;

	STOPWATCH_Init(100, LED_PWM_TIMER);
	if (ProgPort == Serial) {
		fifoBuf_init(&ssp_buffer, rx_buffer, UART_BUFFER_SIZE);
		STOPWATCH_Init(100, SSP_TIMER);//nao devia ser 1000?
		STOPWATCH_Reset(SSP_TIMER);
		ssp_Init(&ssp_port, &SSP_PortConfig);
	}

	STOPWATCH_Reset(LED_PWM_TIMER);
	while (TRUE) {
		if (ProgPort == Serial) {
			ssp_ReceiveProcess(&ssp_port);
			status = ssp_SendProcess(&ssp_port);
			while ((status != SSP_TX_IDLE) && (status != SSP_TX_ACKED)) {
				ssp_ReceiveProcess(&ssp_port);
				status = ssp_SendProcess(&ssp_port);
			}
		}
		if (JumpToApp == TRUE)
			jump_to_app();
		JumpToApp = FALSE;		// if we come back, no point trying again

		switch (DeviceState) {
		case Last_operation_Success:
		case uploadingStarting:
		case DFUidle:
			period1 = 50;
			sweep_steps1 = 100;
			PIOS_LED_Off(RED);
			period2 = 0;
			break;
		case uploading:
			period1 = 50;
			sweep_steps1 = 100;
			period2 = 25;
			sweep_steps2 = 50;
			break;
		case downloading:
			period1 = 25;
			sweep_steps1 = 50;
			PIOS_LED_Off(RED);
			period2 = 0;
			break;
		case BLidle:
			period1 = 0;
			PIOS_LED_On(BLUE);
			period2 = 0;
			break;
		default://error
			period1 = 50;
			sweep_steps1 = 100;
			period2 = 50;
			sweep_steps2 = 100;
		}

		if (period1 != 0) {
			if (LedPWM(period1, sweep_steps1, STOPWATCH_ValueGet(LED_PWM_TIMER)))
				PIOS_LED_On(BLUE);
			else
				PIOS_LED_Off(BLUE);
		} else
			PIOS_LED_On(BLUE);

		if (period2 != 0) {
			if (LedPWM(period2, sweep_steps2, STOPWATCH_ValueGet(LED_PWM_TIMER)))
				PIOS_LED_On(RED);
			else
				PIOS_LED_Off(RED);
		} else
			PIOS_LED_Off(RED);

		if (STOPWATCH_ValueGet(LED_PWM_TIMER) > 100 * 50 * 100)
			STOPWATCH_Reset(LED_PWM_TIMER);
		if ((STOPWATCH_ValueGet(LED_PWM_TIMER) > 60000) && (DeviceState
				== BLidle))
			JumpToApp = TRUE;

		processRX();
		DataDownload(start);
	}
}



static void
do_jump(uint32_t stacktop, uint32_t entrypoint)
{
	asm volatile(
			"msr msp, %0	\n"
			"bx	%1			\n"
			: : "r" (stacktop), "r" (entrypoint) : );
	/* just to keep noreturn happy */
	for (;;) ;
}

void
jump_to_app()
{
	const uint32_t *fw_vec = (uint32_t *)(pios_board_info_blob.fw_base);

	if ((pios_board_info_blob.magic == PIOS_BOARD_INFO_BLOB_MAGIC) &&
		((fw_vec[0] & 0x2FF00000) == 0x20000000)) {

		FLASH_Lock();

		// XXX reset all peripherals here?
		do_jump(fw_vec[0], fw_vec[1]);

	} else {
		DeviceState = failed_jump;
		return;
	}
}

uint32_t LedPWM(uint32_t pwm_period, uint32_t pwm_sweep_steps, uint32_t count) {
	uint32_t pwm_duty = ((count / pwm_period) % pwm_sweep_steps)
			/ (pwm_sweep_steps / pwm_period);
	if ((count % (2 * pwm_period * pwm_sweep_steps)) > pwm_period
			* pwm_sweep_steps)
		pwm_duty = pwm_period - pwm_duty; // negative direction each 50*100 ticks
	return ((count % pwm_period) > pwm_duty) ? 1 : 0;
}

uint8_t processRX() {
	if (ProgPort == Serial) {

		if (fifoBuf_getUsed(&ssp_buffer) >= 63) {
			for (int32_t x = 0; x < 63; ++x) {
				mReceive_Buffer[x] = fifoBuf_getByte(&ssp_buffer);
			}
			processComand(mReceive_Buffer);
		}
	}
	return TRUE;
}

uint32_t sspTimeSource() {
	if (STOPWATCH_ValueGet(SSP_TIMER) > 5000) {
		++ssp_time;
		STOPWATCH_Reset(SSP_TIMER);
	}
	return ssp_time;
}
void SSP_CallBack(uint8_t *buf, uint16_t len) {
	fifoBuf_putData(&ssp_buffer, buf, len);
}
int16_t SSP_SerialRead(void) {
	if (PIOS_COM_ReceiveBufferUsed(PIOS_COM_TELEM_RF) > 0) {
		uint8_t byte;
		if (PIOS_COM_ReceiveBuffer(PIOS_COM_TELEM_RF, &byte, 1, 0) == 1) {
			return byte;
		} else {
			return -1;
		}	    
	} else
		return -1;
}
void SSP_SerialWrite(uint8_t value) {
	PIOS_COM_SendChar(PIOS_COM_TELEM_RF, value);
}
uint32_t SSP_GetTime(void) {
	return sspTimeSource();
}
