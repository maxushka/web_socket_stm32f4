#ifndef __STRUCTS_H
#define __STRUCTS_H

#include <stdint.h>

#define JUMP_MAX_COUNT    8
#define MAX_LIT_CNT       4
#define MAX_VZ_CNT        4
#define AMP_MAX_COUNT     8
#define AMP_SET_COUNT     2

typedef enum
{
  MODE_BARRAGE = 0,
  MODE_WINDOW = 1,
  MODE_TARGET = 10
} tWorkMode;

typedef enum
{
  EMIT_OFF = 0,
  EMIT_ON = !EMIT_OFF
} tEmit;

struct auth
{
  char login[32];
  char hash[32];
};

struct device_info
{
  char device_id[16];
  char sn[16];
  struct auth admin;
  struct auth user;
  uint8_t ipaddr[4];
  uint8_t netmask[4];
  uint8_t macaddr[6];
};

typedef enum
{
  ERROR_NONE                  = 0,

  ERROR_VZ_CHANGE_MODE        = 0x01,
  ERROR_VZ_RAMP_OFF           = 0x02,
  ERROR_VZ_CLEAR_MODE         = 0x03,
  ERROR_VZ_SET_JUMP           = 0x04,
  ERROR_VZ_SET_BANK           = 0x05,
  ERROR_VZ_PWR_TIMEOUT        = 0x06,
  ERROR_VZ_PWR_FPGA_INIT      = 0x07,
  ERROR_VZ_PWR_RAMP_OFF       = 0x08,
  ERROR_VZ_PWR_NONE           = 0x09,
  ERROR_VZ_RESPONSE_TIMEOUT   = 0x0A,


  ERROR_MANAGE_EMIT       = 0x0B,
  ERROR_MAX_TEMPERATURE   = 0x0F,
  MAKE_ENUM_32_BIT        = 0xFFFFFFF
} tErrors;


struct fpga_cmd
{
  uint16_t sync;
  uint16_t cmd;
};

struct fpga_cmd_mod_erase
{
  uint16_t sync;
  uint16_t cmd;
  uint32_t mod;
  uint32_t crc8;
};

struct jump
{
  uint32_t jump_start_freq;
  uint32_t jump_stop_freq;
};

struct fpga_cmd_jump
{
  uint16_t sync;
  uint16_t cmd;
  uint32_t jump_en;
  uint16_t mode;
  uint16_t submod;

  uint32_t bank_cnt;
  uint32_t ext_ctrl;

  uint8_t ext_osk_en;
  uint8_t amp_set_num;
  uint8_t ext_flag1;
  uint8_t ext_flag2;

  uint32_t period;
  uint32_t duty_cycle; // 0-100
  uint32_t rnd_period;
  uint32_t rnd_duty_cycle;

  struct jump jump[JUMP_MAX_COUNT];
  uint32_t crc8;
};

struct bank
{
  uint32_t start_freq;
  uint32_t stop_freq;
  uint32_t step_freq;
  uint32_t step_mask;
  uint32_t run_mask;
  uint16_t phase_in;
  uint16_t next_bank;
  uint8_t phase_toggle_step;
  uint8_t flags_bank;
  uint8_t bank_rpt;
  uint8_t freq_rate;
  uint32_t step_freq_accel;
};

struct fpga_cmd_bank
{
  uint16_t sync;
  uint16_t cmd;
  uint16_t mode;
  uint16_t submod;
  uint32_t bank_num;
  struct bank bank;
  uint32_t crc8;
};

struct sublit_store
{
  char Name[16];
  uint32_t StartFreq;
  uint32_t StopFreq;
  uint8_t multiply_rf;
  uint8_t phase_toggle_allow;
  uint8_t res_1;
  uint8_t res_2;
  float PowerTreshhold;
};

struct litera_store
{
  char Name[16];  //yadim.46472796546
  char SN[16];    //serial num pcb
  uint32_t SELF_CAN_ID;   //CAN_ID
  uint32_t StartFreq;
  uint32_t StopFreq;
  uint32_t SynthFreq;
  
  uint8_t multisignal_allow;
  uint8_t can_filter_new;
  uint8_t res_1_2;
  uint8_t res_1_3;
  //uint32_t RES_1;

  uint32_t SublitCnt;
  uint32_t ADC_period_ms; // Period start ADC DMA
  uint32_t CAN_freq; //500 or 1000// if 0 then 1000

  uint32_t RES_2;
  uint32_t RES_3;
  uint32_t RES_4;

  struct sublit_store SubLit[2];
};

struct XAdcStatus
{
  uint16_t sync;
  uint16_t cmd;

  float max_Temp;
  float min_Temp;
  float curr_Temp;

  float max_VCCPINT;
  float min_VCCPINT;
  float curr_VCCPINT;

  float max_VccPaux;
  float min_VccPaux;
  float curr_VccPaux;

  float max_VccPdro;
  float min_VccPdro;
  float curr_VccPdro;

  float max_VccBram;
  float min_VccBram;
  float curr_VccBram;
  
  uint32_t crc8;
};

enum LOG_CMD
{
  LOG_IO_LOW          = 0x01,
  LOG_RESET_DDS       = 0x02,
  LOG_IO_TOGGLE       = 0x03,
  LOG_IO_HIGH         = 0x04,
  LOG_CHECK_DDS_POWER = 0x05,
  LOG_CMD_ERROR       = 0x06,
  LOG_CMD_OK          = 0x07,
  LOG_CMD_GET_XADC    = 0x37,
  LOG_CMD_BANK_OSK    = 0x08,
  LOG_CMD_FLASH_PARAM = 0x0A,
  LOG_CMD_UNKNOWN     = 0xFF,
  
  LOG_CMD_CHANGE_MOD  = 0xDE,
  LOG_POWER_NONE      = 0x6F,
};

struct sublit_status
{
  uint32_t vcc_18;
  uint32_t vcc_33;
  float power_det;
  enum LOG_CMD status_ok;
  uint8_t RES_1;
  uint8_t RES_2;
  
  //uint32_t status_ok;
  struct XAdcStatus fpga_status;
};

struct check_status
{
  uint8_t is_HSE_on_PCB; //if 1, then HSE on board(init SYS_CLK = 72 MHz), else if 0 init SYS_CLK = 64 MHz
  uint8_t is_CAN_on_1MBPS;
  uint8_t RES_A_3;
  uint8_t RES_A_4;
};

struct litera_status
{
  char SN[16];    //serial num pcb//uint32_t sn;
  uint32_t SELF_CAN_ID; //CAN_ID
  float stm_temperture;
  int8_t stm_temp_max; 
  int8_t stm_temp_min;
  
  struct check_status _check;

  struct sublit_status sublit[2];
};

struct amp
{
  uint16_t amp_in;
  uint16_t att_in;
  uint32_t amp_start_freq;
};

struct fpga_cmd_amp
{
  uint16_t sync;
  uint16_t cmd;
  struct amp amp[AMP_MAX_COUNT*AMP_SET_COUNT];
  uint32_t crc8;
};

struct fpga_cmd_change_mod
{
  uint16_t sync;
  uint16_t cmd;
  uint16_t mod;
  uint16_t sub_mod;
  uint32_t crc8;
};

enum BANK_CMD
{
  CMD_BANK_WRITE       = 0xF015,
  CMD_BANK_SET         = 0xF055,
  CMD_BANK_GET         = 0x7057,
};

enum FPGA_SYNC
{
  SYNC_FPGA_CMD   = 0xFAFB,
  SYNC_BANK     = 0xFAFC,
  SYNC_AMP     = 0xFAFE,
  SYNC_JUMP     = 0xFAFD,

  SYNC_FLASH_PAGE = 0xFAFF,
  SYNC_PAGE_ERASE = 0xFAF0,
  SYNC_MODE_ERASE = 0xFAF1,
  SYNC_CHANGE_MOD = 0xFAF2,
  SYNC_CMD_PAR   = 0xFAF3
};

enum ZYNQ_CMD
{
  CMD_RAMP_OFF       = 0x0015,
  CMD_RAMP_ON        = 0x0055,
  CMD_DDS_RST        = 0x02F0,
  CMD_BOOT           = 0x555F,
  CMD_GET_XADC       = 0x0550,
};

struct fpga_cmd_flash_page
{
  uint16_t sync;
  uint16_t cmd;
  uint32_t address;
  uint8_t page[256];
  uint32_t crc8;
};

struct fpga_cmd_page_erase
{
  uint16_t sync;
  uint16_t cmd;
  uint32_t address;
  uint32_t crc8;
};

#endif
