/**
 ******************************************************************************
 * @addtogroup UAVObjects OpenPilot UAVObjects
 * @{ 
 * @addtogroup $(NAME) $(NAME) 
 * @brief $(DESCRIPTION)
 *
 * Autogenerated files and functions for $(NAME) Object
 
 * @{ 
 *
 * @file       $(NAMELC).h
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      Implementation of the $(NAME) object. This file has been 
 *             automatically generated by the UAVObjectGenerator.
 * 
 * @note       Object definition file: $(XMLFILE). 
 *             This is an automatically generated file.
 *             DO NOT modify manually.
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

#include <string.h>
#include "mavlink_settings_adapter.h"

#include "openpilot.h"
#include "mavlink_types.h"

// START INCLUSION OF SETTINGS HEADERS
$(SETTINGSHEADERS)
// END INCLUSION OF SETTINGS HEADERS

// START INCLUSION OF INDIVIDUAL ADAPTER HEADERS
$(ADAPTERHEADERS)
// END INCLUSION OF INDIVIDUAL ADAPTER HEADERS


int16_t getParamIndexByName(const char* name)
{
	int16_t ret = -1;
//	ret = getActuatorSettingsParamIndexByName(name);
//	if (ret != -1) return ret;
	$(GETPARAMINDEXBYNAMELINES)
	// If none applies, return value
	return ret; // Return last state (== -1)
}

uint8_t getParamByIndex(uint16_t index, mavlink_param_union_t* param)
{
	uint8_t ret;
	ret = getActuatorSettingsParamByIndex(index, param);
	if (ret == MAVLINK_RET_VAL_PARAM_SUCCESS) return ret;
	return ret; // Return last state
}

uint8_t setParamByIndex(uint16_t index, const mavlink_param_union_t* param)
{
	uint8_t ret;
	ret = setActuatorSettingsParamByIndex(index, param);
	if (ret == MAVLINK_RET_VAL_PARAM_SUCCESS) return ret;
	return ret; // Return last state
}

const char* getParamNameByIndex(uint16_t index)
{
	char*
	return getActuatorSettingsParamNameByIndex(index);
}

uint16_t getParamCount()
{
	return MAX_ACTUATOR_PARAMS;
}


uint8_t getParamByName(const char* name, mavlink_param_union_t* param)
{
	int16_t index = -1;
	uint8_t ret;

	// Search for index as long as it stays not found (-1)

	// START INDEX FOUND SECTION
	if (index == -1) index = getActuatorSettingsParamIndexByName(name);
	// END INDEX FOUND SECTION

	if (index > -1)
	{
		// Break on first match

		// START VALUE FOUND SECTION
		ret = getActuatorSettingsParamByIndex(index, param);
		if (ret == MAVLINK_RET_VAL_PARAM_SUCCESS) return ret; // Else continue with other sub-sections
		// END VALUE FOUND SECTION
	}

	// No match, return false
	return MAVLINK_RET_VAL_PARAM_NAME_DOES_NOT_EXIST;
}

uint8_t setParamByName(const char* name, mavlink_param_union_t* param)
{
	int16_t index = -1;
	uint8_t ret;

	// Search for index as long as it stays not found (-1)

	// START INDEX FOUND SECTION
	if (index == -1) index = getActuatorSettingsParamIndexByName(name);
	// END INDEX FOUND SECTION

	if (index > -1)
	{
		// Break on first match

		// START VALUE FOUND SECTION
		ret = setActuatorSettingsParamByIndex(index, param);
		if (ret == MAVLINK_RET_VAL_PARAM_SUCCESS) return ret; // Else continue with other sub-sections
		// END VALUE FOUND SECTION
	}

	// No match, return false
	return MAVLINK_RET_VAL_PARAM_NAME_DOES_NOT_EXIST;
}


// CONTENT OF C-FILES - TO BE REMOVED

int16_t getActuatorSettingsParamIndexByName(const char* name)
{
	for (int i = 0; i < MAX_ACTUATOR_PARAMS; ++i)
	{
		bool match = true;
		const char* storage_name = getActuatorSettingsParamNameByIndex(i);
		for (uint8_t j = 0; j < ONBOARD_PARAM_NAME_LENGTH; ++j)
		{
			// Compare
			if (storage_name[j] != name[j])
			{
				match = false;
			}

			// End matching if null termination is reached
			if (storage_name[j] == '\0')
			{
				break;
			}
		}
		if (match) return i;
	}
	return -1;
}

const char* getActuatorSettingsParamNameByIndex(uint16_t index)
{
	switch (index)
	{
		case 0:
			return "fixedwing_roll1";
		case 1:
			return "fixedwing_roll2";
	}
	return 0;
}

uint8_t getActuatorSettingsParamByIndex(uint16_t index, mavlink_param_union_t* param)
{
	ActuatorSettingsData settings;
	ActuatorSettingsGet(&settings);
	switch (index)
	{
		case 0:
		{
			param->param_uint32 = settings.FixedWingRoll1;
			param->type = MAVLINK_TYPE_UINT32_T;
		}
			break;
		case 1:
		{
			param->param_uint32 = settings.FixedWingRoll2;
			param->type = MAVLINK_TYPE_UINT32_T;
		}
			break;
		default:
		{
			return MAVLINK_RET_VAL_PARAM_NAME_DOES_NOT_EXIST;
		}
			break;
	}
	// Not returned in default case, return true
	return MAVLINK_RET_VAL_PARAM_SUCCESS;
}

uint8_t setActuatorSettingsParamByIndex(uint16_t index, const mavlink_param_union_t* param)
{
	ActuatorSettingsData settings;
	ActuatorSettingsGet(&settings);
	switch (index)
	{
		case 0:
		{
			if (param->type == MAVLINK_TYPE_UINT32_T)
			{
				settings.FixedWingRoll1 = param->param_uint32;
			}
			else
			{
				return MAVLINK_RET_VAL_PARAM_TYPE_MISMATCH;
			}
		}
			break;
		case 1:
		{
			if (param->type == MAVLINK_TYPE_UINT32_T)
			{
				settings.FixedWingRoll2 = param->param_uint32;
			}
			else
			{
				return MAVLINK_RET_VAL_PARAM_TYPE_MISMATCH;
			}
		}
			break;
		default:
		{
			return MAVLINK_RET_VAL_PARAM_NAME_DOES_NOT_EXIST;
		}
			break;
	}

	// Not returned in default case, try to write (ok == 0) and return result of
	// write operation
	if (ActuatorSettingsSet(&settings) == 0)
	{
		return MAVLINK_RET_VAL_PARAM_SUCCESS;
	}
	else
	{
		return MAVLINK_RET_VAL_PARAM_WRITE_ERROR;
	}
}

int32_t writeParametersToStorage()
{
	//					// WORKING
	//					ObjectPersistenceData objper;
	//
	//					// Write all objects individually
	//					// for;;
	//					ObjectPersistenceGet(&objper);
	//
	//					ActuatorSettingsData settings;
	//					ActuatorSettingsGet(&settings);
	//
	//					objper.Selection = OBJECTPERSISTENCE_SELECTION_SINGLEOBJECT;
	//					objper.Operation = OBJECTPERSISTENCE_OPERATION_SAVE;
	//					objper.ObjectID = ACTUATORSETTINGS_OBJID;
	//					objper.InstanceID = 0;
	//					ObjectPersistenceSet(&objper);
	//					// END WORKING



	UAVObjHandle handle = ActuatorSettingsHandle();
	return UAVObjSave(handle, 0);
}

int32_t readParametersFromStorage()
{
	UAVObjHandle handle = ActuatorSettingsHandle();
	return UAVObjLoad(handle, 0);
}

/**
 * @}
 * @}
 */


#ifndef $(NAMEUC)_H
#define $(NAMEUC)_H

// Object constants
#define $(NAMEUC)_OBJID $(OBJIDHEX)
#define $(NAMEUC)_NAME "$(NAME)"
#define $(NAMEUC)_METANAME "$(NAME)Meta"
#define $(NAMEUC)_ISSINGLEINST $(ISSINGLEINST)
#define $(NAMEUC)_ISSETTINGS $(ISSETTINGS)
#define $(NAMEUC)_NUMBYTES sizeof($(NAME)Data)

// Object access macros
/**
 * @function $(NAME)Get(dataOut)
 * @brief Populate a $(NAME)Data object
 * @param[out] dataOut 
 */
#define $(NAME)Get(dataOut) UAVObjGetData($(NAME)Handle(), dataOut)
#define $(NAME)Set(dataIn) UAVObjSetData($(NAME)Handle(), dataIn)
#define $(NAME)InstGet(instId, dataOut) UAVObjGetInstanceData($(NAME)Handle(), instId, dataOut)
#define $(NAME)InstSet(instId, dataIn) UAVObjSetInstanceData($(NAME)Handle(), instId, dataIn)
#define $(NAME)ConnectQueue(queue) UAVObjConnectQueue($(NAME)Handle(), queue, EV_MASK_ALL_UPDATES)
#define $(NAME)ConnectCallback(cb) UAVObjConnectCallback($(NAME)Handle(), cb, EV_MASK_ALL_UPDATES)
#define $(NAME)CreateInstance() UAVObjCreateInstance($(NAME)Handle(),&$(NAME)SetDefaults)
#define $(NAME)RequestUpdate() UAVObjRequestUpdate($(NAME)Handle())
#define $(NAME)RequestInstUpdate(instId) UAVObjRequestInstanceUpdate($(NAME)Handle(), instId)
#define $(NAME)Updated() UAVObjUpdated($(NAME)Handle())
#define $(NAME)InstUpdated(instId) UAVObjUpdated($(NAME)Handle(), instId)
#define $(NAME)GetMetadata(dataOut) UAVObjGetMetadata($(NAME)Handle(), dataOut)
#define $(NAME)SetMetadata(dataIn) UAVObjSetMetadata($(NAME)Handle(), dataIn)
#define $(NAME)ReadOnly(dataIn) UAVObjReadOnly($(NAME)Handle())

// Object data
typedef struct {
$(DATAFIELDS)
} __attribute__((packed)) $(NAME)Data;

// Field information
$(DATAFIELDINFO)

// Generic interface functions
int32_t $(NAME)Initialize();
UAVObjHandle $(NAME)Handle();
void $(NAME)SetDefaults(UAVObjHandle obj, uint16_t instId);

// set/Get functions
$(SETGETFIELDSEXTERN)

#endif // $(NAMEUC)_H

/**
 * @}
 * @}
 */
