#ifndef __MAILBOX_H
#define __MAILBOX_H


#define MBX_MAX_DEVICE_CNT    7
#define MBX_MAX_BUFFER_SIZE     1024

#define MBX_FRAME_SIZE        8

/**
 * @FDCAN_Priority_levels
 */
#define FDCAN_PRIORITY_NORMAL   0x0F

/**
 * @FDCAN_Groups
 */
#define FDCAN_NET_CAST_GROUP    0xFF00
#define FDCAN_PI_PDU_GROUP      0xFF01
#define FDCAN_PI_PU_GROUP       0xFF02
#define FDCAN_PU_PDU_GROUP      0xFF03
#define FDCAN_VZ_GROUP          0xFF04


typedef struct
{
  uint16_t sender_id;
  uint32_t data_len;
  uint8_t data[MBX_FRAME_SIZE];
} message_t;

typedef struct
{
  uint16_t dest_id;
  SysPkg_Typedef pkg;
  uint8_t payload[512];
} CAN_SendMsgTypedef;


typedef struct
{
  uint16_t sender_id;
  uint16_t read_ptr;
  uint16_t write_ptr;
  uint32_t expected_bytes_cnt;
  uint32_t pkg_start_time;
  uint8_t recv_data_flag;
  uint8_t data[MBX_MAX_BUFFER_SIZE];
} mailbox_t;


typedef struct
{
  protocol
  protocol_size
  mailboxes[]
  connected_device
  void (*msgHandler)(void*arg);
} pool_mailboxes_t;



void FDCAN_SendBigData    ( FDCAN_HandleTypeDef* fdcan, uint16_t dest_id, uint8_t *data, uint32_t size );
void FDCAN_SendMessage    ( uint16_t dest_id, SysPkg_Typedef *pkg, uint8_t *payload );
void FDCAN_SendMessagePhy ( CAN_SendMsgTypedef *msg );

void MBX_AddMsgToPool     ( struct mailbox *mailbox, CAN_MsgTypedef *msg );
void MBX_CheckNewMessages ( void (*callback) (void *arg) );



#endif
