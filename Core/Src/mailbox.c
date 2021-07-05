#include "mailbox.h"


static void fdcan_send_msg( FDCAN_HandleTypeDef* fdcan, 
                            uint16_t dest_id, 
                            uint8_t prio,
                            uint8_t *data, 
                            uint8_t size );
static void MBX_ParsePool( struct mailbox *mailbox, void (*callback) (void *arg));


struct mailbox fdcan_mailbox[MBX_MAX_DEVICE_CNT];
uint8_t line_data_arr[MBX_MAX_SIZE_CB];
uint16_t fdcan_device_cnt = 0;
CAN_SendMsgTypedef CAN_SendMsg;


extern uint8_t SELF_NET_ID;





static void fdcan_send_msg( FDCAN_HandleTypeDef* fdcan, 
                            uint16_t dest_id, 
                            uint8_t prio,
                            uint8_t *data, 
                            uint8_t size )
{
  FDCAN_TxHeaderTypeDef TxHeader = {0};
  uint8_t net_cast = 0x00;

  net_cast = (dest_id >> 8) & 0xFF;
  TxHeader.Identifier = (prio << 24) | 
                        (net_cast << 16) | 
                        ((uint8_t)dest_id << 8) | 
                        SELF_NET_ID;

  TxHeader.IdType = FDCAN_EXTENDED_ID;
  TxHeader.TxFrameType = FDCAN_DATA_FRAME;
  TxHeader.DataLength = (size << 16);
  TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
  TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
  TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  TxHeader.MessageMarker = 0;

  while (HAL_FDCAN_GetTxFifoFreeLevel(fdcan) == 0) {}

  HAL_FDCAN_AddMessageToTxFifoQ(fdcan, &TxHeader, data);
}

/**
 * [FDCAN_SendBigData description]
 * @param fdcan    [description]
 * @param fdcan_id [description]
 * @param data     [description]
 * @param size     [description]
 */
void FDCAN_SendBigData( FDCAN_HandleTypeDef* fdcan, 
                        uint16_t dest_id, 
                        uint8_t *data, 
                        uint32_t size )
{
  const uint8_t page = 8;
  uint8_t send_size_chunk = 0;

  for(int iBulk = 0; iBulk < size; iBulk += page)
  {
    send_size_chunk = page;
    if ( size < (iBulk + send_size_chunk) )
    {
      send_size_chunk = size - iBulk;
    }
    fdcan_send_msg( fdcan, 
                    dest_id, 
                    FDCAN_PRIORITY_NORMAL, 
                    (data+iBulk), 
                    send_size_chunk );
  }
}

/**
 * [FDCAN_SendMessage description]
 * @param pkg     [description]
 * @param payload [description]
 */
void FDCAN_SendMessage( uint16_t dest_id, 
                        SysPkg_Typedef *pkg, 
                        uint8_t *payload )
{
  extern xQueueHandle CAN_SendMsgQueue;
  memset(&CAN_SendMsg, 0x00, sizeof(CAN_SendMsgTypedef));

  memcpy(&CAN_SendMsg.pkg, pkg, sizeof(SysPkg_Typedef));
  if ( (pkg->byte_cnt > sizeof(SysPkg_Typedef)) &&
       (payload != NULL) )
  {
    memcpy(CAN_SendMsg.payload, payload, pkg->byte_cnt-sizeof(SysPkg_Typedef));
  }
  CAN_SendMsg.dest_id = dest_id;
  xQueueSend( CAN_SendMsgQueue, (void*)&CAN_SendMsg, 0 );
}

/**
 * [FDCAN_SendMessagePhy description]
 * @param msg [description]
 */
void FDCAN_SendMessagePhy( CAN_SendMsgTypedef *msg )
{
  FDCAN_SendBigData( &hfdcan1, 
                     msg->dest_id, 
                     (uint8_t*)&msg->pkg, 
                     sizeof(SysPkg_Typedef) );

  if ( msg->pkg.byte_cnt > sizeof(SysPkg_Typedef) )
  {
    FDCAN_SendBigData( &hfdcan1, 
                       msg->dest_id, 
                       msg->payload, 
                       msg->pkg.byte_cnt-sizeof(SysPkg_Typedef) );
  }
}





void mbx_add_msg_to_pool( pool_mailboxes_t *pool, mail_t *msg )
{
  uint8_t is_device_exist = 0;
  uint32_t free_size = 0;
  mailbox_t *mbx;

  for (int iPos = 0; iPos < pool->connected_device; ++iPos)
  {
    mbx = &pool->mailboxes[iPos];
    if (mbx->sender_id == msg->sender_id)
    {
      free_size = MBX_MAX_BUFFER_SIZE - mbx->write_ptr;
      if (free_size < msg->data_len)
      {
        uint32_t diff = (msg->data_len - free_size);
        memcpy(&mbx->data[mbx->write_ptr], msg->data, free_size);
        memcpy(&mbx->data[0], &msg->data[free_size], diff);
        mbx->write_ptr = diff;
      }
      else
      {
        memcpy(&mbx->data[mbx->write_ptr], msg->data, msg->data_len);
        mbx->write_ptr += msg->data_len;
      }
      is_device_exist = 1;
      break;
    }
  }
  if (!is_device_exist)
  {
    mailbox[fdcan_device_cnt].sender_id = msg->sender_id;
    mailbox[fdcan_device_cnt].expected_bytes_cnt = pool->protocol_size;
    mailbox[fdcan_device_cnt].read_ptr = 0;
    mailbox[fdcan_device_cnt].write_ptr = msg->data_len;
    memcpy(pool->mailboxes[pool->connected_device].data, msg->data, msg->data_len);
    pool->connected_device++;
  }
}



static void mbx_parse_pool( pool_mailboxes_t *pool )
{
  uint32_t endpoint = 0;
  uint32_t tmpsize = 0;
  SysPkg_Typedef *pkg;
  uint16_t crc = 0;

  if (mailbox->wr_cnt == mailbox->rd_ptr)
  {
    mailbox->wait_packet_flag = 0;
    return;
  }
  else
  {
    if(mailbox->wait_packet_flag == 0)
    {
      mailbox->start_wait_packet_ticks = HAL_GetTick();
      mailbox->wait_packet_flag = 1;
    }
    else
    {
      uint32_t time_div = (HAL_GetTick() - mailbox->start_wait_packet_ticks);
      if ( time_div >= 100 )
      {
        mailbox->byte_cnt = sizeof(SysPkg_Typedef);
        mailbox->rd_ptr = mailbox->wr_cnt;
        mailbox->wait_packet_flag = 0;
        return;
      }
    }
    /** Проверка размера пришедшего пакета */
    if (mailbox->wr_cnt < mailbox->rd_ptr)
      tmpsize = MBX_MAX_SIZE_CB - mailbox->rd_ptr + mailbox->wr_cnt;
    else
      tmpsize = mailbox->wr_cnt - mailbox->rd_ptr;
    
    /** Если пришел еще не весь пакет */
    if (tmpsize < mailbox->byte_cnt) 
      return;

  }
  
  endpoint = (mailbox->rd_ptr+mailbox->byte_cnt);
  if (endpoint > MBX_MAX_SIZE_CB)
  {
    endpoint = endpoint - MBX_MAX_SIZE_CB;
  }
  if (endpoint > mailbox->rd_ptr)
  {
    memcpy(line_data_arr, mailbox->data+mailbox->rd_ptr, mailbox->byte_cnt);
  }
  else
  {
    memcpy(line_data_arr, mailbox->data+mailbox->rd_ptr, (MBX_MAX_SIZE_CB - mailbox->rd_ptr));
    memcpy(line_data_arr+(MBX_MAX_SIZE_CB - mailbox->rd_ptr), mailbox->data, endpoint);
  }

  pkg = (SysPkg_Typedef *)line_data_arr;

  if (pkg->SYNQSEQ != SYNQSEQ_DEF)
  {
    int offset = 0;
    while(pkg->SYNQSEQ != SYNQSEQ_DEF)
    {
      if ( mailbox->rd_ptr == mailbox->wr_cnt )
      {
        mailbox->byte_cnt = sizeof(SysPkg_Typedef);
        mailbox->wait_packet_flag = 0;
        return;
      }
      offset++;
      pkg = (SysPkg_Typedef *)(line_data_arr+offset);
      mailbox->rd_ptr++;
      if (mailbox->rd_ptr > MBX_MAX_SIZE_CB)
        mailbox->rd_ptr = 0;
    }
  }

  if (pkg->SYNQSEQ == SYNQSEQ_DEF)
  {
    if (pkg->byte_cnt != mailbox->byte_cnt)
    {
      if(pkg->byte_cnt < sizeof(SysPkg_Typedef) || pkg->byte_cnt >= MBX_MAX_SIZE_CB)
      {
        mailbox->byte_cnt = sizeof(SysPkg_Typedef);
        mailbox->rd_ptr = mailbox->wr_cnt;
      }
      else
      {
        mailbox->byte_cnt = pkg->byte_cnt;
      }
      mailbox->wait_packet_flag = 0;
      line_data_arr[0] = 0;
      return;
    }


    if ( (pkg->dest_id == NET_CAST) || (pkg->dest_id == SELF_NET_ID) )
    {
      if (pkg->byte_cnt == sizeof(SysPkg_Typedef))
      {
        crc = Utils_crc16((uint8_t*)pkg, sizeof(SysPkg_Typedef)-sizeof(uint16_t));
      }
      else
      {
        crc = Utils_crc16((uint8_t*)pkg, sizeof(SysPkg_Typedef)-sizeof(uint16_t));
        crc += Utils_crc16((uint8_t*)(line_data_arr+sizeof(SysPkg_Typedef)), 
                                     (pkg->byte_cnt-sizeof(SysPkg_Typedef)));
      }
      if (crc == pkg->crc16)
      {
        callback((void*)pkg);
      }
      else
      {
        // TODO: ERROR!
      }
    }
  }
  mailbox->wait_packet_flag = 0;
  mailbox->byte_cnt = sizeof(SysPkg_Typedef);

  uint32_t byte_cnt_temp = mailbox->rd_ptr + pkg->byte_cnt;

  mailbox->rd_ptr = (byte_cnt_temp > MBX_MAX_SIZE_CB) ? (byte_cnt_temp - MBX_MAX_SIZE_CB):byte_cnt_temp;

  line_data_arr[0] = 0;
}

/**
 * [MBX_parse description]
 * @param  callback   [description]
 */
void MBX_CheckNewMessages(void (*callback) (void *arg))
{
  for (int iMail = 0; iMail < fdcan_device_cnt; ++iMail)
  {
    MBX_ParsePool(&fdcan_mailbox[iMail], callback);
  }
}

/* USER CODE END 1 */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
