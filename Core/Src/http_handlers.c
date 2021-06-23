#include "http_handlers.h"
#include "jsonlib.h"
#include "structs.h"
#include "utils.h"
#include "vz.h"
#include "net_can.h"

extern struct   litera_store letters_store[MAX_VZ_CNT];
extern uint8_t  letters_cnt;
extern xSemaphoreHandle mtx_BlockStateRequest;
extern xSemaphoreHandle smpr_VzAmpGet;
extern xSemaphoreHandle smpr_VzJumpGet;
extern xSemaphoreHandle smpr_VzBankGet;

#if HH_USE_SDRAM == 1
  __IO char *tmp_get_var =  (__IO char*)(HH_TEMP_GET_HANDL_START_ADDR);
  __IO char *tmp_post_var = (__IO char*)(HH_TEMP_POST_HANDL_START_ADDR);
#else
  __IO char tmp_get_var[HH_TEMP_GET_HANDL_SIZE] =  {0};
  __IO char tmp_post_var[HH_TEMP_POST_HANDL_SIZE] = {0};
#endif

struct fpga_cmd_amp vFpgaGetAmp = {0};
struct fpga_cmd_jump vFpgaGetJump = {0};
struct fpga_cmd_bank vFpgaGetBank = {0};
struct litera_status vLiteraStatus = {0};

float amplitude[AMP_MAX_COUNT*AMP_SET_COUNT] = {0};

char tmp_name[32] = {0};

struct fpga_cmd_amp cmd_amp = {
  .sync = SYNC_AMP,
  .cmd = 0,
  .crc8 = 0
};



/**
 * [GET_Handler description]
 * @param url      [description]
 * @param response [description]
 */
__IO char* GET_Handler( char *url )
{
  memset( (void*)tmp_get_var, 0x00, NET_TEMP_GET_HANDL_SIZE );

  if (strstr(url, "api/getvz") != NULL)
  {
    __IO char *pVz = tmp_post_var;
    memset( (char*)pVz, 0x00, NET_TEMP_GET_HANDL_SIZE );
    strcat( (char*)pVz, "[ " );
    pVz += strlen( (char*)pVz );

    for (int iLit = 0; iLit < letters_cnt; iLit++)
    {
      for (int iSub = 0; iSub < letters_store[iLit].SublitCnt; iSub++)
      {
        json_create( (char*)pVz, "can_id", (void*)letters_store[iLit].SELF_CAN_ID, jsINT );
        json_create( (char*)pVz, "name", (void*)letters_store[iLit].Name, "string" );
        json_create( (char*)pVz, "start_freq", (void*)letters_store[iLit].SubLit[iSub].StartFreq, jsINT );
        json_create( (char*)pVz, "stop_freq", (void*)letters_store[iLit].SubLit[iSub].StopFreq, jsINT );
        json_create( (char*)pVz, "num_sublit", (void*)iSub, jsINT );
        json_create( (char*)pVz, "sublit_cnt", (void*)letters_store[iLit].SublitCnt, jsINT );
        json_create( (char*)pVz, "multisignal_allow", (void*)(int)letters_store[iLit].multisignal_allow, jsINT );
        json_create( (char*)pVz, "phase_toggle_allow", (void*)((uint32_t)letters_store[iLit].SubLit[iSub].phase_toggle_allow), jsINT);
        json_create( (char*)pVz, "amp", (void*)"[ ]", jsARR );
        if ( iLit != letters_cnt-1 )
        {
          strcat( (char*)pVz, "," );
        }
        else
        {
          if (iSub != letters_store[iLit].SublitCnt-1)
            strcat( (char*)pVz, "," );
        }
        pVz += strlen( (char*)pVz );
      }
    }
    strcat( (char*)pVz, " ]" );
    json_create( (char*)tmp_get_var, "vz", (void*)tmp_post_var, jsARR );
    
    memset((char*)tmp_post_var, 0x00, strlen((char*)tmp_post_var));
    char *vozb_edit_arr = (char *)(tmp_post_var);
    strcat( vozb_edit_arr, "[" );
    vozb_edit_arr++;

    for (int iLit = 0; iLit < letters_cnt; iLit++)
    {
      json_create((char*)vozb_edit_arr, "can_id", (void*)letters_store[iLit].SELF_CAN_ID, jsINT);
      json_create((char*)vozb_edit_arr, "name", (void*)letters_store[iLit].Name, jsSTRING);
      json_create((char*)vozb_edit_arr, "sn", (void*)letters_store[iLit].SN, jsSTRING);
      json_create((char*)vozb_edit_arr, "start_freq", (void*)letters_store[iLit].StartFreq, jsINT);
      json_create((char*)vozb_edit_arr, "stop_freq", (void*)letters_store[iLit].StopFreq, jsINT);
      json_create((char*)vozb_edit_arr, "synth_freq", (void*)letters_store[iLit].SynthFreq, jsINT);
      json_create((char*)vozb_edit_arr, "multisignal_allow", (void*)((uint32_t)letters_store[iLit].multisignal_allow), jsINT);
      json_create((char*)vozb_edit_arr, "sublit_cnt", (void*)letters_store[iLit].SublitCnt, jsINT);

      for (int iSub = 0; iSub < letters_store[iLit].SublitCnt; iSub++)
      {
        sprintf((char*)tmp_name, "start_freq_sub%d\0", (iSub+1));
        json_create((char*)vozb_edit_arr, (char*)tmp_name, (void*)letters_store[iLit].SubLit[iSub].StartFreq, jsINT);

        sprintf((char*)tmp_name, "stop_freq_sub%d\0", (iSub+1));
        json_create((char*)vozb_edit_arr, (char*)tmp_name, (void*)letters_store[iLit].SubLit[iSub].StopFreq, jsINT);

        sprintf((char*)tmp_name, "phase_toggle_allow%d\0", (iSub+1));
        json_create((char*)vozb_edit_arr, (char*)tmp_name, (void*)((int)letters_store[iLit].SubLit[iSub].phase_toggle_allow), jsINT);

        sprintf((char*)tmp_name, "pwr_sub%d\0", (iSub+1));
        json_create((char*)vozb_edit_arr, (char*)tmp_name, (void*)&letters_store[iLit].SubLit[iSub].PowerTreshhold, jsFLOAT);
      }

      if ( iLit != letters_cnt-1 )
        strcat((char*)vozb_edit_arr, ",");
      vozb_edit_arr += strlen(vozb_edit_arr);
    }
    strcat((char*)vozb_edit_arr, "]");
    json_create((char*)tmp_get_var, "vozb_edit", (void*)tmp_post_var, jsARR);
  }
  
  else if (strstr(url, "api/state-vz") != NULL)
  {
    if (xSemaphoreTake( mtx_BlockStateRequest, pdMS_TO_TICKS(500) ) == pdTRUE)
    {
      char *vz_state_obj = (char *)(tmp_get_var);
      sprintf((char*)vz_state_obj, "[");
      vz_state_obj++;
      for (int iVz = 0; iVz < letters_cnt; ++iVz)
      {
        if ( VZ_GetStatus( letters_store[iVz].SELF_CAN_ID ) != ERROR_NONE )
          continue;

        for (int iSub = 0; iSub < letters_store[iVz].SublitCnt; ++iSub)
        {
          json_create((char*)vz_state_obj, "name", (void*)letters_store[iVz].Name, jsSTRING);
          json_create((char*)vz_state_obj, "can_id", (void*)vLiteraStatus.SELF_CAN_ID, "int");
          json_create((char*)vz_state_obj, "num_sublit", (void*)iSub, "int");
          json_create((char*)vz_state_obj, "start_freq", (void*)letters_store[iVz].SubLit[iSub].StartFreq, "int");
          json_create((char*)vz_state_obj, "stop_freq", (void*)letters_store[iVz].SubLit[iSub].StopFreq, "int");
          json_create((char*)vz_state_obj, "temp_fpga", (void*)(int)vLiteraStatus.sublit[iSub].fpga_status.curr_Temp, "int");
          json_create((char*)vz_state_obj, "max_temp_fpga", (void*)(int)vLiteraStatus.sublit[iSub].fpga_status.max_Temp, "int");
          json_create((char*)vz_state_obj, "min_temp_fpga", (void*)(int)vLiteraStatus.sublit[iSub].fpga_status.min_Temp, "int");
          json_create((char*)vz_state_obj, "temp_stm", (void*)(int)vLiteraStatus.stm_temperture, "int");
          json_create((char*)vz_state_obj, "vcc_dds", (void*)vLiteraStatus.sublit[iSub].vcc_18, "int");
          json_create((char*)vz_state_obj, "vcc_fpga", (void*)vLiteraStatus.sublit[iSub].vcc_33, "int");
          json_create((char*)vz_state_obj, "pout_vz", (void*)(int)vLiteraStatus.sublit[iSub].power_det, "int");
          json_create((char*)vz_state_obj, "mode_vz", (void*)(int)device_status.mode_vz, "int");
          if ( iVz != letters_cnt-1 )
          {
            strcat((char*)vz_state_obj, ",");
          }
          else
          {
            if (iSub != letters_store[iVz].SublitCnt-1)
            {
              strcat((char*)vz_state_obj, ",");
            }
          }
          vz_state_obj += strlen(vz_state_obj);
        }
      }
      strcat((char*)vz_state_obj, "]");
      xSemaphoreGive( mtx_BlockStateRequest );
    }
  }

  return tmp_get_var;
}

/**
 * [POST_Handler description]
 * @param url      [description]
 * @param json     [description]
 * @param response [description]
 */
__IO char* POST_Handler( char *url, char *json )
{
  memset((void*)tmp_post_var, 0x00, NET_TEMP_POST_HANDL_SIZE);

  /** Получение аплитуды рампы возбудителя */
  else if (strstr(url, "api/get-amp") != NULL)
  {
    if (xSemaphoreTake( mtx_BlockStateRequest, pdMS_TO_TICKS(500) ) == pdTRUE)
    {
      uint32_t can_id = 0;
      uint32_t num_sublit = 0;
      json_get(json, "can_id", (void*)&can_id, jsINT);
      json_get(json, "num_sublit", (void*)&num_sublit, jsINT);

      /** Запрашиваем аплитуду с возбудителя */
      FPGA_getAmp( &vFpgaGetAmp, can_id, num_sublit );
      /** Дожидаемся ответа */
      if ( xSemaphoreTake( smpr_VzAmpGet, pdMS_TO_TICKS(1000) ) != pdTRUE )
      {
        FPGA_getAmp( &vFpgaGetAmp, can_id, num_sublit);
        if ( xSemaphoreTake( smpr_VzAmpGet, pdMS_TO_TICKS(1000) ) != pdTRUE )
          return NULL;
      }
      /** Выполняем обратный пересчет амплитуд, аттенюации и частот */
      for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
      {
        if (can_id == letters_store[iVz].SELF_CAN_ID)
        {
          VZ_reCalcAmpsDDS( vFpgaGetAmp.amp, amplitude, &letters_store[iVz], num_sublit );
          break;
        }
      }
      /** Формируем JSON */
      char *amp_obj = (char*)tmp_get_var;
      memset( (char*)amp_obj, 0x00, NET_TEMP_GET_HANDL_SIZE );
      sprintf( (char*)amp_obj, "[" );
      amp_obj++;
      for (int iAmp = 0; iAmp < AMP_MAX_COUNT*AMP_SET_COUNT; iAmp++)
      {
        json_create((char*)amp_obj, "amp_start_freq", (void*)vFpgaGetAmp.amp[iAmp].amp_start_freq, "int");
        json_create((char*)amp_obj, "amp_in", (void*)&amplitude[iAmp], "float");
        json_create((char*)amp_obj, "att_in", (void*)((int)vFpgaGetAmp.amp[iAmp].att_in), "int");
        if (iAmp != ((AMP_MAX_COUNT*AMP_SET_COUNT)-1))
          strcat((char*)amp_obj, ",");
        amp_obj += strlen(amp_obj);
      }
      strcat((char*)amp_obj, "]");

      xSemaphoreGive( mtx_BlockStateRequest );
    }
    json_create((char*)tmp_post_var, "amp", (void*)tmp_get_var, "obj");
    /** И возвращаем его */
    return tmp_post_var;
  }
  /** Запрос банков */
  else if (strstr(url, "api/get-bank") != NULL)
  {
    if (xSemaphoreTake( mtx_BlockStateRequest, pdMS_TO_TICKS(500) ) == pdTRUE)
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
      if ( xSemaphoreTake( smpr_VzJumpGet, pdMS_TO_TICKS(500)) != pdTRUE )
      {
        FPGA_getJump(can_id, num_sublit, mode, submode);
        if ( xSemaphoreTake( smpr_VzJumpGet, pdMS_TO_TICKS(500)) != pdTRUE )
          return NULL;
      }

      json_create((char*)tmp_post_var, "mode", (void*)(int)vFpgaGetJump.mode, "int");
      json_create((char*)tmp_post_var, "submod", (void*)(int)vFpgaGetJump.submod, "int");
      json_create((char*)tmp_post_var, "bank_cnt", (void*)vFpgaGetJump.bank_cnt, "int");
      json_create((char*)tmp_post_var, "ext_ctrl", (void*)vFpgaGetJump.ext_ctrl, "int");
      json_create((char*)tmp_post_var, "ext_osk_en", (void*)(int)vFpgaGetJump.ext_osk_en, "int");
      json_create((char*)tmp_post_var, "amp_set_num", (void*)(int)vFpgaGetJump.amp_set_num, "int");
      json_create((char*)tmp_post_var, "ext_flag1", (void*)(int)vFpgaGetJump.ext_flag1, "int");
      json_create((char*)tmp_post_var, "ext_flag2", (void*)(int)vFpgaGetJump.ext_flag2, "int");
      json_create((char*)tmp_post_var, "period", (void*)vFpgaGetJump.period, "int");
      json_create((char*)tmp_post_var, "duty_cycle", (void*)vFpgaGetJump.duty_cycle, "int");
      json_create((char*)tmp_post_var, "rnd_period", (void*)vFpgaGetJump.rnd_period, "int");
      json_create((char*)tmp_post_var, "rnd_duty_cycle", (void*)vFpgaGetJump.rnd_duty_cycle, "int");
      for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
      {
        if (can_id == letters_store[iVz].SELF_CAN_ID)
        {
          json_create((char*)tmp_post_var, "synth_freq", (void*)letters_store[iVz].SynthFreq, "int");
          VZ_reCalcJumpsDDS( vFpgaGetJump.jump, &letters_store[iVz], num_sublit );
          break;
        }
      }
      /** Составляем JSON для jump */
      char *jump_obj = (char*)tmp_get_var;
      memset(jump_obj, 0x00, NET_TEMP_GET_HANDL_SIZE);
      sprintf(jump_obj, "[");
      jump_obj++;
      for (int iJump = 0; iJump < JUMP_MAX_COUNT; iJump++)
      {
        json_create((char*)jump_obj, "jump_id", (void*)iJump, "int");
        json_create((char*)jump_obj, "jump_en", (void*)((vFpgaGetJump.jump_en >> iJump)&0x01), "int");
        json_create((char*)jump_obj, "jump_start_freq", (void*)vFpgaGetJump.jump[iJump].jump_start_freq, "int");
        json_create((char*)jump_obj, "jump_stop_freq", (void*)vFpgaGetJump.jump[iJump].jump_stop_freq, "int");
        if (iJump != (JUMP_MAX_COUNT-1))
          strcat(jump_obj, ",");
        jump_obj += strlen(jump_obj);
      }
      strcat(jump_obj, "]");
      json_create((char*)tmp_post_var, "jump", (void*)tmp_get_var, "obj");

      /** Составляем JSON для банков */
      char *bank_obj = (char*)tmp_get_var;
      memset( bank_obj, 0x00, NET_TEMP_GET_HANDL_SIZE );
      sprintf((char*)bank_obj, "[");
      bank_obj++;

      if((vFpgaGetJump.bank_cnt == 0) || (vFpgaGetJump.bank_cnt > 1024))
         vFpgaGetJump.bank_cnt = 8;

      for (int iBank = 0; iBank < vFpgaGetJump.bank_cnt; iBank++)
      {
        /** Запрос банка у возбудителя */
        FPGA_getBank(can_id, num_sublit, mode, submode, iBank);
        /** Дожидаемся ответа */
        if ( xSemaphoreTake( smpr_VzBankGet, pdMS_TO_TICKS(500)) != pdTRUE )
        {
          FPGA_getBank(can_id, num_sublit, mode, submode, iBank);
          if ( xSemaphoreTake( smpr_VzBankGet, pdMS_TO_TICKS(500)) != pdTRUE )
            return NULL;
        }

        for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
        {
          if (can_id == letters_store[iVz].SELF_CAN_ID)
          {
            VZ_reCalcBanksDDS( &vFpgaGetBank.bank, &letters_store[iVz], num_sublit );
          }
        }
        json_create((char*)bank_obj, "bank_id", (void*)vFpgaGetBank.bank_num, "int");
        json_create((char*)bank_obj, "start_freq", (void*)vFpgaGetBank.bank.start_freq, "int");
        json_create((char*)bank_obj, "stop_freq", (void*)vFpgaGetBank.bank.stop_freq, "int");
        json_create((char*)bank_obj, "step_freq", (void*)vFpgaGetBank.bank.step_freq, "int");
        json_create((char*)bank_obj, "step_mask", (void*)vFpgaGetBank.bank.step_mask, "int");
        json_create((char*)bank_obj, "run_mask", (void*)vFpgaGetBank.bank.run_mask, "int");
        json_create((char*)bank_obj, "phase_in", (void*)((int)vFpgaGetBank.bank.phase_in), "int");
        json_create((char*)bank_obj, "next_bank", (void*)((int)vFpgaGetBank.bank.next_bank), "int");
        json_create((char*)bank_obj, "phase_toggle_step", (void*)((int)vFpgaGetBank.bank.phase_toggle_step), "int");
        json_create((char*)bank_obj, "flags_bank", (void*)((int)vFpgaGetBank.bank.flags_bank), "int");
        json_create((char*)bank_obj, "bank_rpt", (void*)((int)vFpgaGetBank.bank.bank_rpt), "int");
        json_create((char*)bank_obj, "freq_rate", (void*)((int)vFpgaGetBank.bank.freq_rate), "int");
        json_create((char*)bank_obj, "step_freq_accel", (void*)((int)vFpgaGetBank.bank.step_freq_accel), "int");
        if (iBank != (vFpgaGetJump.bank_cnt-1))
          strcat((char*)bank_obj, ",");
        bank_obj += strlen(bank_obj);
      }
      strcat((char*)bank_obj, "]");

      xSemaphoreGive( mtx_BlockStateRequest );
    }
    json_create((char*)tmp_post_var, "banks", (void*)tmp_get_var, "obj");
    return tmp_post_var;
  }

  /** Изменение настроек возбудителей */
  else if ( strstr(url, "api/vz-settings") != NULL )
  {
    if (xSemaphoreTake( mtx_BlockStateRequest, pdMS_TO_TICKS(500) ) == pdTRUE)
    {
      int can_id = 0, tmp = 0;
      json_get(json, "can_id", (void*)&can_id);

      for (int iVz = 0; iVz < letters_cnt; iVz++)
      {
        if (letters_store[iVz].SELF_CAN_ID == can_id)
        {
          json_get(json, "start_freq", (void*)&letters_store[iVz].StartFreq);
          json_get(json, "stop_freq", (void*)&letters_store[iVz].StopFreq);
          json_get(json, "synth_freq", (void*)&letters_store[iVz].SynthFreq);
          json_get(json, "multisignal_allow", (void*)&tmp);
          letters_store[iVz].multisignal_allow = (uint8_t)tmp;

          char* tmp_name = (char *)(tmp_get_var);
          for (int iSub = 0; iSub < letters_store[iVz].SublitCnt; iSub++)
          {
            memset((char*)tmp_name, 0x00, 32);
            sprintf((char*)tmp_name, "start_freq_sub%d", (iSub+1));
            json_get(json, (char*)tmp_name, (void*)&letters_store[iVz].SubLit[iSub].StartFreq);

            memset((char*)tmp_name, 0x00, 32);
            sprintf((char*)tmp_name, "stop_freq_sub%d", (iSub+1));
            json_get(json, (char*)tmp_name, (void*)&letters_store[iVz].SubLit[iSub].StopFreq);
            
            memset((char*)tmp_name, 0x00, 32);
            sprintf((char*)tmp_name, "phase_toggle_allow%d", (iSub+1));
            json_get(json, (char*)tmp_name, (void*)&letters_store[iVz].SubLit[iSub].phase_toggle_allow);

            sprintf((char*)tmp_name, "pwr_sub%d", (iSub+1));
            char *p = strstr(json, (char*)tmp_name);
            if (p != NULL)
            {
              p += strlen((char*)tmp_name) + 2;
              char *end;
              letters_store[iVz].SubLit[iSub].PowerTreshhold = strtod(p, &end);
            }
          }

          VZ_SetStore( &letters_store[iVz] );
          //VZ_RequestStore();
        }
      }
      xSemaphoreGive( mtx_BlockStateRequest );
    }

    json_create((char*)tmp_post_var, "token", (void*)(webworker.token), "string");
    return tmp_post_var;
  }
  else if ( (strstr(url, "api/save-amp") != NULL) || (strstr(url, "api/set-amp") != NULL) )
  {
    if (xSemaphoreTake( mtx_BlockStateRequest, pdMS_TO_TICKS(500) ) == pdTRUE)
    {
      uint16_t cmd = 0;
      if (strcmp(url, "api/save-amp") == 0)
        cmd = CMD_BANK_WRITE;
      else if (strcmp(url, "api/set-amp") == 0)
        cmd = CMD_BANK_SET;
      cmd_amp.cmd = cmd;

      uint32_t can_id = 0;
      uint32_t num_sublit = 0;
      uint32_t temp = 0;
      json_get(json, "can_id", (void*)&can_id);
      json_get(json, "num_sublit", (void*)&num_sublit);

      /** Останавливаем рампу */
      for(int i = 0; i < 5; i++)
      {
        FPGA_sendCmd(can_id, num_sublit, CMD_RAMP_OFF, CMD_VOZB_STOP_RAMP);
        if (VZ_WaitOkResponse() == ERROR_NONE)
          break;
      }

      /** Получаем из JSON значения амплитуд и частот */
      char *pArr = (char *)(tmp_get_var);

      memset((char*)pArr, 0x00, 128);
      char *ampset = strstr(json, "amp\":");
      ampset = ampset+strlen("amp\":");
      strcpy((char*)pArr, ampset);
      char *ppArr = (char*)pArr;
      int tmp = 0;

      for (int iAmp = 0; iAmp < AMP_MAX_COUNT*AMP_SET_COUNT; iAmp++)
      {
        char *obj = strstr(ppArr, "{");
        if (obj != NULL)
        {
          json_get(obj, "amp_start_freq", (void*)&cmd_amp.amp[iAmp].amp_start_freq);

          char *amp_in = strstr(obj, "amp_in");
          amp_in = strstr(amp_in, ":") + 1;
          char *end = NULL;
          amplitude[iAmp] = strtof(amp_in, &end);

          json_get(obj, "att_in", (void*)&tmp);
          cmd_amp.amp[iAmp].att_in = (uint16_t)tmp;
        }
        else
        {
          break;
        }
        ppArr = strstr(ppArr, "}") + 1;
      }
      
      /** Пересчитываем в коды DDS */
      for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
      {
        if (can_id == letters_store[iVz].SELF_CAN_ID)
        {
          VZ_calcAmpsDDS(cmd_amp.amp, amplitude, &letters_store[iVz], (uint8_t)num_sublit);
          break;
        }
      }

      /** Отправка амплитуд в возбудитель */
      for(int i = 0; i < 5; i++)
      {
        FPGA_setAmp(&cmd_amp, can_id, num_sublit);
        if (VZ_WaitOkResponse() == ERROR_NONE)
          break;
      }

      /** Включение рампы */
      for(int i = 0; i < 5; i++)
      {
        FPGA_changeMode(can_id, num_sublit, device_status.mode_vz, 0);
        if (VZ_WaitOkResponse() == ERROR_NONE)
          break;
      }
      
      xSemaphoreGive( mtx_BlockStateRequest );
     }
    json_create((char*)tmp_post_var, "token", (void*)(webworker.token), "string");
    return tmp_post_var;
  }

  return NULL;
}


/*************************** END OF FILE ***************************/
