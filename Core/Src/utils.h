#ifndef _UTILS_H
#define _UTILS_H

#include <stdint.h>
#include <string.h>

#define SYNQSEQ_DEF  0x4563FEFE

typedef struct
{
  uint32_t SYNQSEQ;
  uint16_t cmd;
  uint16_t src_id;
  uint16_t dest_id;
  uint8_t misc;
  uint8_t pack_cnt;
  uint16_t byte_cnt;
  uint16_t crc16;
} SysPkg_Typedef;

uint8_t Utils_crc8(uint8_t *pcBlock, uint32_t len);
uint16_t Utils_crc16(uint8_t *pcBlock, uint32_t len);
void Utils_CmdCreate( SysPkg_Typedef *pkg, uint8_t *payload, uint32_t size );

#endif //_NET_CMD_STR_H
