/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie Firmware
 *
 * Copyright (C) 2011-2012 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 */
#include "stm32f10x_conf.h"

#include "FreeRTOS.h"
#include "task.h"

#include "commander.h"
#include "crtp.h"
#include "configblock.h"

#define MIN_THRUST  10000
#define MAX_THRUST  60000

struct CommanderCrtpValues
{
  float roll;
  float pitch;
  float yaw;
  uint16_t thrust;
  bool hover;
} __attribute__((packed));

static struct CommanderCrtpValues targetVal[2];
static bool isInit;
static int side=0;
static uint32_t lastUpdate;
static bool isInactive;
static bool hoverMode=false;

static void commanderCrtpCB(CRTPPacket* pk);
static void commanderWatchdogReset(void);

void commanderInit(void)
{
  if(isInit)
    return;


  crtpInit();
  crtpRegisterPortCB(CRTP_PORT_COMMANDER, commanderCrtpCB);

  lastUpdate = xTaskGetTickCount();
  isInactive = TRUE;
  isInit = TRUE;
}

bool commanderTest(void)
{
  crtpTest();
  return isInit;
}

static void commanderCrtpCB(CRTPPacket* pk)
{
  targetVal[!side] = *((struct CommanderCrtpValues*)pk->data);
  side = !side;
  commanderWatchdogReset();
}

void commanderWatchdog(void)
{
  int usedSide = side;
  uint32_t ticktimeSinceUpdate;

  ticktimeSinceUpdate = xTaskGetTickCount() - lastUpdate;

  if (ticktimeSinceUpdate > COMMANDER_WDT_TIMEOUT_STABALIZE)
  {
    targetVal[usedSide].roll = 0;
    targetVal[usedSide].pitch = 0;
    targetVal[usedSide].yaw = 0;
  }
  if (ticktimeSinceUpdate > COMMANDER_WDT_TIMEOUT_SHUTDOWN)
  {
    targetVal[usedSide].thrust = 0;
    targetVal[usedSide].hover = false; // do we need this? It would reset the target altitude upon reconnect if still hovering
    isInactive = TRUE;
  }
  else
  {
    isInactive = FALSE;
  }
}

static void commanderWatchdogReset(void)
{
  lastUpdate = xTaskGetTickCount();
}

uint32_t commanderGetInactivityTime(void)
{
  return xTaskGetTickCount() - lastUpdate;
}

void commanderGetRPY(float* eulerRollDesired, float* eulerPitchDesired, float* eulerYawDesired)
{
  int usedSide = side;

  *eulerRollDesired  = targetVal[usedSide].roll;
  *eulerPitchDesired = targetVal[usedSide].pitch;
  *eulerYawDesired   = targetVal[usedSide].yaw;
}

void commanderGetHover(bool* hover, bool* set_hover, float* hover_change) {
    int usedSide = side;
    *hover = targetVal[usedSide].hover; // Still in hover mode
    *set_hover = !hoverMode && targetVal[usedSide].hover; // Hover just activated
    //*hover_change = targetVal[usedSide].hover ? targetVal[usedSide].thrust : 0; // Amount to change hover target
    *hover_change = targetVal[usedSide].hover ? ((float)targetVal[usedSide].thrust-32767.)/32767. : 0.0; // Amount to change hover target
    hoverMode = targetVal[usedSide].hover;
}


void commanderGetRPYType(RPYType* rollType, RPYType* pitchType, RPYType* yawType)
{
  *rollType  = ANGLE;
  *pitchType = ANGLE;
  *yawType   = RATE;
}

void commanderGetThrust(uint16_t* thrust)
{
  int usedSide = side;
  uint16_t rawThrust = targetVal[usedSide].thrust;

  if (rawThrust > MIN_THRUST)
  {
    *thrust = rawThrust;
  }
  else
  {
    *thrust = 0;
  }

  if (rawThrust > MAX_THRUST)
  {
    *thrust = MAX_THRUST;
  }

  commanderWatchdog();
}

