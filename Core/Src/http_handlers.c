#include "http_handlers.h"
#include "config.h"
#include "jsonlib.h"
#include "quadspi.h"
#include "utils.h"
#include "cmsis_os.h"
#include "jamm_structs.h"
#include "manage.h"
#include "fdcan.h"
#include "net_can.h"


#define SIZE_JSON_DUMP    4096

/** Variables */
char __IO *json_dump = (char __IO*)(SDRAM_JSON_DUMP_ADDRESS);
char __IO *json_banks_obj = (char __IO*)(SDRAM_BANKS_ADDRESS);

/** External variables */
extern struct webworker webworker;
extern struct dev_info device_info;
extern __IO uint8_t *qspi_mode_addr;
extern __IO uint8_t *qspi_std_addr;

extern struct litera_store letters_store[MAX_VZ_CNT];
extern int _crnt_letters_cnt;
extern int json_input_size;

extern xSemaphoreHandle CanHttpOk_Semaphore;
extern xQueueHandle GetAmp_Queue;
extern xQueueHandle GetJump_Queue;
extern xQueueHandle GetBank_Queue;
extern xQueueHandle GetState_Queue;

extern uint8_t SELF_NET_ID;

static char *GET_Handler(char *params);
static char *POST_Handler(char *url, char *json, struct webworker *web);
static void convert_ip(char *str_ip, uint8_t *out_ip);
static void convert_mac(char *str_mac, uint8_t *out_mac);

struct webworker webworker = {
  .getHandler = GET_Handler,
  .postHandler = POST_Handler,
};

struct fpga_cmd_jump cmd = {
  .sync = SYNC_JUMP,
  .cmd = CMD_BANK_WRITE,
  .crc8 = 0,
  .ext_ctrl = 0
};

struct fpga_cmd_amp cmd_amp = {
  .sync = SYNC_AMP,
  .cmd = 0,
  .crc8 = 0
};

struct fpga_cmd_bank cmd_bank = {
  .sync = SYNC_BANK,
  .cmd = CMD_BANK_WRITE,
  .crc8 = 0
};

struct fpga_cmd_flash_page page = {
  .sync = SYNC_FLASH_PAGE,
  .cmd = CMD_BANK_WRITE,
  .crc8 = 0
};

/**
 * [GET_Handler description]
 * @author M.Beletsky
 * @date   2020-09-21
 * @param  params     [description]
 * @return            [description]
 */
static char *GET_Handler(char *params)
{
  char tempParams[strlen(params)];
  strcpy(tempParams, params);
  char *pUrl = strtok(tempParams, "?");
  SysPkg_Typedef response;

  for (int i = 0; i < SIZE_JSON_DUMP; i++)
    json_dump[i] = 0;

  if (strcmp(pUrl, "/api/whouser") == 0)
  {
    json_create((char*)json_dump, "user", (void*)webworker.crnt_user, "string");
    json_create((char*)json_dump, "mode", (void*)(qspi_mode_addr+sizeof(uint32_t)), "obj");
    json_create((char*)json_dump, "std", (void*)(qspi_std_addr+sizeof(uint32_t)), "obj");
    if (strcmp(webworker.crnt_user, "admin") == 0)
      json_create((char*)json_dump, "hash", (void*)device_info.admin_hash, "string");
    else
      json_create((char*)json_dump, "hash", (void*)device_info.user_hash, "string");

    //char *tmp_ip = pvPortMalloc(16);
    char __IO *tmp_ip = (char __IO*)(0xC0560000);
    //if (tmp_ip != NULL)
    {
      memset((char*)tmp_ip, 0x00, 128);
      sprintf((char*)tmp_ip, "%d.%d.%d.%d", device_info.ipaddr[0], device_info.ipaddr[1], device_info.ipaddr[2], device_info.ipaddr[3]);
      json_create((char*)json_dump, "ipaddr", (void*)tmp_ip, "string");
      sprintf((char*)tmp_ip, "%d.%d.%d.%d", device_info.netmask[0], device_info.netmask[1], device_info.netmask[2], device_info.netmask[3]);
      json_create((char*)json_dump, "netmask", (void*)tmp_ip, "string");
      json_create((char*)json_dump, "dev_name", (void*)device_info.device_id, "string");
      json_create((char*)json_dump, "dev_sn", (void*)device_info.sn, "string");
      uint8_t hb1 = 0, hb2 = 0;
      for (int i = 0 ; i < 6; i++)
      {
        hb1 = device_info.macaddr[i] & 0xF;
        hb2 = (device_info.macaddr[i] >> 4) & 0xF;
        if (i == 5) {
          sprintf((char*)tmp_ip+(i*3), "%X%X", hb2, hb1);
        } else {
          sprintf((char*)tmp_ip+(i*3), "%X%X:", hb2, hb1);
        }
      }
      json_create((char*)json_dump, "dev_macaddr", (void*)tmp_ip, "string");
    }
    char __IO *vozb_all = (char __IO*)(0xC0560000);
    //char *vozb_all = pvPortMalloc(128*_crnt_letters_cnt*2);
    if (_crnt_letters_cnt != 0)
    {
      memset((char*)vozb_all, 0x00, 128*_crnt_letters_cnt*2);
      sprintf((char*)vozb_all, "%s", "[");
      char *amp_void = "[]\0";
      for (int iLit = 0; iLit < _crnt_letters_cnt; iLit++)
      {
        for (int iSub = 0; iSub < letters_store[iLit].SublitCnt; iSub++)
        {
          //char* vozb_obj = pvPortMalloc(128);
          char __IO *vozb_obj = (char __IO*)(0xC0570000);
          //if (vozb_obj != NULL)
          {
            memset((char*)vozb_obj, 0x00, 128);
            json_create((char*)vozb_obj, "can_id", (void*)letters_store[iLit].SELF_CAN_ID, "int");
            json_create((char*)vozb_obj, "name", (void*)letters_store[iLit].Name, "string");
            json_create((char*)vozb_obj, "start_freq", (void*)letters_store[iLit].SubLit[iSub].StartFreq, "int");
            json_create((char*)vozb_obj, "stop_freq", (void*)letters_store[iLit].SubLit[iSub].StopFreq, "int");
            json_create((char*)vozb_obj, "num_sublit", (void*)iSub, "int");

            //json_create(vozb_obj, "synth_freq", (void*)letters_store[iLit].SynthFreq, "int");
            json_create((char*)vozb_obj, "sublit_cnt", (void*)letters_store[iLit].SublitCnt, "int");
            json_create((char*)vozb_obj, "ext_osk_en", (void*)letters_store[iLit].signal_mod, "int");

            json_create((char*)vozb_obj, "amp", (void*)amp_void, "obj");
            strcat((char*)vozb_all, (char*)vozb_obj);
            if ( iLit != _crnt_letters_cnt-1 )
            {
              strcat((char*)vozb_all, ",");
            }
            else
            {
              if (iSub != letters_store[iLit].SublitCnt-1)
              {
                strcat((char*)vozb_all, ",");
              }
            }
            //vPortFree(vozb_obj);
          }
        }
      }
      strcat((char*)vozb_all, "]");
      json_create((char*)json_dump, "vozb", (void*)vozb_all, "obj");
      //vPortFree(vozb_all);
    }
    else
    {
      char *emp = "[  ]";
      json_create((char*)json_dump, "vozb", (void*)emp, "obj");
    }
    int size_vozb_edit_arr = (164*_crnt_letters_cnt*2);
    //char *vozb_edit_arr = pvPortMalloc(size_vozb_edit_arr);
    char __IO *vozb_edit_arr = (char __IO*)(0xC0560000);
    if (size_vozb_edit_arr != 0)
    {
      memset((char*)vozb_edit_arr, 0x00, size_vozb_edit_arr);
      sprintf((char*)vozb_edit_arr, "%s", "[");
      for (int iLit = 0; iLit < _crnt_letters_cnt; iLit++)
      {
        //char* vozb_edit_obj = pvPortMalloc(164);
        char __IO *vozb_edit_obj = (char __IO*)(0xC0570000);
        //if (vozb_edit_obj != NULL)
        {
          memset((char*)vozb_edit_obj, 0x00, 164);
          json_create((char*)vozb_edit_obj, "can_id", (void*)letters_store[iLit].SELF_CAN_ID, "int");
          json_create((char*)vozb_edit_obj, "name", (void*)letters_store[iLit].Name, "string");
          json_create((char*)vozb_edit_obj, "sn", (void*)letters_store[iLit].SN, "int");
          json_create((char*)vozb_edit_obj, "start_freq", (void*)letters_store[iLit].StartFreq, "int");
          json_create((char*)vozb_edit_obj, "stop_freq", (void*)letters_store[iLit].StopFreq, "int");
          json_create((char*)vozb_edit_obj, "synth_freq", (void*)letters_store[iLit].SynthFreq, "int");
          json_create((char*)vozb_edit_obj, "ext_osk_en", (void*)letters_store[iLit].signal_mod, "int");
          json_create((char*)vozb_edit_obj, "sublit_cnt", (void*)letters_store[iLit].SublitCnt, "int");
          strcat((char*)vozb_edit_arr, (char*)vozb_edit_obj);
          if ( iLit != _crnt_letters_cnt-1 )
          {
            strcat((char*)vozb_edit_arr, ",");
          }
          //vPortFree(vozb_edit_obj);
        }
      }
      strcat((char*)vozb_edit_arr, "]");
      json_create((char*)json_dump, "vozb_edit", (void*)vozb_edit_arr, "obj");
      //vPortFree(vozb_edit_arr);
    }
    else
    {
      char *emp = "[  ]";
      json_create((char*)json_dump, "vozb_edit", (void*)emp, "obj");
    }
  }
  else if (strcmp(pUrl, "/api/devinfo") == 0)
  {
    json_create((char*)json_dump, "device_id", (void*)device_info.device_id, "string");
    json_create((char*)json_dump, "sn", (void*)device_info.sn, "string");
    json_create((char*)json_dump, "mode", (void*)device_info.mode, "int");
    json_create((char*)json_dump, "sm", (void*)device_info.submode, "int");
    json_create((char*)json_dump, "emit", (void*)device_info.emit, "int");

    //char *tmp_arr = pvPortMalloc(128);
    char __IO *tmp_arr = (char __IO*)(0xC0560000);
    //if (tmp_arr != NULL)
    {
      memset((char*)tmp_arr, 0x00, 1024);
      sprintf((char*)tmp_arr, "[%d,%d,%d,%d]", device_info.irp[0], device_info.irp[1], device_info.irp[2], device_info.irp[3]);
      json_create((char*)json_dump, "irp", (void*)tmp_arr, "obj");
      sprintf((char*)tmp_arr, "[%d,%d,%d,%d]", device_info.afu[0], device_info.afu[1], device_info.afu[2], device_info.afu[3]);
      json_create((char*)json_dump, "afu", (void*)tmp_arr, "obj");
      sprintf((char*)tmp_arr, "[%d,%d,%d,%d]", device_info.lit_en[0], device_info.lit_en[1], device_info.lit_en[2], device_info.lit_en[3]);
      json_create((char*)json_dump, "lit_en", (void*)tmp_arr, "obj");
      //vPortFree(tmp_arr);
    }
  }
  else if (strcmp(pUrl, "/api/state-vz") == 0)
  {
    char __IO *vz_state_obj = (char __IO*)(0xC0500000);
    struct litera_status litera_status;
    sprintf((char*)json_dump, "[");
    
    for (int iVz = 0; iVz < _crnt_letters_cnt; iVz++)
    {
      response = Utils_CmdCreate(letters_store[iVz].SELF_CAN_ID, SELF_NET_ID, CMD_VOZB_GET_STATUS,
                                               sizeof(SysPkg_Typedef), NULL, false, 0, 0);
      FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&response, sizeof(SysPkg_Typedef));

      if (xQueueReceive(GetState_Queue, &(litera_status), pdMS_TO_TICKS(500)) == pdTRUE)
      {
        for (int iSub = 0; iSub < letters_store[iVz].SublitCnt; iSub++)
        {
          memset((char*)vz_state_obj, 0x00, 2048);
          json_create((char*)vz_state_obj, "name", (void*)letters_store[iVz].Name, "string");
          json_create((char*)vz_state_obj, "can_id", (void*)litera_status.SELF_CAN_ID, "int");
          json_create((char*)vz_state_obj, "num_sublit", (void*)iSub, "int");
          json_create((char*)vz_state_obj, "start_freq", (void*)letters_store[iVz].SubLit[iSub].StartFreq, "int");
          json_create((char*)vz_state_obj, "stop_freq", (void*)letters_store[iVz].SubLit[iSub].StopFreq, "int");
          json_create((char*)vz_state_obj, "temp_fpga", (void*)(int)litera_status.sublit[iSub].fpga_status.curr_Temp, "int");
          json_create((char*)vz_state_obj, "max_temp_fpga", (void*)(int)litera_status.sublit[iSub].fpga_status.max_Temp, "int");
          json_create((char*)vz_state_obj, "min_temp_fpga", (void*)(int)litera_status.sublit[iSub].fpga_status.min_Temp, "int");
          json_create((char*)vz_state_obj, "temp_stm", (void*)(int)litera_status.stm_temperture, "int");
          json_create((char*)vz_state_obj, "vcc_dds", (void*)litera_status.sublit[iSub].vcc_18, "int");
          json_create((char*)vz_state_obj, "vcc_fpga", (void*)litera_status.sublit[iSub].vcc_33, "int");
          json_create((char*)vz_state_obj, "pout_vz", (void*)(int)litera_status.sublit[iSub].power_det, "int");
          json_create((char*)vz_state_obj, "mode_vz", (void*)(int)device_info.mode_vz, "int");
          strcat((char*)json_dump, (char*)vz_state_obj);
          if ( iVz != _crnt_letters_cnt-1 )
          {
            strcat((char*)json_dump, ",");
          }
          else
          {
            if (iSub != letters_store[iVz].SublitCnt-1)
            {
              strcat((char*)json_dump, ",");
            }
          }
        }
      }
    }
    strcat((char*)json_dump, "]");
  }
  return (char*)json_dump;
}

/**
 * [POST_Handler description]
 * @author M.Beletsky
 * @date   2020-09-21
 * @param  url        [description]
 * @param  json       [description]
 * @param  web        [description]
 * @return            [description]
 */
static char *POST_Handler(char *url, char *json, struct webworker *web)
{
  for (int i = 0; i < SIZE_JSON_DUMP; i++)
    json_dump[i] = 0;
  
  if (strcmp(url, "api/auth") == 0)
  {
    json_get(json, "hash", (void*)json_dump);
    if (strcmp((char*)json_dump, device_info.admin_hash) == 0)
    {
      memset((char*)json_dump, 0x00, SIZE_JSON_DUMP);
      sprintf(web->crnt_user, "%s", "admin");
      json_create((char*)json_dump, "token", (void*)(web->token), "string");
      return (char*)json_dump;
    }
    else if (strcmp((char*)json_dump, device_info.user_hash) == 0)
    {
      memset((char*)json_dump, 0x00, SIZE_JSON_DUMP);
      sprintf(web->crnt_user, "%s", "user");
      json_create((char*)json_dump, "token", (void*)(web->token), "string");
      return (char*)json_dump;
    }
    else
    {
      sprintf(web->crnt_user, "%s", "");
    }
  }
  else if (strcmp(url, "api/change-mode") == 0)
  {
    int mode = 0, sm = 0, mode_vz = 0;
    json_get(json, "mode", (void*)&mode);
    device_info.mode = mode;
    json_get(json, "sm", (void*)&sm);
    device_info.submode = sm;
    json_get(json, "mode_vz", (void*)&mode_vz);
    device_info.mode_vz = mode_vz;

    /** Устанавливаем прежний режим */
    for (int iVz = 0; iVz < _crnt_letters_cnt; iVz++)
    {
      for (int iRamp = 0; iRamp < letters_store[iVz].SublitCnt; iRamp++)
      {
        FPGA_changeMode(letters_store[iVz].SELF_CAN_ID, iRamp, mode_vz, 0);
        /** Дожидаемся ответа и если его нет в течении 500 мс, повторяем отправку */
        if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
        {
          FPGA_changeMode(letters_store[iVz].SELF_CAN_ID, iRamp, mode_vz, 0);
          xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
        }
      }
    }
    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if (strcmp(url, "api/change-emit") == 0)
  {
    device_info.emit = !device_info.emit;
    SysPkg_Typedef request = Utils_CmdCreate(NET_CAST, SELF_NET_ID, CMD_GLOBAL_EMIT,
                                  sizeof(SysPkg_Typedef), NULL, false, (uint8_t)device_info.emit, 0);
    FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&request, sizeof(SysPkg_Typedef));

    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if (strcmp(url, "api/lit-en") == 0)
  {
    int lit = 0, en = 0;;
    json_get(json, "lit", (void*)&lit);
    json_get(json, "en", (void*)&en);
    device_info.lit_en[lit] = (uint8_t)en;
    SysPkg_Typedef request = Utils_CmdCreate(NET_CAST, SELF_NET_ID, CMD_LIT_CHANGE_STATE,
                                             sizeof(SysPkg_Typedef), NULL, false, (uint8_t)en, (uint8_t)lit);
    FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&request, sizeof(SysPkg_Typedef));

    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if (strcmp(url, "api/save-std") == 0)
  {
    CSP_QUADSPI_Init();
    uint32_t address = (QSPI_STD_ADDRESS-QSPI_START_ADDRESS);
    uint32_t size = (strstr(json, "}]")+strlen("}]")-json);
    if (CSP_QSPI_EraseSector(address, address+sizeof(uint32_t)+size) == HAL_OK)
    {
      uint32_t synqseq = SYNQSEQ_DEF;
      CSP_QSPI_WriteMemory((uint8_t*)&synqseq, address, sizeof(uint32_t));
      CSP_QSPI_WriteMemory((uint8_t*)json, address+sizeof(uint32_t), size);
      uint8_t nol = 0;
      CSP_QSPI_WriteMemory((uint8_t*)&nol, address+sizeof(uint32_t)+size, sizeof(uint8_t));
    }
    CSP_QSPI_EnableMemoryMappedMode();
  }
  else if (strcmp(url, "api/save-mode") == 0)
  {
    CSP_QUADSPI_Init();
    uint32_t address = (QSPI_MODE_ADDRESS-QSPI_START_ADDRESS);
    uint32_t size = json_input_size-strlen("{\"json\":")-1;
    if (CSP_QSPI_EraseSector(address, address+sizeof(uint32_t)+size) == HAL_OK)
    {
      uint32_t synqseq = SYNQSEQ_DEF;
      CSP_QSPI_WriteMemory((uint8_t*)&synqseq, address, sizeof(uint32_t));
      CSP_QSPI_WriteMemory((uint8_t*)json, address+sizeof(uint32_t), size);
      uint8_t nol = 0;
      CSP_QSPI_WriteMemory((uint8_t*)&nol, address+sizeof(uint32_t)+size, sizeof(uint8_t));
    }
    CSP_QSPI_EnableMemoryMappedMode();
    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if (strcmp(url, "api/clear-mode") == 0)
  {
    /** Остановка рампы */
    for (int iVz = 0; iVz < _crnt_letters_cnt; iVz++)
    {
      for (int iRamp = 0; iRamp < letters_store[iVz].SublitCnt; iRamp++)
      {
        FPGA_sendCmd(letters_store[iVz].SELF_CAN_ID, iRamp, CMD_RAMP_OFF, CMD_VOZB_STOP_RAMP);
        if (iRamp == 0)
          osDelay(100);
        /** Дожидаемся ответа и если его нет в течении 500 мс, повторяем отправку */
        if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
        {
          printf("--> Disable ramp TIMEOUT: %d\n", iRamp);
          FPGA_sendCmd(letters_store[iVz].SELF_CAN_ID, iRamp, CMD_RAMP_OFF, CMD_VOZB_STOP_RAMP);
          xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
        }
      }
    }
    /** Очистка выбранного режима */
    uint32_t mode = 0;
    json_get(json, "mode", (void*)&mode);
    
    for (int iVz = 0; iVz < _crnt_letters_cnt; iVz++)
    {
      for (int iRamp = 0; iRamp < letters_store[iVz].SublitCnt; iRamp++)
      {
        FPGA_modeErase(letters_store[iVz].SELF_CAN_ID, iRamp, mode);
        if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
        {
          printf("--> Clear mode TIMEOUT: %d\n", mode);
          FPGA_modeErase(letters_store[iVz].SELF_CAN_ID, iRamp, mode);
          xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
        }
      }
    }
    /** Отправка ответа */
    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if (strcmp(url, "api/set-jump") == 0)
  {
    for (int iJump = 0; iJump < 8; iJump++)
    {
      cmd.jump[iJump].jump_start_freq = 0;
      cmd.jump[iJump].jump_stop_freq = 0;
    }
    
   // memset(&cmd, 0x00, sizeof(struct fpga_cmd_jump));
    uint32_t can_id = 0;
    uint32_t num_sublit = 0;
    int tmp = 0;
    
    json_get(json, "can_id", (void*)&can_id);
    json_get(json, "num_sublit", (void*)&num_sublit);

    json_get(json, "jump_en", (void*)&cmd.jump_en);
    json_get(json, "mod", (void*)&tmp);
    cmd.mode = (uint16_t)tmp;
    json_get(json, "submod", (void*)&tmp);
    cmd.submod = (uint16_t)tmp;
    json_get(json, "curr_bank_cnt", (void*)&cmd.bank_cnt);
    json_get(json, "ext_ctrl", (void*)&cmd.ext_ctrl);

    json_get(json, "ext_osk_en", (void*)&tmp);
    cmd.ext_osk_en = (uint8_t)tmp;
    json_get(json, "amp_set_num", (void*)&tmp);
    cmd.amp_set_num = (uint8_t)tmp;
    json_get(json, "ext_flag1", (void*)&tmp);
    cmd.ext_flag1 = (uint8_t)tmp;
    json_get(json, "ext_flag2", (void*)&tmp);
    cmd.ext_flag2 = (uint8_t)tmp;

    json_get(json, "period", (void*)&cmd.period);
    json_get(json, "duty_cycle", (void*)&cmd.duty_cycle);
    json_get(json, "rnd_period", (void*)&cmd.rnd_period);
    json_get(json, "rnd_duty_cycle", (void*)&cmd.rnd_duty_cycle);

    //char *pArr = pvPortMalloc(300);
    char __IO *pArr = (char __IO*)(0xC0500000);
    //if (pArr != NULL)
    {
      memset((char*)pArr, 0x00, 300);
      char *jset = strstr(json, "jump_set\":");
      jset = jset+strlen("jump_set\":");
      strcpy((char*)pArr, jset);
      char *ppArr = (char*)pArr;
      for (int iJump = 0; iJump < 8; iJump++)
      {
        char *obj = strtok(ppArr, "}");
        if (obj != NULL)
        {
          json_get(obj, "start_freq", (void*)&cmd.jump[iJump].jump_start_freq);
          json_get(obj, "stop_freq", (void*)&cmd.jump[iJump].jump_stop_freq);
        }
        else
        {
          break;
        }
        ppArr += strlen(obj)+1;
      }
      //vPortFree(pArr);
    }

    if((cmd.amp_set_num<0) || (cmd.amp_set_num>1))
    {
      cmd.amp_set_num=0;
      cmd.ext_ctrl=0;
    }

    for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
    {
      if (can_id == letters_store[iVz].SELF_CAN_ID)
      {
        Manage_calcJumpsDDS(cmd.jump, &letters_store[iVz], (uint8_t)num_sublit);
        break;
      }
    }
    /** Отправка jump в возбудитель */
    FPGA_setJump(&cmd, can_id, num_sublit);
    //printf("--> Set jump\n");
    /** Дожидаемся ответа и в случае его отсутсвия повторяем отправку */
    if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
    {
      printf("--> Set jump TIMEOUT\n");
      FPGA_setJump(&cmd, can_id, num_sublit);
      xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
    }
    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if (strcmp(url, "api/set-bank") == 0)
  {
    uint32_t can_id = 0;
    uint32_t num_sublit = 0;
    uint32_t temp = 0;
    json_get(json, "can_id", (void*)&can_id);
    json_get(json, "num_sublit", (void*)&num_sublit);

    json_get(json, "mod", (void*)&temp);
    cmd_bank.mode = (uint16_t)temp;
    json_get(json, "submod", (void*)&temp);
    cmd_bank.submod = (uint16_t)temp;
    json_get(json, "bank_num", (void*)&cmd_bank.bank_num);

    json_get(json, "start_freq", (void*)&cmd_bank.bank.start_freq);
    json_get(json, "stop_freq", (void*)&cmd_bank.bank.stop_freq);
    json_get(json, "step_freq", (void*)&cmd_bank.bank.step_freq);
    json_get(json, "step_mask", (void*)&cmd_bank.bank.step_mask);
    json_get(json, "run_mask", (void*)&cmd_bank.bank.run_mask);

    json_get(json, "phase_in", (void*)&temp);
    cmd_bank.bank.phase_in = (uint16_t)temp;
    json_get(json, "next_bank", (void*)&temp);
    cmd_bank.bank.next_bank = (uint16_t)temp;

    json_get(json, "phase_toggle_step", (void*)&temp);
    cmd_bank.bank.phase_toggle_step = (uint8_t)temp;
    json_get(json, "flags_bank", (void*)&temp);
    cmd_bank.bank.flags_bank = (uint8_t)temp;

    json_get(json, "base_lsfr_polynome", (void*)&temp);
    cmd_bank.bank.base_lsfr_polynome = (uint16_t)temp;
    json_get(json, "hight_step_lsfr_polynome", (void*)&temp);
    cmd_bank.bank.hight_step_lsfr_polynome = (uint16_t)temp;
    json_get(json, "hight_run_lsfr_polynome", (void*)&temp);
    cmd_bank.bank.hight_run_lsfr_polynome = (uint16_t)temp;

    /** Выполняем пересчет банка */
    for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
    {
      if (can_id == letters_store[iVz].SELF_CAN_ID)
      {
        Manage_calcBanksDDS(&cmd_bank.bank, &letters_store[iVz], (uint8_t)num_sublit);
        break;
      }
    }
    /** Отправляем банк в возбудитель и дожидаемся ответа */
    FPGA_setBank(&cmd_bank, can_id, num_sublit);
    if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
    {
      FPGA_setBank(&cmd_bank, can_id, num_sublit);
      xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
    }
    /** На каждый банк возвращаем ответ */
    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if (strcmp(url, "api/get-amp") == 0)
  {
    uint32_t can_id = 0;
    uint32_t num_sublit = 0;
    json_get(json, "can_id", (void*)&can_id);
    json_get(json, "num_sublit", (void*)&num_sublit);

    /** Запрашиваем аплитуду с возбудителя */
    struct fpga_cmd_amp cmd_get_amp;
    FPGA_getAmp(&cmd_get_amp, can_id, num_sublit);
    /** Дожидаемся ответа */
    if (xQueueReceive(GetAmp_Queue, &(cmd_get_amp), pdMS_TO_TICKS(1000)) != pdTRUE)
    {
      FPGA_getAmp(&cmd_get_amp, can_id, num_sublit);
      if (xQueueReceive(GetAmp_Queue, &(cmd_get_amp), pdMS_TO_TICKS(1000)) != pdTRUE)
      {
        return NULL;
      }
    }
    /** Выполняем обратный пересчет амплитуд, аттенюации и частот */
    for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
    {
      if (can_id == letters_store[iVz].SELF_CAN_ID)
      {
        Manage_reCalcAmpsDDS(cmd_get_amp.amp, &letters_store[iVz], num_sublit);
        break;
      }
    }
    /** Формируем JSON */
    char __IO *amp_obj = (char __IO*)(0xC0500000); //pvPortMalloc(128*AMP_MAX_COUNT*AMP_SET_COUNT+20);
    //if (amp_obj != NULL)
    {
      memset((char*)amp_obj, 0x00, (128*AMP_MAX_COUNT*AMP_SET_COUNT+20));
      sprintf((char*)amp_obj, "[");
      for (int iAmp = 0; iAmp < AMP_MAX_COUNT*AMP_SET_COUNT; iAmp++)
      {
        char __IO *amp = (char __IO*)(0xC050F000);//pvPortMalloc(128);
        memset((char*)amp, 0x00, 128);
        //if (amp != NULL)
        {
          json_create((char*)amp, "amp_start_freq", (void*)cmd_get_amp.amp[iAmp].amp_start_freq, "int");
          json_create((char*)amp, "amp_in", (void*)((int)cmd_get_amp.amp[iAmp].amp_in), "int");
          json_create((char*)amp, "att_in", (void*)((int)cmd_get_amp.amp[iAmp].att_in), "int");
          strcat((char*)amp_obj, (char*)amp);
          if (iAmp != ((AMP_MAX_COUNT*AMP_SET_COUNT)-1))
            strcat((char*)amp_obj, ",");
          //vPortFree(amp);
        }
      }
      strcat((char*)amp_obj, "]");
      json_create((char*)json_dump, "amp", (void*)amp_obj, "obj");
      //vPortFree(amp_obj);
    }
    /** И возвращаем его */
    return (char*)json_dump;
  }
  else if (strcmp(url, "api/get-bank") == 0)
  {
    uint32_t can_id = 0;
    uint32_t num_sublit = 0;
    uint32_t mode = 0;
    uint32_t submode = 0;
    json_get(json, "can_id", (void*)&can_id);
    json_get(json, "num_sublit", (void*)&num_sublit);
    json_get(json, "mode", (void*)&mode);
    json_get(json, "submod", (void*)&submode);

    /** Запрашиваем Jump с возбудителя */
    FPGA_getJump(can_id, num_sublit, mode, submode);
    /** Дожидаемся ответа */
    struct fpga_cmd_jump cmd_get_jump;
    if (xQueueReceive(GetJump_Queue, &(cmd_get_jump), pdMS_TO_TICKS(500)) == pdTRUE)
    {
      json_create((char*)json_dump, "mode", (void*)cmd_get_jump.mode, "int");
      json_create((char*)json_dump, "submod", (void*)cmd_get_jump.submod, "int");
      json_create((char*)json_dump, "bank_cnt", (void*)cmd_get_jump.bank_cnt, "int");
      json_create((char*)json_dump, "ext_ctrl", (void*)cmd_get_jump.ext_ctrl, "int");
      json_create((char*)json_dump, "ext_osk_en", (void*)cmd_get_jump.ext_osk_en, "int");
      json_create((char*)json_dump, "amp_set_num", (void*)cmd_get_jump.amp_set_num, "int");
      json_create((char*)json_dump, "ext_flag1", (void*)cmd_get_jump.ext_flag1, "int");
      json_create((char*)json_dump, "ext_flag2", (void*)cmd_get_jump.ext_flag2, "int");
      json_create((char*)json_dump, "period", (void*)cmd_get_jump.period, "int");
      json_create((char*)json_dump, "duty_cycle", (void*)cmd_get_jump.duty_cycle, "int");
      json_create((char*)json_dump, "rnd_period", (void*)cmd_get_jump.rnd_period, "int");
      json_create((char*)json_dump, "rnd_duty_cycle", (void*)cmd_get_jump.rnd_duty_cycle, "int");
      for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
      {
        if (can_id == letters_store[iVz].SELF_CAN_ID)
        {
          json_create((char*)json_dump, "synth_freq", (void*)letters_store[iVz].SynthFreq, "int");
          Manage_reCalcJumpsDDS(cmd_get_jump.jump, &letters_store[iVz], num_sublit);
          break;
        }
      }
      /** Составляем JSON для jump */
      char *jump_obj = pvPortMalloc(128*JUMP_MAX_COUNT+20);
      if (jump_obj != NULL)
      {
        memset(jump_obj, 0x00, (128*JUMP_MAX_COUNT+20));
        sprintf(jump_obj, "[");
        for (int iJump = 0; iJump < JUMP_MAX_COUNT; iJump++)
        {
          char *jump = pvPortMalloc(128);
          if (jump != NULL)
          {
            memset(jump, 0x00, 128);
            json_create((char*)jump, "jump_id", (void*)iJump, "int");
            json_create((char*)jump, "jump_en", (void*)((cmd_get_jump.jump_en >> iJump)&0x01), "int");
            json_create((char*)jump, "jump_start_freq", (void*)cmd_get_jump.jump[iJump].jump_start_freq, "int");
            json_create((char*)jump, "jump_stop_freq", (void*)cmd_get_jump.jump[iJump].jump_stop_freq, "int");
            strcat(jump_obj, jump);
            if (iJump != (JUMP_MAX_COUNT-1))
              strcat(jump_obj, ",");
            vPortFree(jump);
          }
        }
        strcat(jump_obj, "]");
        json_create((char*)json_dump, "jump", (void*)jump_obj, "obj");
        vPortFree(jump_obj);
      }

      /** Составляем JSON для банков */
      memset((char*)json_banks_obj, 0x00, strlen((char*)json_banks_obj));
      sprintf((char*)json_banks_obj, "[");
      struct fpga_cmd_bank cmd_get_bank;
      cmd_get_jump.bank_cnt = 8;
      for (int iBank = 0; iBank < cmd_get_jump.bank_cnt; iBank++)
      {
        /** Запрос банка у возбудителя */
        FPGA_getBank(can_id, num_sublit, mode, submode, iBank);
        /** Дожидаемся ответа */
        if (xQueueReceive(GetBank_Queue, &(cmd_get_bank), pdMS_TO_TICKS(500)) == pdTRUE)
        {
          for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
          {
            if (can_id == letters_store[iVz].SELF_CAN_ID)
            {
              Manage_reCalcBanksDDS(&cmd_get_bank.bank, &letters_store[iVz], num_sublit);
            }
          }
          /** Заполняем JSON */
          char __IO *bank = (char __IO*)(0xC0500000);
          //if (bank != NULL)
          {
            memset((char*)bank, 0x00, 256);
            json_create((char*)bank, "bank_id", (void*)cmd_get_bank.bank_num, "int");
            json_create((char*)bank, "start_freq", (void*)cmd_get_bank.bank.start_freq, "int");
            json_create((char*)bank, "stop_freq", (void*)cmd_get_bank.bank.stop_freq, "int");
            json_create((char*)bank, "step_freq", (void*)cmd_get_bank.bank.step_freq, "int");
            json_create((char*)bank, "step_mask", (void*)cmd_get_bank.bank.step_mask, "int");
            json_create((char*)bank, "run_mask", (void*)cmd_get_bank.bank.run_mask, "int");
            json_create((char*)bank, "phase_in", (void*)((int)cmd_get_bank.bank.phase_in), "int");
            json_create((char*)bank, "next_bank", (void*)((int)cmd_get_bank.bank.next_bank), "int");
            json_create((char*)bank, "phase_toggle_step", (void*)((int)cmd_get_bank.bank.phase_toggle_step), "int");
            json_create((char*)bank, "flags_bank", (void*)((int)cmd_get_bank.bank.flags_bank), "int");
            json_create((char*)bank, "base_lsfr_polynome", (void*)((int)cmd_get_bank.bank.base_lsfr_polynome), "int");
            json_create((char*)bank, "hight_step_lsfr_polynome", (void*)((int)cmd_get_bank.bank.hight_step_lsfr_polynome), "int");
            json_create((char*)bank, "hight_run_lsfr_polynome", (void*)((int)cmd_get_bank.bank.hight_run_lsfr_polynome), "int");
            strcat((char*)json_banks_obj, (char*)bank);
            if (iBank != (cmd_get_jump.bank_cnt-1))
              strcat((char*)json_banks_obj, ",");
            //vPortFree(bank);
          }
        }
      }
      strcat((char*)json_banks_obj, "]");
      json_create((char*)json_dump, "banks", (void*)json_banks_obj, "obj");
    }
    return (char*)json_dump;
  }
  else if ( (strcmp(url, "api/save-amp") == 0) || (strcmp(url, "api/set-amp") == 0) )
  {
    uint16_t cmd = 0;
    if (strcmp(url, "api/save-amp") == 0)
      cmd = CMD_BANK_WRITE;
    else if (strcmp(url, "api/set-amp") == 0)
      cmd = CMD_BANK_SET;
    cmd_amp.cmd = cmd;

    uint32_t can_id = 0;
    uint32_t num_sublit = 0;
    printf("SAVE AMP --> JSON Get CAN_ID\n");
    json_get(json, "can_id", (void*)&can_id);
    printf("SAVE AMP --> JSON Get Sublit\n");
    json_get(json, "num_sublit", (void*)&num_sublit);

    printf("SAVE AMP --> Stop Ramp\n");
    /** Останавливаем рампу */
    FPGA_sendCmd(can_id, num_sublit, CMD_RAMP_OFF, CMD_VOZB_STOP_RAMP);
    if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
    {
      FPGA_sendCmd(can_id, num_sublit, CMD_RAMP_OFF, CMD_VOZB_STOP_RAMP);
      xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
    }

    /** Получаем из JSON значения амплитуд и частот */
    char __IO *pArr = (char __IO*)(0xC0500000);
    printf("SAVE AMP --> Get amplitude managment\n");
    //if (pArr != NULL)
    {
      memset((char*)pArr, 0x00, 128);
      char *ampset = strstr(json, "amp\":");
      ampset = ampset+strlen("amp\":");
      strcpy((char*)pArr, ampset);
      char *ppArr = (char*)pArr;
      int tmp = 0;
      for (int iAmp = 0; iAmp < AMP_MAX_COUNT*AMP_SET_COUNT; iAmp++)
      {
        char *obj = strtok(ppArr, "}");
        if (obj != NULL)
        {
          json_get(obj, "amp_start_freq", (void*)&cmd_amp.amp[iAmp].amp_start_freq);
          json_get(obj, "amp_in", (void*)&tmp);
          cmd_amp.amp[iAmp].amp_in = (uint16_t)tmp;
          json_get(obj, "att_in", (void*)&tmp);
          cmd_amp.amp[iAmp].att_in = (uint16_t)tmp;
        }
        else
        {
          break;
        }
        ppArr += strlen(obj)+1;
      }
    }
    printf("SAVE AMP --> Recalc amplitude\n");
    /** Пересчитываем в коды DDS */
    for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
    {
      if (can_id == letters_store[iVz].SELF_CAN_ID)
      {
        Manage_calcAmpsDDS(cmd_amp.amp, &letters_store[iVz], (uint8_t)num_sublit);
        break;
      }
    }

    printf("SAVE AMP --> Send amplitude to vozb\n");
    /** Отправка амплитуд в возбудитель */
    FPGA_setAmp(&cmd_amp, can_id, num_sublit);
    if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
    {
      FPGA_setAmp(&cmd_amp, can_id, num_sublit);
      xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
    }
    /** Включение рампы */
    printf("SAVE AMP --> Enable Ramp\n");
    FPGA_changeMode(can_id, num_sublit, device_info.mode_vz, 0);
    if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
    {
      FPGA_changeMode(can_id, num_sublit, device_info.mode_vz, 0);
      xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
    }

    printf("return post netcon from handler\n");
    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if ( strcmp(url, "api/erase-sector-vz") == 0 )
  {
    uint32_t address = 0, can_id = 0, num_sublit = 0;
    json_get(json, "can_id", (void*)&can_id);
    json_get(json, "num_sublit", (void*)&num_sublit);
    json_get(json, "address", (void*)&address);
    FPGA_eraseSector(address, can_id, num_sublit);
    if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
    {
      printf("--> Erase Sector FPGA TIMEOUT\n");
      FPGA_eraseSector(address, can_id, num_sublit);
      xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
    }
    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if ( strcmp(url, "api/flash-page-vz") == 0 )
  {
    int can_id = 0, num_sublit = 0;
    json_get(json, "can_id", (void*)&can_id);
    json_get(json, "num_sublit", (void*)&num_sublit);
    json_get(json, "address", (void*)&page.address);

    char __IO *pArr = (char __IO*)(0xC0500000);
    memset((char*)pArr, 0x00, 128);
    char *page_arr = strstr(json, "page\":");
    page_arr = page_arr+strlen("page\":")+1;
    strcpy((char*)pArr, page_arr);
    char *ppArr = (char*)pArr;
    char *pNum = NULL;
    for (int i = 0; i < 256; i++)
    {
      pNum = strtok((char*)ppArr, ",");
      if (pNum == NULL)
        break;
      page.page[i] = (uint8_t)atoi(pNum);
      ppArr += strlen(pNum)+1;
    }
    FPGA_programPage(&page, can_id, num_sublit);
    if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
    {
      printf("--> Program page FPGA TIMEOUT\n");
      FPGA_programPage(&page, can_id, num_sublit);
      xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
    }
    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if ( strcmp(url, "api/vz-settings") == 0 )
  {
    int can_id = 0;
    json_get(json, "can_id", (void*)&can_id);
    SysPkg_Typedef response;

    for (int iVz = 0; iVz < _crnt_letters_cnt; iVz++)
    {
      if (letters_store[iVz].SELF_CAN_ID == can_id)
      {
        json_get(json, "start_freq", (void*)&letters_store[iVz].StartFreq);
        json_get(json, "stop_freq", (void*)&letters_store[iVz].StopFreq);
        json_get(json, "synth_freq", (void*)&letters_store[iVz].SynthFreq);
        json_get(json, "ext_osk_en", (void*)&letters_store[iVz].signal_mod);
        
        letters_store[iVz].StartFreq = letters_store[iVz].StartFreq / 1000;
        letters_store[iVz].StopFreq = letters_store[iVz].StopFreq / 1000;

        for (int iSub = 0; iSub < letters_store[iVz].SublitCnt; iSub++)
        {
          letters_store[iVz].SubLit[iSub].StartFreq = letters_store[iVz].SubLit[iSub].StartFreq / 1000;
          letters_store[iVz].SubLit[iSub].StopFreq = letters_store[iVz].SubLit[iSub].StopFreq / 1000;
        }

        response = Utils_CmdCreate(letters_store[iVz].SELF_CAN_ID, SELF_NET_ID, CMD_VOZB_SET_STORE,
                                   sizeof(SysPkg_Typedef)+sizeof(struct litera_store), (uint8_t*)&letters_store[iVz], false, 0, 0);
        FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&response, sizeof(SysPkg_Typedef));
        FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&letters_store[iVz], sizeof(struct litera_store));

        if (xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500)) != pdTRUE)
        {
          printf("--> CMD_VOZB_SET_STORE Timeout Error\n");
          FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&response, sizeof(SysPkg_Typedef));
          FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&letters_store[iVz], sizeof(struct litera_store));
          xSemaphoreTake(CanHttpOk_Semaphore, pdMS_TO_TICKS(500));
        }
        
        letters_store[iVz].StartFreq = letters_store[iVz].StartFreq * 1000;
        letters_store[iVz].StopFreq = letters_store[iVz].StopFreq * 1000;

        for (int iSub = 0; iSub < letters_store[iVz].SublitCnt; iSub++)
        {
          letters_store[iVz].SubLit[iSub].StartFreq = letters_store[iVz].SubLit[iSub].StartFreq * 1000;
          letters_store[iVz].SubLit[iSub].StopFreq = letters_store[iVz].SubLit[iSub].StopFreq * 1000;
        }
        
        for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
        {
          response = Utils_CmdCreate(NET_ID_VOZB_1+iVz, SELF_NET_ID, CMD_VOZB_GET_STORE,
                                                   sizeof(SysPkg_Typedef), NULL, false, 0, 0);
          FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&response, sizeof(SysPkg_Typedef));
          osDelay(100);
        }
        break;
      }
    }
    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if ( strcmp(url, "api/save-net") == 0 )
  {
    if (strcmp(web->crnt_user, "admin") == 0)
    {
      json_get(json, "hash", (void*)device_info.admin_hash);
    }
    else if (strcmp(web->crnt_user, "user") == 0)
    {
      json_get(json, "hash", (void*)device_info.user_hash);
    }

    char __IO *tmp_ip = (char __IO*)(0xC0500000);
    memset((char*)tmp_ip, 0x00, 128);

    json_get(json, "ipaddr", (void*)tmp_ip);
    convert_ip((char*)tmp_ip, device_info.ipaddr);

    json_get(json, "netmask", (void*)tmp_ip);
    convert_ip((char*)tmp_ip, device_info.netmask);

    CSP_QUADSPI_Init();
    uint32_t address = (QSPI_DEV_ADDRESS-QSPI_START_ADDRESS);
    if (CSP_QSPI_EraseSector( address, address+sizeof(uint32_t)+sizeof(struct dev_info)) == HAL_OK)
    {
      uint32_t synqseq = SYNQSEQ_DEF;
      CSP_QSPI_WriteMemory((uint8_t*)&synqseq, address, sizeof(uint32_t));
      CSP_QSPI_WriteMemory((uint8_t*)&device_info, address+sizeof(uint32_t), sizeof(struct dev_info));
    }
    CSP_QSPI_EnableMemoryMappedMode();

    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }
  else if ( strcmp(url, "api/save-config") == 0 )
  {
    char __IO *tmp_mac = (char __IO*)(0xC0500000);
    memset((char*)tmp_mac, 0x00, 128);

    json_get(json, "dev_name", (void*)device_info.device_id);
    json_get(json, "dev_sn", (void*)device_info.sn);
    json_get(json, "dev_macaddr", (void*)tmp_mac);

    convert_mac((char*)tmp_mac, device_info.macaddr);

    CSP_QUADSPI_Init();
    uint32_t address = (QSPI_DEV_ADDRESS-QSPI_START_ADDRESS);
    if (CSP_QSPI_EraseSector( address, address+sizeof(uint32_t)+sizeof(struct dev_info)) == HAL_OK)
    {
      uint32_t synqseq = SYNQSEQ_DEF;
      CSP_QSPI_WriteMemory((uint8_t*)&synqseq, address, sizeof(uint32_t));
      CSP_QSPI_WriteMemory((uint8_t*)&device_info, address+sizeof(uint32_t), sizeof(struct dev_info));
    }
    CSP_QSPI_EnableMemoryMappedMode();

    json_create((char*)json_dump, "token", (void*)(web->token), "string");
    return (char*)json_dump;
  }

  return NULL;
}

/**
 * [convert_ip description]
 * @param str_ip [description]
 * @param out_ip [description]
 */
static void convert_ip(char *str_ip, uint8_t *out_ip)
{
  char *ip;
  for (int i = 0; i < 4; i++)
  {
    if (i == 0)
      ip = strtok(str_ip, ".");
    else
      ip = strtok(NULL, ".");
    
    if (ip == NULL)
      break;
    out_ip[i] = atoi(ip);
  }
  for (int i = 0; i < 100; i++) str_ip[i] = 0;
}

/**
 * [convert_mac description]
 * @param str_mac [description]
 * @param out_mac [description]
 */
static void convert_mac(char *str_mac, uint8_t *out_mac)
{
  char *mac;
  char *pEnd;
  for (int i = 0; i < 6; i++)
  {
    if (i == 0)
      mac = strtok(str_mac, ":");
    else
      mac = strtok(NULL, ":");

    if (mac == NULL)
      break;
    out_mac[i] = strtol(mac, &pEnd, 16);
  }
}

/*************************** END OF FILE ***************************/
