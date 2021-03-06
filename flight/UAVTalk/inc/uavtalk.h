/**
 ******************************************************************************
 * @addtogroup OpenPilotSystem OpenPilot System
 * @{
 * @addtogroup OpenPilotLibraries OpenPilot System Libraries
 * @{
 * @file       uavtalk.h
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      Include file of the UAVTalk library
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

#ifndef UAVTALK_H
#define UAVTALK_H

// Public types
typedef int32_t (*UAVTalkOutputStream)(uint8_t* data, int32_t length);

typedef struct {
    uint32_t txBytes;
    uint32_t rxBytes;
    uint32_t txObjectBytes;
    uint32_t rxObjectBytes;
    uint32_t rxObjects;
    uint32_t txObjects;
    uint32_t txErrors;
    uint32_t rxErrors;
} UAVTalkStats;

typedef void* UAVTalkConnection;

// Public functions
UAVTalkConnection UAVTalkInitialize(UAVTalkOutputStream outputStream, uint32_t maxPacketSize);
int32_t UAVTalkSetOutputStream(UAVTalkConnection connection, UAVTalkOutputStream outputStream);
UAVTalkOutputStream UAVTalkGetOutputStream(UAVTalkConnection connection);
int32_t UAVTalkSendObject(UAVTalkConnection connection, UAVObjHandle obj, uint16_t instId, uint8_t acked, int32_t timeoutMs);
int32_t UAVTalkSendObjectRequest(UAVTalkConnection connection, UAVObjHandle obj, uint16_t instId, int32_t timeoutMs);
int32_t UAVTalkProcessInputStream(UAVTalkConnection connection, uint8_t rxbyte);
void UAVTalkGetStats(UAVTalkConnection connection, UAVTalkStats *stats);
void UAVTalkResetStats(UAVTalkConnection connection);

#endif // UAVTALK_H
/**
 * @}
 * @}
 */
