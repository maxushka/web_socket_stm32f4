#ifndef _NET_CAN_H
#define _NET_CAN_H

enum CAN_CMD
{
//----------------------------------PCB Commands

  CMD_GET_DEV_STATUS_INFO   = 0x01,
  CMD_SET_DEV_STATUS_INFO   = 0x02,

  CMD_MANAGE_EMIT           = 0x11,
  CMD_MANAGE_EMIT_OK        = 0x12,
  CMD_MANAGE_EMIT_ERR       = 0x13,

  CMD_CHANGE_MODE           = 0x21,
  CMD_CHANGE_MODE_OK        = 0x22,
  CMD_CHANGE_MODE_ERR       = 0x23,

  CMD_CLEAR_MODE            = 0x31,
  CMD_CLEAR_MODE_OK         = 0x32,
  CMD_CLEAR_MODE_ERR        = 0x33,

  CMD_SEND_JUMP             = 0x41,
  CMD_SEND_JUMP_OK          = 0x42,
  CMD_SEND_JUMP_ERR         = 0x43,

  CMD_SEND_BANK             = 0x51,
  CMD_SEND_BANK_OK          = 0x52,
  CMD_SEND_BANK_ERR         = 0x53,

  CMD_SEND_AMP              = 0x61,
  CMD_SEND_AMP_OK           = 0x62,
  CMD_SEND_AMP_ERR          = 0x63,

  CMD_VZ_GET_STATE          = 0x71,
  CMD_VZ_GET_STATE_OK       = 0x72,
  CMD_VZ_GET_STATE_ERR      = 0x73,

  CMD_SET_MODE              = 0x81,
  CMD_SET_MODE_OK           = 0x82,
  CMD_SET_MODE_ERR          = 0x83,

  CMD_SAVE_MODE             = 0x91,
  CMD_SAVE_MODE_OK          = 0x92,
  CMD_SAVE_MODE_ERR         = 0x93,

//----------------------------------VZ

  CMD_VOZB_START_RAMP       = 0xA1,
  CMD_VOZB_STOP_RAMP        = 0xA0,

  CMD_VOZB_CMD              = 0xB0,
  CMD_VOZB_SET              = 0xB1,
  CMD_VOZB_GET              = 0xB2,
  
  CMD_VOZB_FLASH            = 0xBA,

  CMD_VOZB_SET_STORE        = 0xC0,  
  CMD_VOZB_GET_STORE        = 0xC1,
  CMD_VOZB_GET_STATUS       = 0xC2,
  CMD_VOZB_CHECK_PWR        = 0xC4,

  CMD_VOZB_OK               = 0xF5,
  CMD_VOZB_ERROR            = 0xF0,

//----------------------------------VZ

  CMD_NET_BROADCAST_INFO    = 0x10

};

enum NET_ID
{
  NET_ID_VOZB_1 = 0xB0,
  NET_ID_VOZB_2 = 0xB1,
  NET_ID_VOZB_3 = 0xB2,
  NET_ID_VOZB_4 = 0xB3,

  NET_ID_PU     = 0xD0,
  NET_ID_PI     = 0xE0,
  NET_ID_PDU    = 0xF0,

  NET_CAST      = 0xFF
};

typedef enum NET_ID tNetId;

#endif
