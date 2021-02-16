#ifndef _UTILS_H
#define _UTILS_H

#include <stdint.h>
#include <stdbool.h>

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

unsigned char Utils_crc8(unsigned char *pcBlock, uint32_t len);
unsigned short Utils_crc16(unsigned char * pcBlock, unsigned short len);
SysPkg_Typedef Utils_CmdCreate(uint8_t dest, uint8_t source, uint8_t cmd, uint32_t size, 
							uint8_t* payload, bool compress, uint8_t misc, uint8_t pack_cnt);

#endif //_NET_CMD_STR_H
