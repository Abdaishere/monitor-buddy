#pragma once
#include <stdint.h>
typedef uint8_t DSTATUS;
typedef enum { RES_OK = 0, RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR } DRESULT;
#define STA_NOINIT 0x01
#define STA_NODISK 0x02
DSTATUS disk_initialize(uint8_t drive);
DRESULT disk_read(uint8_t drive, uint8_t *buffer, uint32_t sector, unsigned count);
