#ifndef _NET_CAN_H
#define _NET_CAN_H

enum CAN_CMD
{
  //CMD_PK_PU_EMIT = 0x12,
  CMD_LIT_CHANGE_STATE = 0x08,
  CMD_PU_PDU_INFO = 0x07,
  CMD_GLOBAL_EMIT = 0x14,
  CMD_PU_READY = 0x77,

  CMD_PU_PDU_IRP_AFU_INFO   = 2,
  CMD_PK_PU_EMIT            = 3,
  CMD_PU_PI_READY           = 5,

  CMD_VOZB_START_RAMP       = 0xA1,
  CMD_VOZB_STOP_RAMP        = 0xA0,

  CMD_VOZB_CMD              = 0xB0,
  CMD_VOZB_SET              = 0xB1,
  CMD_VOZB_GET              = 0xB2,
  
  CMD_VOZB_FLASH            = 0xBA,

  CMD_VOZB_SET_STORE        = 0xC0,  
  CMD_VOZB_GET_STORE        = 0xC1,
  CMD_VOZB_GET_STATUS       = 0xC2,

  CMD_VOZB_OK               = 0xF5,
  CMD_VOZB_ERROR            = 0xF0,
};

enum NET_ID
{
  NET_ID_PK = 0x10,

  NET_ID_VOZB_1 = 0xB0,
  NET_ID_VOZB_2 = 0xB1,
  NET_ID_VOZB_3 = 0xB2,
  NET_ID_VOZB_4 = 0xB3,

  NET_ID_UM_1 = 0xC0,
  NET_ID_UM_2 = 0xC1,
  NET_ID_UM_3 = 0xC2,
  NET_ID_UM_4 = 0xC3,
  NET_ID_UM_5 = 0xC4,

  NET_ID_PU = 0xD0,

  NET_ID_PI = 0xE0,

  NET_ID_PDU = 0xF0,

  NET_CAST = 0xFF
};

#endif

