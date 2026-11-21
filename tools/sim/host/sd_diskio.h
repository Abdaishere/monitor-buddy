#pragma once
#include <Arduino.h>
#include <SPI.h>
uint8_t sdcard_init(uint8_t cs, SPIClass *spi, int hz);
uint8_t sdcard_uninit(uint8_t drive);
uint32_t sdcard_num_sectors(uint8_t drive);
uint32_t sdcard_sector_size(uint8_t drive);
uint8_t sdcard_type(uint8_t drive);
