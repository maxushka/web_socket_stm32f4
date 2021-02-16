/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "config.h"
#include "jamm_structs.h"
#include "fdcan.h"
#include "rng.h"
#include "quadspi.h"
#include "net.h"
#include "utils.h"
#include "message_buffer.h"
#include "net_can.h"
#include "dualcore_msg.h"
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
extern RNG_HandleTypeDef hrng;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
extern struct http_file_system http_file_system;
extern __IO char *sdram_http_address;

extern struct webworker webworker;
extern struct dev_info device_info;

extern __IO uint8_t *flash_http_address;
extern __IO uint8_t *qspi_dev_addr;
extern __IO uint8_t *qspi_mode_addr;
extern __IO uint8_t *qspi_std_addr;

extern struct litera_store letters_store[MAX_VZ_CNT];
extern int _crnt_letters_cnt;

extern uint8_t SELF_NET_ID;
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
xSemaphoreHandle CanHttpOk_Semaphore;
xQueueHandle CAN_MsgQueue;
xQueueHandle GetAmp_Queue;
xQueueHandle GetJump_Queue;
xQueueHandle GetBank_Queue;
xQueueHandle GetState_Queue;
/* USER CODE END Variables */
osThreadId MainTask_M7Handle;
osThreadId CANTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void CAN_CMD_ParseCallback(SysPkg_Typedef *pkg, uint8_t *payload);
/* USER CODE END FunctionPrototypes */

void thread_MainTask_M7(void const * argument);
void thread_CANTask(void const * argument);

extern void MX_LWIP_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  CanHttpOk_Semaphore = xSemaphoreCreateCounting( 4, 0 );
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  CAN_MsgQueue =  xQueueCreate(7, sizeof(CAN_Msg_typedef));
  GetAmp_Queue = xQueueCreate(2, sizeof(struct fpga_cmd_amp));
  GetJump_Queue = xQueueCreate(2, sizeof(struct fpga_cmd_jump));
  GetBank_Queue = xQueueCreate(2, sizeof(struct fpga_cmd_bank));
  GetState_Queue = xQueueCreate(1, sizeof(struct litera_status));
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of MainTask_M7 */
  osThreadDef(MainTask_M7, thread_MainTask_M7, osPriorityNormal, 0, 256);
  MainTask_M7Handle = osThreadCreate(osThread(MainTask_M7), NULL);

  /* definition and creation of CANTask */
  osThreadDef(CANTask, thread_CANTask, osPriorityNormal, 0, 1024);
  CANTaskHandle = osThreadCreate(osThread(CANTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_thread_MainTask_M7 */
/**
  * @brief  Function implementing the MainTask_M7 thread.
  * @param  argument: Not used
  * @retval None
  */
struct irp_afu_state
{
  uint8_t irp[MAX_LIT_CNT];
  uint8_t afu[MAX_LIT_CNT];
  uint8_t lit_en[MAX_LIT_CNT];
};
struct irp_afu_state state;
float akb = 0;
/* USER CODE END Header_thread_MainTask_M7 */
void thread_MainTask_M7(void const * argument)
{
  /* init code for LWIP */
  //MX_LWIP_Init();
  /* USER CODE BEGIN thread_MainTask_M7 */
  uint32_t synqseq = 0;
  extern uint8_t IP_ADDRESS[4];
  extern uint8_t NETMASK_ADDRESS[4];
/** ***************************************************** */
////////////////////
// DUAL CORE TEST //
////////////////////

//  HAL_NVIC_SetPriority(HSEM2_IRQn, 10, 0);
//  HAL_NVIC_EnableIRQ(HSEM2_IRQn);
//  HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(1U));

//  DCM_Init();
//  char test_string[16] = "Hello, Core2!";
//  DCM_SendMsg(0x55, (uint8_t*)test_string, 13);
/** ***************************************************** */
  CSP_QUADSPI_Init();
  CSP_QSPI_EnableMemoryMappedMode();
  /** Проверяем наличие структуры dev_info */
  memcpy(&synqseq, (uint8_t*)qspi_dev_addr, sizeof(uint32_t));
  synqseq = 0;
  if (synqseq != SYNQSEQ_DEF)
  {
    /** Если ее нет, записываем значения по умолчанию */
    CSP_QUADSPI_Init();
    uint32_t address = (QSPI_DEV_ADDRESS-QSPI_START_ADDRESS);
    if (CSP_QSPI_EraseSector( address, address+sizeof(uint32_t)+sizeof(struct dev_info)) == HAL_OK)
    {
      synqseq = SYNQSEQ_DEF;
      CSP_QSPI_WriteMemory((uint8_t*)&synqseq, address, sizeof(uint32_t));
      CSP_QSPI_WriteMemory((uint8_t*)&device_info, address+sizeof(uint32_t), sizeof(struct dev_info));
    }
    CSP_QSPI_EnableMemoryMappedMode();
  }
  else
  {
    /** Иначе копируем себе в оперативную память */
    memcpy(&device_info, (uint8_t*)(qspi_dev_addr+sizeof(uint32_t)), sizeof(struct dev_info));
  }

  memcpy(IP_ADDRESS, device_info.ipaddr, sizeof(uint8_t)*4);
  memcpy(NETMASK_ADDRESS, device_info.netmask, sizeof(uint8_t)*4);
  MX_LWIP_Init();

  /** Проверяем наличие в памяти режимов с задачами */
  synqseq = 0;
  memcpy(&synqseq, (uint8_t*)qspi_mode_addr, sizeof(uint32_t));
  if (synqseq != SYNQSEQ_DEF)
  {
    /** Если их нет, записываем пустую форму */
    char *mode_buf = pvPortMalloc(256);
    if (mode_buf != NULL)
    {
      CSP_QUADSPI_Init();
      sprintf(mode_buf, "%s","[{\"Name\": \"Заградительный\",\"Index\": 0,\"Tasks\": [[]]},{\"Name\": \"Оконный\",\"Index\": 1,\"Tasks\": [[],[],[],[],[],[],[],[],[]]},{\"Name\": \"Прицельный\",\"Index\": 1,\"Tasks\": [[],[],[],[],[],[],[],[],[]]}]\0");
      uint32_t address = (QSPI_MODE_ADDRESS-QSPI_START_ADDRESS);
      if (CSP_QSPI_EraseSector( address, address+sizeof(uint32_t)+strlen(mode_buf)+1) == HAL_OK)
      {
        synqseq = SYNQSEQ_DEF;
        CSP_QSPI_WriteMemory((uint8_t*)&synqseq, address, sizeof(uint32_t));
        CSP_QSPI_WriteMemory((uint8_t*)mode_buf, address+sizeof(uint32_t), strlen(mode_buf)+1);
      }
      vPortFree(mode_buf);
      CSP_QSPI_EnableMemoryMappedMode();
    }
  }

  /** Проверяем наличие в памяти стандартов */
  synqseq = 0;
  memcpy(&synqseq, (uint8_t*)qspi_std_addr, sizeof(uint32_t));
  if (synqseq != SYNQSEQ_DEF)
  {
    /** Если их нет, записываем пустую форму */
    char *std_buf = pvPortMalloc(4);
    if (std_buf != NULL)
    {
      CSP_QUADSPI_Init();
      sprintf(std_buf, "%s","[ ]\0");
      uint32_t address = (QSPI_STD_ADDRESS-QSPI_START_ADDRESS);
      if (CSP_QSPI_EraseSector(address, address+sizeof(uint32_t)+strlen(std_buf)+1) == HAL_OK)
      {
        synqseq = SYNQSEQ_DEF;
        CSP_QSPI_WriteMemory((uint8_t*)&synqseq, address, sizeof(uint32_t));
        CSP_QSPI_WriteMemory((uint8_t*)std_buf, address+sizeof(uint32_t), strlen(std_buf)+1);
      }
      vPortFree(std_buf);
      CSP_QSPI_EnableMemoryMappedMode();
    }
  }

  /** Из внутренней FLASH копируем структуру сайта и сам сайт в SDRAM */
  memcpy(&http_file_system.http_file_cnt, (uint8_t*)flash_http_address, sizeof(uint32_t));
  http_file_system.http_file_store = pvPortMalloc(sizeof(struct http_files)*http_file_system.http_file_cnt);
  if (http_file_system.http_file_store != NULL)
  {
    memcpy(http_file_system.http_file_store, (uint8_t*)flash_http_address+sizeof(uint32_t), 
                                             sizeof(struct http_files)*http_file_system.http_file_cnt);
  }
  int end_web_site = http_file_system.http_file_store[http_file_system.http_file_cnt-1].offset+
                     http_file_system.http_file_store[http_file_system.http_file_cnt-1].page_size;
  memcpy((char*)sdram_http_address, (char*)flash_http_address, end_web_site);

  /** Запрос возбудителей в сети, для отображения на сайте*/
  for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
  {
    SysPkg_Typedef response = Utils_CmdCreate(NET_ID_VOZB_1+iVz, SELF_NET_ID, CMD_VOZB_GET_STORE,
                                             sizeof(SysPkg_Typedef), NULL, false, 0, 0);
    FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&response, sizeof(SysPkg_Typedef));
    HAL_Delay(100);
  }

  /** Запускаем HTTP-соединение с новым токеном */
  uint32_t new_token = 0;
  HAL_RNG_GenerateRandomNumber(&hrng, &new_token);
  sprintf(webworker.token, "%x", new_token);
  sys_thread_new("HTTP", http_server_netconn_thread, (void*)&webworker, 4096, osPriorityNormal);

  //SysPkg_Typedef response;

  /* Infinite loop */
  for(;;)
  {
//  for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
//  {
//    SysPkg_Typedef response = Utils_CmdCreate(NET_ID_VOZB_1+iVz, SELF_NET_ID, CMD_VOZB_GET_STORE,
//                                             sizeof(SysPkg_Typedef), NULL, false, 0, 0);
//    FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&response, sizeof(SysPkg_Typedef));
//    HAL_Delay(100);
//  }
    osDelay(100);
  }
  /* USER CODE END thread_MainTask_M7 */
}

/* USER CODE BEGIN Header_thread_CANTask */
/**
* @brief Function implementing the CANTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_thread_CANTask */
void thread_CANTask(void const * argument)
{
  /* USER CODE BEGIN thread_CANTask */
  CAN_Msg_typedef msg;
  /* Infinite loop */
  for(;;)
  {
    xQueueReceive(CAN_MsgQueue, &( msg ), portMAX_DELAY);
    MBX_AddMsgToPool(fdcan_mailbox, &fdcan_device_cnt, msg.Header.Identifier, msg.Data, (msg.Header.DataLength >> 16) );
    for (int iMail = 0; iMail < fdcan_device_cnt; iMail++)
    {
      MBX_ParsePool(&fdcan_mailbox[iMail], CAN_CMD_ParseCallback);
    }
  }
  /* USER CODE END thread_CANTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
struct fpga_cmd_amp amp;
struct fpga_cmd_jump jump;
struct fpga_cmd_bank bank;
static void CAN_CMD_ParseCallback(SysPkg_Typedef *pkg, uint8_t *payload)
{
  uint8_t isLitExist = 0;
  SysPkg_Typedef response;
  
  struct fpga_cmd *fpga_cmd;
  struct litera_store *vozb;


  struct irp_afu_state
  {
    uint8_t irp[MAX_LIT_CNT];
    uint8_t afu[MAX_LIT_CNT];
    uint8_t lit_en[MAX_LIT_CNT];
  };
  struct irp_afu_state state;

  switch(pkg->cmd)
  {
    case CMD_PU_PDU_INFO:
        memcpy(&state, payload, sizeof(struct irp_afu_state));
        for (int iLit= 0; iLit < MAX_LIT_CNT; iLit++)
        {
          device_info.irp[iLit] = state.irp[iLit];
          device_info.afu[iLit] = state.afu[iLit];
          device_info.lit_en[iLit] = state.lit_en[iLit];
        }
      break;
    case CMD_LIT_CHANGE_STATE:
      break;
    case CMD_GLOBAL_EMIT:
        device_info.emit = pkg->misc;
      break;

    case CMD_PU_PI_READY:
        response = Utils_CmdCreate(NET_CAST, SELF_NET_ID, CMD_VOZB_GET_STORE,
                                     sizeof(SysPkg_Typedef), NULL, false, 0, 0);
        FDCAN_SendBigData(&hfdcan1, SELF_NET_ID, (uint8_t*)&response, sizeof(SysPkg_Typedef));
      break;
    case CMD_VOZB_GET_STORE:
      vozb = (struct litera_store *)payload;
      vozb->StartFreq = vozb->StartFreq * 1000;
      vozb->StopFreq = vozb->StopFreq * 1000;
      for (int iSub = 0; iSub < vozb->SublitCnt; iSub++)
      {
        vozb->SubLit[iSub].StartFreq = vozb->SubLit[iSub].StartFreq * 1000;
        vozb->SubLit[iSub].StopFreq = vozb->SubLit[iSub].StopFreq * 1000;
      }
      for (int iVz = 0; iVz < MAX_VZ_CNT; iVz++)
      {
        if (vozb->SELF_CAN_ID == letters_store[iVz].SELF_CAN_ID)
        {
          memcpy(&letters_store[iVz], payload, sizeof(struct litera_store));
          isLitExist = 1;
        }
      }
      if (isLitExist == 0)
      {
        memcpy(&letters_store[_crnt_letters_cnt], payload, sizeof(struct litera_store));
        _crnt_letters_cnt++;
      }
      break;

      case CMD_VOZB_OK:
        xSemaphoreGive( CanHttpOk_Semaphore );
      break;

      case CMD_VOZB_GET:
        fpga_cmd = (struct fpga_cmd*)payload;
        switch(fpga_cmd->sync)
        {
          case SYNC_AMP:
            memcpy(&amp, payload, sizeof(struct fpga_cmd_amp));
            xQueueSend(GetAmp_Queue, &(amp), 0);
            break;

          case SYNC_JUMP:
            memcpy(&jump, payload, sizeof(struct fpga_cmd_jump));
            xQueueSend(GetJump_Queue, &(jump), 0);
            break;

          case SYNC_BANK:
            memcpy(&bank, payload, sizeof(struct fpga_cmd_bank));
            xQueueSend(GetBank_Queue, &(bank), 0);
            break;
        }
      break;

    case CMD_VOZB_GET_STATUS:
      xQueueSend(GetState_Queue, (payload), 0);
      break;
  }
}
/* USER CODE END Application */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
